#pragma once

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "lf/random.hh"

namespace lf
{

template <class Comparator>
class SkipListMCas
{
private:
  struct Node;
  const uint16_t kMaxHeight_;
  const uint16_t kBranching_;
  const uint32_t kScaledInverseBranching_;

  // Immutable after construction
  Comparator const compare_;

  Node *const head_;

  // Modified only by Insert().
  volatile int max_height_; // Height of the entire list

public:
  // Create a new SkipList object that will use "cmp" for comparing keys.
  explicit SkipListMCas(Comparator cmp, int32_t max_height = 12, int32_t branching_factor = 4);

  // 析构非线程安全
  ~SkipListMCas();

  // Allocates a key and a skip-list node, returning a pointer to the key
  // portion of the node.
  char *allocate_key(LimboHandle *limbo_hdl, size_t key_size);

  // 插入先前由allocate_key分配的key, 返回 旧key
  // 调用这负责释放旧值
  const char *update(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                     const char *key);

  // 返回 旧key,调用这负责释放旧值
  const char *remove(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                     const char *key);

  // 只在Insert返回-1，调用FreeKey
  void free_key(LimboHandle *hdl, const char *key);

  // Returns true iff an entry that compares equal to key is in the list.
  bool Contains(const char *key) const;

  // Returns true iff table is empty
  bool Empty() const;

  // Return estimated number of entries smaller than `key`.
  uint64_t EstimateCount(const char *key) const;

  // Iteration over the contents of a skip list
  class Iterator
  {
  public:
    // Initialize an iterator over the specified list.
    // The returned iterator is not valid.
    explicit Iterator(const SkipList *list);

    // Change the underlying skiplist used for this iterator
    // This enables us not changing the iterator without deallocating
    // an old one and then allocating a new one
    void SetList(const SkipList *list);

    // Returns true iff the iterator is positioned at a valid node.
    bool Valid() const;

    // Returns the key at the current position.
    // REQUIRES: Valid()
    const char *Key() const;

    // Advances to the next position.
    // REQUIRES: Valid()
    void Next();

    // Advances to the previous position.
    // REQUIRES: Valid()
    void Prev();

    // Advance to the first entry with a key >= target
    void Seek(const char *target);

    // Position at the first entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    void SeekToFirst();

    // Position at the last entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    void SeekToLast();

  private:
    const SkipList *list_;
    Node *node_;
    // Intentionally copyable
  };

private:
  inline int get_max_height() const
  {
    return max_height_;
  }

  void search(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
              Node **preds, Node **succs);

  Node *AllocateNode(size_t key_size, int height);
  void FreeNode(Node *n, int height);
  int RandomHeight();
  bool Equal(const char *a, const char *b) const { return (compare_(a, b) == 0); }

  // Return true if key is greater than the data stored in "n"
  bool KeyIsAfterNode(const char *key, Node *n) const;

  // Returns the earliest node with a key >= key.
  // Return NULL if there is no such node.
  Node *FindGreaterOrEqual(const char *key) const;

  // Return the latest node with a key < key.
  // Return head_ if there is no such node.
  // Fills prev[level] with pointer to previous node at "level" for every
  // level in [0..max_height_-1], if prev is non-null.
  Node *FindLessThan(const char *key, Node **prev = NULL) const;

  // Return the last node in the list.
  // Return head_ if list is empty.
  Node *FindLast() const;

  // No copying allowed
  SkipList(const SkipList &);
  void operator=(const SkipList &);
};

// Implementation details follow

// The Node data type is more of a pointer into custom-managed memory
// then a traditional c++ struct. The key is stored in the bytes immediately
// after the struct, and the next_ pointers for nodes with height>1 are
// store immediately _before_ the struct. This avoids the need to include
// any pointer or sizing data, which reduces per-node memory overheads.
template <class Comparator>
struct SkipListMCas<Comparator>::Node
{
  Node *next_[1];
  uint32_t flags_;
  int32_t height_;
  char key_[0];
  // Stores the height of the node in the memory location normally used for
  // next_[0]. This is used for passing data from AllocateKey to Insert.

  void mark_deleted()
  {
    atomic_store_relaxed(&flags_, (uint32_t)(flags_ | (uint32_t)1));
  }

  bool is_deleted()
  {
    uint32_t flag = atomic_load_relaxed(&flags);
    return (flag & (uint32_t)1);
  }

  const char *key() const
  {
    return key_;
  }

  // Accessors/mutators for links.  Wrapped in methods so we can
  // add the appropriate barriers as necessary, and perform the
  // necessary addressing trickery for storing links below the
  // Node in memory.
  inline Node *next(int n)
  {
    assert(n >= 0);
    return next_[-n];
  }
  inline void set_next(int n, Node *x)
  {
    assert(n >= 0);
    next_[-n] = x;
  }
  inline Node **next_addr(int n)
  {
    assert(n >= 0);
    return &(next_[-n]);
  }
};

template <class Comparator>
inline SkipList<Comparator>::Iterator::Iterator(const SkipList *list)
{
  SetList(list);
}

template <class Comparator>
inline void SkipList<Comparator>::Iterator::SetList(const SkipList *list)
{
  list_ = list;
  node_ = NULL;
}

template <class Comparator>
inline bool SkipList<Comparator>::Iterator::Valid() const
{
  return node_ != NULL;
}

template <class Comparator>
inline const char *SkipList<Comparator>::Iterator::Key() const
{
  assert(Valid());
  return node_->Key();
}

template <class Comparator>
inline void SkipList<Comparator>::Iterator::Next()
{
  assert(Valid());
  node_ = node_->Next(0);
}

template <class Comparator>
inline void SkipList<Comparator>::Iterator::Prev()
{
  // Instead of using explicit "prev" links, we just search for the
  // last node that falls before key.
  assert(Valid());
  node_ = list_->FindLessThan(node_->Key());
  if (node_ == list_->head_)
  {
    node_ = NULL;
  }
}

template <class Comparator>
inline void SkipList<Comparator>::Iterator::Seek(const char *target)
{
  node_ = list_->FindGreaterOrEqual(target);
}

template <class Comparator>
inline void SkipList<Comparator>::Iterator::SeekToFirst()
{
  node_ = list_->head_->Next(0);
}

template <class Comparator>
inline void SkipList<Comparator>::Iterator::SeekToLast()
{
  node_ = list_->FindLast();
  if (node_ == list_->head_)
  {
    node_ = NULL;
  }
}

template <class Comparator>
int SkipList<Comparator>::random_height()
{
  Random *rnd = Random::get_tls_instance();

  // Increase height with probability 1 in kBranching
  int height = 1;
  while (height < kMaxHeight_ && rnd->next() < kScaledInverseBranching_)
  {
    height++;
  }
  assert(height > 0);
  assert(height <= kMaxHeight_);
  return height;
}

template <class Comparator>
bool SkipListMCas<Comparator>::key_is_after_node(const char *key, Node *n) const
{
  // NULL n is considered infinite
  return (n != nullptr) && (compare_(n->key(), key) < 0);
}

template <class Comparator>
typename SkipList<Comparator>::Node *
SkipList<Comparator>::FindGreaterOrEqual(const char *key) const
{
  // Note: It looks like we could reduce duplication by implementing
  // this function as FindLessThan(key)->Next(0), but we wouldn't be able
  // to exit early on equality and the result wouldn't even be correct.
  // A concurrent insert might occur after FindLessThan(key) but before
  // we get a chance to call Next(0).
  Node *x = head_;
  int level = GetMaxHeight() - 1;
  Node *last_bigger = NULL;
  while (true)
  {
    Node *next = x->Next(level);
    // Make sure the lists are sorted
    assert(x == head_ || next == NULL || KeyIsAfterNode(next->Key(), x));
    // Make sure we haven't overshot during our search
    assert(x == head_ || KeyIsAfterNode(key, x));
    int cmp = (next == NULL || next == last_bigger)
                  ? 1
                  : compare_(next->Key(), key);
    if (cmp == 0 || (cmp > 0 && level == 0))
    {
      return next;
    }
    else if (cmp < 0)
    {
      // Keep searching in this list
      x = next;
    }
    else
    {
      // Switch to next list, reuse compare_() result
      last_bigger = next;
      level--;
    }
  }
}

template <class Comparator>
typename SkipList<Comparator>::Node *
SkipList<Comparator>::FindLessThan(const char *key, Node **prev) const
{
  Node *x = head_;
  int level = GetMaxHeight() - 1;
  // KeyIsAfter(key, last_not_after) is definitely false
  Node *last_not_after = NULL;
  while (true)
  {
    Node *next = x->Next(level);
    assert(x == head_ || next == NULL || KeyIsAfterNode(next->Key(), x));
    assert(x == head_ || KeyIsAfterNode(key, x));
    if (next != last_not_after && KeyIsAfterNode(key, next))
    {
      // Keep searching in this list
      x = next;
    }
    else
    {
      if (prev != NULL)
      {
        prev[level] = x;
      }
      if (level == 0)
      {
        return x;
      }
      else
      {
        // Switch to next list, reuse KeyIsAfterNode() result
        last_not_after = next;
        level--;
      }
    }
  }
}

template <class Comparator>
typename SkipList<Comparator>::Node *SkipList<Comparator>::FindLast() const
{
  Node *x = head_;
  int level = GetMaxHeight() - 1;
  while (true)
  {
    Node *next = x->Next(level);
    if (next == NULL)
    {
      if (level == 0)
      {
        return x;
      }
      else
      {
        // Switch to next list
        level--;
      }
    }
    else
    {
      x = next;
    }
  }
}

template <class Comparator>
uint64_t SkipList<Comparator>::EstimateCount(const char *key) const
{
  uint64_t count = 0;

  Node *x = head_;
  int level = GetMaxHeight() - 1;
  while (true)
  {
    assert(x == head_ || compare_(x->Key(), key) < 0);
    Node *next = x->Next(level);
    if (next == NULL || compare_(next->Key(), key) >= 0)
    {
      if (level == 0)
      {
        return count;
      }
      else
      {
        // Switch to next list
        count *= kBranching_;
        level--;
      }
    }
    else
    {
      x = next;
      count++;
    }
  }
}

template <class Comparator>
SkipListMCas<Comparator>::SkipListMCas(const Comparator cmp,
                                       int32_t max_height,
                                       int32_t branching_factor)
    : kMaxHeight_(max_height),
      kBranching_(branching_factor),
      kScaledInverseBranching_((Random::kMaxNext + 1) / kBranching_),
      compare_(cmp),
      head_(allocate_node(nullptr, 0, max_height)),
      max_height_(1)
{
  assert(max_height > 0 &&
         kMaxHeight_ == static_cast<uint32_t>(max_height));
  assert(branching_factor > 1 &&
         kBranching_ == static_cast<uint32_t>(branching_factor));
  assert(kScaledInverseBranching_ > 0);

  for (int i = 0; i < kMaxHeight_; i++)
  {
    head_->set_next(i, nullptr);
  }
}

template <class Comparator>
SkipListMCas<Comparator>::~SkipListMCas()
{
  Node *cur = head_->next(0);
  while (cur != nullptr)
  {
    free_node(nullptr, cur);
    cur = head_->next(0);
  }
  // free head_
  free_node(head_);
  head_ = nullptr;
}

template <class Comparator>
char *SkipListMCas<Comparator>::allocate_key(LimboHandle *limbo_hdl, size_t key_size)
{
  Node *node = allocate_node(limbo_hdl, key_size, random_height());
  return node ? const_cast<char *>(node->key()) : nullptr;
}

template <class Comparator>
void SkipListMCas<Comparator>::free_key(LimboHandle *hdl, const char *key)
{
  if (key)
  {
    Node *x = reinterpret_cast<Node *>(const_cast<char *>(key)) - 1;
    free_node(hdl, x);
  }
}

template <class Comparator>
typename SkipList<Comparator>::Node *
SkipListMCas<Comparator>::allocate_node(LimboHandle *limbo_hdl, size_t key_size, int height)
{
  size_t prefix = sizeof(Node *) * (height - 1);

  char *raw = nullptr;
  if (likely(limbo_hdl))
  {
    raw = (char *)limbo_hdl->alloc(prefix + sizeof(Node) + key_size);
  }
  else
  {
    raw = (char *)ThreadInfo::direct_alloc(1, prefix + sizeof(Node) + key_size);
  }
  if (unlikely(raw == nullptr))
  {
    return nullptr;
  }

  Node *x = reinterpret_cast<Node *>(raw + prefix);

  x->height_ = height;
  return x;
}

template <class Comparator>
void SkipListMCas<Comparator>::free_node(LimboHandle *hdl, Node *n)
{
  if (n != NULL)
  {
    assert(n->height_ >= 1 && n->height_ <= kMaxHeight_);
    size_t prefix = sizeof(Node *) * (n->height_ - 1);
    char *raw = ((char *)n) - prefix;
    if (hdl)
    {
      hdl->dealloc(raw);
    }
    else
    {
      ThreadInfo::direct_free(raw);
    }
  }
}

template <class Comparator>
void SkipListMCas<Comparator>::search(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                                      Node **preds, Node **succs, const char *key)
{
  Node *x = l->head_;
  for (uint16_t i = kMaxHeight_ - 1; i >= 0; i--)
  {
    Node *y = nullptr;
    while (true)
    {
      assert(x != nullptr);
      y = (Node *)mcas_read(thd_ctx, limbo_hdl,
                            (intptr_t *)x->next_addr(i));
      acquire_fence();
      if (!key_is_after_node(key, y))
        break;
      x = y;
    }
    preds[i] = x;
    succs[i] = y;
  }
  return ;
}

template <class Comparator>
const char *SkipListMCas<Comparator>::update(const char *key)
{
  // fast path for sequential insertion
  if (prev_height_ > 0 && !KeyIsAfterNode(key, prev_[0]->Next(0)) &&
      (prev_[0] == head_ || KeyIsAfterNode(key, prev_[0])))
  {
    assert(prev_[0] != head_ || (prev_height_ == 1 && GetMaxHeight() == 1));

    // Outside of this method prev_[1..max_height_] is the predecessor
    // of prev_[0], and prev_height_ refers to prev_[0].  Inside Insert
    // prev_[0..max_height - 1] is the predecessor of key.  Switch from
    // the external state to the internal
    for (int i = 1; i < prev_height_; i++)
    {
      prev_[i] = prev_[0];
    }
  }
  else
  {
    // TODO(opt): we could use a NoBarrier predecessor search as an
    // optimization for architectures where memory_order_acquire needs
    // a synchronization instruction.  Doesn't matter on x86
    FindLessThan(key, prev_);
  }

  if (prev_[0]->Next(0) && Equal(key, prev_[0]->Next(0)->Key()))
  {
    prev_height_ = 0;
    return -1;
  }

  // Our data structure does not allow duplicate insertion
  //assert(prev_[0]->Next(0) == NULL || !Equal(key, prev_[0]->Next(0)->Key()));

  Node *x = reinterpret_cast<Node *>(const_cast<char *>(key)) - 1;
  int height = x->UnstashHeight();
  assert(height >= 1 && height <= kMaxHeight_);

  if (height > GetMaxHeight())
  {
    for (int i = GetMaxHeight(); i < height; i++)
    {
      prev_[i] = head_;
    }
    //fprintf(stderr, "Change height from %d to %d\n", max_height_, height);

    max_height_ = height;
  }

  for (int i = 0; i < height; i++)
  {
    // we publish a pointer to "x" in prev[i].
    x->SetNext(i, prev_[i]->Next(i));
    prev_[i]->SetNext(i, x);
  }
  prev_[0] = x;
  prev_height_ = height;
  return 0;
}

template <class Comparator>
bool SkipList<Comparator>::Contains(const char *key) const
{
  Node *x = FindGreaterOrEqual(key);
  if (x != NULL && Equal(key, x->Key()))
  {
    return true;
  }
  else
  {
    return false;
  }
}

template <class Comparator>
void SkipList<Comparator>::Remove(const char *key)
{
  FindLessThan(key, prev_);
  Node *x = prev_[0]->Next(0);
  if (x != NULL && Equal(key, x->Key()))
  {
    // find
    int height = GetMaxHeight();
    int i = 0;
    for (; i < height; i++)
    {
      if (prev_[i]->Next(i) == x)
      {
        prev_[i]->SetNext(i, x->Next(i));
      }
      else
      {
        break;
      }
    }
    // i is x next height
    assert(i > 0 && i <= height);
    while ((max_height_ > 1) && (head_->Next(max_height_ - 1) == NULL))
    {
      max_height_--;
    }
    // free memory
    FreeNode(x, i);
  }
  prev_height_ = 0;
}

} // namespace lf
