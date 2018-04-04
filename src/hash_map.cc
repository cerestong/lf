#include <thread>
#include <assert.h>
#include "lf/hash_map.hh"
#include "lf/hash.hh"
#include "lf/bit_util.hh"

namespace lf
{

static int strnncoll(const uint8_t *s, size_t slen,
                     const uint8_t *t, size_t tlen)
{
    size_t len = (slen > tlen) ? tlen : slen;
    while (len--)
    {
        if (*s++ != *t++)
        {
            return ((int)s[-1] - (int)t[-1]);
        }
    }
    return slen > tlen ? 1 : slen < tlen ? -1 : 0;
}

/*
        an element of the list
    */
struct SList
{
    intptr_t volatile link; // a pointer to the next element in a list and a flag
    uint32_t hashnr;        // reversed hash number, for sorting
    const uint8_t *key;
    size_t keylen;
    /*
            data is stored here, directly after the keylen.
            thus the pointer to data is (void*)(slist_element_ptr+1)
        */
};

const int LF_HASH_OVERHEAD = sizeof(SList);

/*
        a structure to pass the context(pointers two the three successive
        elements in a list)
    */
struct Cursor
{
    intptr_t volatile *prev;
    SList *curr;
    SList *next;
};

/*
        the last bit in SList::link is a "deleted" flag.
        the helper macros below convert it to a pure pointer or a pure flag
    */
#define PTR(V) (SList *)((V) & (~(intptr_t)1))
#define DELETED(V) ((V)&1)

/*
        search for hashnr/key/keylen in the list starting from 'head' and
        position the cursor. the list is order by hashnr,key

        return
            0 - not found
            1 - found
        note
            cursor is positioned in either case
            pins[0..2] are used, they are not removed on return
    */
static int lfind(SList *volatile *head, uint32_t hashnr,
                 const uint8_t *key, size_t keylen,
                 Cursor *cursor, Pins *pins)
{
    uint32_t cur_hashnr;
    const uint8_t *cur_key;
    size_t cur_keylen;
    intptr_t link;

retry:
    cursor->prev = (intptr_t *)head;
    do
    {
        cursor->curr = (SList *)(*cursor->prev);
        pin(pins, 1, cursor->curr);
    } while (*cursor->prev != (intptr_t)cursor->curr);
    for (;;)
    {
        if (unlikely(!cursor->curr))
        {
            return 0; /* end of the list */
        }
        do
        {
            link = cursor->curr->link;
            cursor->next = PTR(link);
            pin(pins, 0, cursor->next);
        } while (link != cursor->curr->link);

        cur_hashnr = cursor->curr->hashnr;
        cur_key = cursor->curr->key;
        cur_keylen = cursor->curr->keylen;
        if (*cursor->prev != (intptr_t)cursor->curr)
        {
            std::this_thread::yield();
            goto retry;
        }

        if (!DELETED(link))
        {
            if (cur_hashnr >= hashnr)
            {
                int r = 1;
                if (cur_hashnr > hashnr ||
                    (r = strnncoll((uint8_t *)cur_key, cur_keylen,
                                   (uint8_t *)key, keylen)) >= 0)
                {
                    return !r;
                }
            }
            cursor->prev = &(cursor->curr->link);
            pin(pins, 2, cursor->curr);
        }
        else
        {
            /*
                    we found a deleted node - be nice, help the other thread
                    and remove this deleted node
                */
            if (atomic_casptr((void *volatile *)cursor->prev,
                              (void **)&cursor->curr,
                              cursor->next))
            {
                pins_free(pins, cursor->curr);
            }
            else
            {
                std::this_thread::yield();
                goto retry;
            }
        }

        cursor->curr = cursor->next;
        pin(pins, 1, cursor->curr);
    }
}

/*
    Search for list element satisfying condition specified by match
    function and position cursor on it.

    @param head          Head of the list to search in.
    @param first_hashnr  Hash value to start from.
    @param last_hashnr   Hash value to stop after
    @param match         Match function
    @param cursor        Cursor to be position
    @param pins          Pins for the calling thread to be used
                         during search for pinning result.
    @retval 0 - not found
    @retval 1 - found
    */
static int lfind_match(SList *volatile *head,
                       uint32_t first_hashnr, uint32_t last_hashnr,
                       hash_match_func *match,
                       Cursor *cursor, Pins *pins)
{
    int32_t cur_hashnr;
    intptr_t link;

retry:
    cursor->prev = (intptr_t *)head;
    do
    { /* PTR() is not necessory below, head is a dummy node */
        cursor->curr = (SList *)(*cursor->prev);
        pin(pins, 1, cursor->curr);
    } while (*cursor->prev != (intptr_t)cursor->prev);
    for (;;)
    {
        if (unlikely(!cursor->curr))
            return 0; // end of the list
        do
        {
            link = cursor->curr->link;
            cursor->next = PTR(link);
            pin(pins, 0, cursor->next);
        } while (link != cursor->curr->link);
        cur_hashnr = cursor->curr->hashnr;
        if (*cursor->prev != (intptr_t)cursor->curr)
        {
            std::this_thread::yield();
            goto retry;
        }
        if (!DELETED(link))
        {
            if (cur_hashnr >= first_hashnr)
            {
                if (cur_hashnr > last_hashnr)
                    return 0;
                if (cur_hashnr & 1)
                {
                    // normal node. check if element matches condition
                    if ((*match)((uint8_t *)(cursor->curr + 1)))
                        return 1;
                }
                else
                {
                    /*
                            dummy node. check if element matches condition
                            dummy node are never deleted we can save it as 
                            a safe place to restart iteration if ever need.
                        */
                    head = (SList * volatile *)&(cursor->curr->link);
                }
            }

            cursor->prev = &(cursor->curr->link);
            pin(pins, 2, cursor->curr); // prev node
        }
        else
        {
            /*
                    we found a deleted node - be nice, help the other thread
                    and remove this deleted node
                */
            if (atomic_casptr((void *volatile *)cursor->prev,
                              (void **)&cursor->curr,
                              cursor->next))
            {
                pins_free(pins, cursor->curr);
            }
            else
            {
                std::this_thread::yield();
                goto retry;
            }
        }
        cursor->curr = cursor->next;
        pin(pins, 1, cursor->curr);
    }
}

/*
        insert a 'node' in the list that starts from 'head'
        in the correct position (as found by lfind)

        return
            0 - inserted
            not 0 - a pointer to a duplicate (not pinned and thus unusable)
        
        note
            it uses pins[0..2], on return all pins are removed.
            if there're nodes with the same key value, a new node is
            added before them.
    */
static SList *linsert(SList *volatile *head,
                      SList *node,
                      Pins *pins,
                      uint32_t flags)
{
    Cursor cursor;
    int res;

    for (;;)
    {
        if (lfind(head, node->hashnr, node->key,
                  node->keylen, &cursor, pins) &&
            (flags && LF_HASH_UNIQUE))
        {
            res = 0; // duplicate found
            break;
        }

        node->link = (intptr_t)cursor.curr;
        assert(node->link != (intptr_t)node); /* no circular reference */
        assert(cursor.prev != &node->link);   /* no circular reference */
        if (atomic_casptr((void *volatile *)cursor.prev,
                          (void **)&cursor.curr,
                          node))
        {
            res = 1; /* inserted ok */
            break;
        }
    }
    unpin(pins, 0);
    unpin(pins, 1);
    unpin(pins, 2);

    /*
            cursor.curr is not pinned here and the pointer is unreliable,
            the object may disappear anytime. but if it points to a dummy
            node, the pointer is safe, because dummy nodes are never freed
        */
    return res ? 0 : cursor.curr;
}

/*
        deletes a node as identified by hashnr/key/keylen from the list
        that starts from 'head'

        return 
            0 - ok
            1 - not found
        it use pins[0..2], on return all pins are removed.
    */
static int ldelete(SList *volatile *head, uint32_t hashnr,
                   const uint8_t *key, uint32_t keylen, Pins *pins)
{
    Cursor cursor;
    int res;

    for (;;)
    {
        if (!lfind(head, hashnr, key, keylen, &cursor, pins))
        {
            res = 1; /* not found */
            break;
        }
        else
        {
            // mark the node deleted
            if (atomic_casptr((void *volatile *)&(cursor.curr->link),
                              (void **)&cursor.next,
                              (void *)(((intptr_t)cursor.next) | 1)))
            {
                // and remove it from the list
                if (atomic_casptr((void *volatile *)cursor.prev,
                                  (void **)&cursor.curr,
                                  cursor.next))
                {
                    pins_free(pins, cursor.curr);
                }
                else
                {
                    /*
                        somebody already helped us and removed the node ?
                        let's check if we need to help that someone too
                        to ensure the number of 'set deleted flag actions'
                        is equal to the number of 'remvoe from the list' action
                        */
                    lfind(head, hashnr, key, keylen, &cursor, pins);
                }
                res = 0;
                break;
            }
        }
    }
    unpin(pins, 0);
    unpin(pins, 1);
    unpin(pins, 2);
    return res;
}

/*
    searches for a node as identified by hashnr/key/keylen in the list
    that starts from 'head'

    return
        0 - not found
        node - found
    note
        it use pins[0..2], on return the pin[2] keeps the node found
        all other pins are removed.
    */
static SList *lsearch(SList *volatile *head, uint32_t hashnr,
                      const uint8_t *key, uint8_t keylen,
                      Pins *pins)
{
    Cursor cursor;
    int res = lfind(head, hashnr, key, keylen, &cursor, pins);
    if (res)
    {
        pin(pins, 2, cursor.curr);
    }
    unpin(pins, 0);
    unpin(pins, 1);
    return res ? cursor.curr : nullptr;
}

    /*
        Hash
    */

#define MAX_LOAD 1.0 /* average number of elements in a bucket */

static int initialize_bucket(HashMap *hash, SList *volatile *, uint32_t, Pins *);

static uint32_t hash_adapter(const uint8_t *key, size_t keylen)
{
    return slice_hash((const char *)key, keylen);
}

HashMap::HashMap(uint32_t element_size, uint32_t flags,
                 uint32_t key_offset, uint32_t key_length,
                 hash_get_key *get_key_arg,
                 hash_func *hash_function,
                 allocator_func *ctor, allocator_func *dtor,
                 hash_init_func *init)
    : array_(sizeof(SList *)),
      alloc_(sizeof(SList) + element_size, offsetof(SList, key), ctor, dtor),
      get_key_(get_key_arg),
      hash_function_(hash_function ? hash_function : hash_adapter),
      key_offset_(key_offset),
      key_length_(key_length),
      element_size_(element_size),
      flags_(flags),
      size_(1),
      count_(0),
      initialize_(init)
{
    assert(get_key_ ? !key_offset_ && !key_length_ : key_length_);
}

HashMap::HashMap(uint32_t element_size, uint32_t flags,
                 uint32_t key_offset, uint32_t key_length,
                 hash_get_key *get_key)
    : array_(sizeof(SList *)),
      alloc_(sizeof(SList) + element_size, offsetof(SList, key), nullptr, nullptr),
      get_key_(get_key),
      hash_function_(hash_adapter),
      key_offset_(key_offset),
      key_length_(key_length),
      element_size_(element_size),
      flags_(flags),
      size_(1),
      count_(0),
      initialize_(nullptr)
{
    assert(get_key_ ? !key_offset_ && !key_length_ : key_length_);
}

HashMap::~HashMap()
{
    SList *el, **head = (SList **)array_.value(0);

    if (unlikely(!head))
        return;

    el = *head;

    while (el)
    {
        intptr_t next = el->link;
        if (el->hashnr & 1)
            alloc_.direct_free(el); // normal node
        else
            free(el); // dummy node
        el = (SList *)next;
    }
}

/*
    DESCRIPTION
        inserts a new element to a hash. it will have a _copy_ of
        data, not a pointer to it.

    RETURN
        0 - inserted
        1 - didn't (unique key conflict)
        -1 - out of memory

    NOTE
        see linsert() for pin usage notes
    */
int HashMap::insert(Pins *pins, const void *data)
{
    int csize, bucket, hashnr;
    SList *node, *volatile *el;

    node = (SList *)Allocator::alloc(pins);
    if (unlikely(!node))
        return -1;
    if (initialize_)
        (*initialize_)((uint8_t *)(node + 1), (const uint8_t *)data);
    else
        memcpy(node + 1, data, element_size_);
    node->key = hash_key((uint8_t *)(node + 1), &node->keylen);
    hashnr = calc_hash(node->key, node->keylen);
    bucket = hashnr % size_;
    el = (SList **)array_.lvalue(bucket);
    if (unlikely(!el))
        return -1;
    if (*el == NULL && unlikely(initialize_bucket(this, el, bucket, pins)))
        return -1;
    node->hashnr = reverse_bits(hashnr) | 1; /*normal node*/
    if (linsert(el, node, pins, flags_))
    {
        pins_free(pins, node);
        return 1;
    }
    csize = size_;
    if ((atomic_add32(&count_, 1) + 1.0) / csize > MAX_LOAD)
        atomic_cas32(&size_, &csize, csize * 2);
    return 0;
}

/**
     Find hash element corresponding to the key.

    @param pins    Pins for the calling thread which were earlier
                    obtained from this hash using get_pins().
    @param key     Key
    @param keylen  Key length

    @retval A pointer to an element with the given key (if a hash is not unique
            and there're many elements with this key - the "first" matching
            element).
    @retval nullptr      - if nothing is found
    @retval ERRPTR - if OOM

    @note Uses pins[0..2]. On return pins[0..1] are removed and pins[2]
            is used to pin object found. It is also not removed in case when
            object is not found/error occurs but pin value is undefined in
            this case.
            So calling unpin() is mandatory after call to this function
            in case of both success and failure.
            @sa lsearch().
    */
void *HashMap::search(Pins *pins, const void *key, uint32_t keylen)
{
    SList *volatile *el, *found;
    uint32_t bucket, hashnr = calc_hash((const uint8_t *)key, keylen);

    bucket = hashnr % size_;
    el = (SList **)(array_.lvalue(bucket));
    if (unlikely(!el))
        return ERRPTR;
    if (*el == nullptr && unlikely(initialize_bucket(this, el, bucket, pins)))
        return ERRPTR;
    found = lsearch(el, reverse_bits(hashnr) | 1,
                    (uint8_t *)key, keylen, pins);
    return found ? found + 1 : nullptr;
}

/*
    DESCRIPTION
        remove an element with the given key from the hash (if a hash is
        not unique and there're many elements with this key - the "first"
        matching element is deleted)
    RETURN
        0 - deleted
        1 - didn't (not found)
        -1 - out of memory
    NOTE
        see ldelete() for pin usage notes
    */
int HashMap::remove(Pins *pins, const void *key, uint32_t keylen)
{
    SList *volatile *el;
    uint32_t bucket, hashnr = calc_hash((const uint8_t *)key, keylen);

    bucket = hashnr % size_;
    el = (SList **)array_.lvalue(bucket);
    if (unlikely(!el))
        return -1;

    /*
            note that we still need to initialize_bucket here,
            we cannot return "node not found", because an old bucket of that
            node may've been split and the node was assigned to a new bucket
            that was never accessed before and thus is not initialized.
        */
    if (*el == nullptr && unlikely(initialize_bucket(this, el, bucket, pins)))
        return -1;
    if (ldelete(el, reverse_bits(hashnr) | 1,
                (uint8_t *)key, keylen, pins))
        return 1;
    atomic_add32(&count_, -1);
    return 0;
}

/**
     Find random hash element which satisfies condition specified by
    match function.

    @param pin       Pins for calling thread to be used during search
                    and for pinning its result.
    @param match     Pointer to match function. This function takes
                    pointer to object stored in hash as parameter
                    and returns 0 if object doesn't satisfy its
                    condition (and non-0 value if it does).
    @param rand_val  Random value to be used for selecting hash
                    bucket from which search in sort-ordered
                    list needs to be started.

    @retval A pointer to a random element matching condition.
    @retval NULL      - if nothing is found
    @retval ERRPTR - OOM.

    @note This function follows the same pinning protocol as search(),
            i.e. uses pins[0..2]. On return pins[0..1] are removed and pins[2]
            is used to pin object found. It is also not removed in case when
            object is not found/error occurs but its value is undefined in
            this case.
            So calling search_unpin() is mandatory after call to this function
            in case of both success and failure.
    */
void *HashMap::random_match(Pins *pins, hash_match_func *match, uint32_t rand_val)
{
    /*convert random value to valid hash value.*/
    uint32_t hashnr = (rand_val & LF_INT_MAX32);
    uint32_t bucket;
    uint32_t rev_hashnr;
    SList *volatile *el;
    Cursor cursor;
    int res;

    bucket = hashnr % size_;
    rev_hashnr = reverse_bits(hashnr);

    el = (SList **)(array_.lvalue(bucket));
    if (unlikely(!el))
        return ERRPTR;

    /*
            Bucket might be totally empty if it has not been accessed since last
            time size_ has been increased. In this case we initialize it
            by inserting dummy node for this bucket to the correct position
            in split-ordered list. This should help future HashMap.* calls 
            trying to access the same bucket.
        */
    if (*el == nullptr && unlikely(initialize_bucket(this, el, bucket, pins)))
        return ERRPTR;

    /*
            To avoid bias towards the first matching element in the bucket,
            we start looking for elements with inversed hash value greater
            or equal than inversed vlaue of our random hash.
        */
    res = lfind_match(el, rev_hashnr | 1, LF_UINT_MAX32, match, &cursor, pins);
    if (!res && hashnr != 0)
    {
        /*
                We have not found matching element - probably we were too close to
                the tail of our split-ordered list. To avoid bias against elements
                at the head of the list we restart our search from its head. Unless
                we were already searching from it.

                To avoid going through elements at which we have already looked
                twice we stop once we reach element from which we have begun our
                first search.
            */
        el = (SList **)(array_.lvalue(0));
        if (unlikely(!el))
            return ERRPTR;
        res = lfind_match(el, 1, rev_hashnr, match, &cursor, pins);
    }

    if (res)
    {
        pin(pins, 2, cursor.curr);
        unpin(pins, 0);
        unpin(pins, 1);
    }

    return res ? cursor.curr + 1 : nullptr;
}

static const uint8_t *dummy_key = (uint8_t *)"";
/*
    RETURN
        0 - ok
        -1 - out of memory
    */
static int initialize_bucket(HashMap *hash, SList *volatile *node,
                             uint32_t bucket, Pins *pins)
{
    uint32_t parent = clear_highest_bit(bucket);
    SList *dummy = (SList *)malloc(sizeof(SList));
    SList **tmp = nullptr, *cur;
    SList *volatile *el = (SList **)(hash->array_.lvalue(parent));
    if (unlikely(!el || !dummy))
        return -1;
    if (*el == nullptr && bucket &&
        unlikely(initialize_bucket(hash, el, parent, pins)))
        return -1;
    dummy->hashnr = reverse_bits(bucket) | 0; /* dummy node */
    dummy->key = dummy_key;
    dummy->keylen = 0;
    if ((cur = linsert(el, dummy, pins, LF_HASH_UNIQUE)))
    {
        free(dummy);
        dummy = cur;
    }
    atomic_casptr((void *volatile *)node, (void **)&tmp, dummy);

    /*
            note that if the CAS above failed (after linsert() succeeded),
            it would mean that some other thread has executed linsert() for
            the same dummy node, its linsert() failed, it picked up our
            dummy node (in "dummy= cur") and executed the same CAS as above.
            Which means that even if CAS above failed we don't need to retry,
            and we should not free(dummy) - there's no memory leak here
        */
    return 0;
}

} // end namespace
