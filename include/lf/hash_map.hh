#pragma once
#include "lf/alloc_pin.hh"

namespace lf
{

#define LF_HASH_UNIQUE 1
#define LF_INT_MAX32 0x7FFFFFFFL
#define LF_UINT_MAX32 0xFFFFFFFFL
#define ERRPTR ((void *)(intptr_t)1)

class HashMap;

typedef uint8_t *(hash_get_key)(const uint8_t *, size_t *, bool);
typedef uint32_t(hash_func)(const uint8_t *, size_t);
typedef void(hash_init_func)(uint8_t *dst, const uint8_t *src);
typedef int(hash_match_func)(const uint8_t *el);

class HashMap
{
  public:
    DynArray array_;  /*hash itself*/
    Allocator alloc_; /*allocator for elements*/
    hash_get_key *get_key_;
    hash_func *hash_function_;
    uint32_t key_offset_;
    uint32_t key_length_;
    uint32_t element_size_;
    uint32_t flags_;
    int32_t volatile size_;  /*size of array*/
    int32_t volatile count_; /*number of elements in the hash*/
    /*
        "Initialzie" hook - called to finish initialization of object provided
        by Allocator (which is pointed by 'dst' parameter) and set element key
        from object passed as parameter to hash_insert (pointed by 'src')
        */
    hash_init_func *initialize_;

  public:
    HashMap(uint32_t element_size, uint32_t flags,
            uint32_t key_offset, uint32_t key_length,
            hash_get_key *get_key,
            hash_func *hash_function,
            allocator_func *ctor, allocator_func *dtor,
            hash_init_func *init);
    HashMap(uint32_t element_size, uint32_t flags,
            uint32_t key_offset, uint32_t key_length,
            hash_get_key *get_key);

    ~HashMap();

    int insert(Pins *pins, const void *data);
    void *search(Pins *pins, const void *key, uint32_t keylen);
    int remove(Pins *pins, const void *key, uint32_t keylen);
    void *random_match(Pins *pins, hash_match_func *match, uint32_t rand_val);

    Pins *get_pins()
    {
        return alloc_.get_pins();
    }

    void put_pins(Pins *pins)
    {
        lf::put_pins(pins);
    }

    void search_unpin(Pins *pins)
    {
        unpin(pins, 2);
    }

    const uint8_t *hash_key(const uint8_t *record, size_t *length)
    {
        if (get_key_)
            return (*get_key_)(record, length, 0);
        *length = key_length_;
        return record + key_offset_;
    }

    uint32_t calc_hash(const uint8_t *key, size_t keylen)
    {
        return (hash_function_(key, keylen) & LF_INT_MAX32);
    }
};

} // end namespace
