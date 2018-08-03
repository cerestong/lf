#pragma once

#include <stdint.h>
#include <mutex>
#include <assert.h>
#include <vector>
#include "lf/compiler.hh"

/*
1. 需要维护两个全局的epoch， latest_epoch, min_active_epoch
latest_epoch 在内存释放时，用于标识释放的时间点
min_active_epoch 是活跃的thread中最小的epoch。
内存在释放点小于min_active_epoch再可以被释放。
2. 固定住工作线程的数量，每个线程对应一个limbo_thread对象。
limbo_thread对象维护自己的active_epoch, limbo_group链表。
3. 因为事务的跨度完全由用户定义，所以不能假定一个线程只能线性的处理一个事务。
真实情况应该是n个工作线程处理m个事务（m>n），单个事务可以在多个线程上线性调度。
所以在线程上维护min_active_epoch变的非常不适合。
4. 基于3的原因，考虑使用lock-free的sorted-list维护所有的事务链表，
但仔细的查看基于pin的sort-list实现，它并没有完全解决ABA问题。
所以在设计上做个妥协，每个事务在生命期内只能绑定在一个线程上执行。
由每个线程自己维护事务sorted链表，并在thread本地维护min_active_epoch.
全局min_active_epoch由遍历每个线程获取，这个值不必是实时的，不需要使用屏障。
*/

namespace lf
{

typedef uint64_t Epoch;
typedef int64_t SignedEpoch;

extern volatile Epoch global_epoch; // global epoch, updated regularly

class ThreadInfo;
class LimboHandle;

enum MemTag : uint16_t {
  MemTagNone = 0x0000,
  MemTagPoolMask = 0xFF,
  MemTagRcuCallback = ((uint16_t)-1)
};

struct LimboGroup
{
  struct Element 
  {
    void *ptr_;
    union {
      MemTag tag;
      Epoch epoch;
    } u_;
  };

  enum
  {
    capacity = (4076 - sizeof(uint32_t) * 2 - sizeof(Epoch) - sizeof(LimboGroup *)) / sizeof(Element)
  };
  uint32_t head_;
  uint32_t tail_;
  Epoch epoch_;
  LimboGroup *next_;
  Element e_[capacity];

  LimboGroup()
      : head_(0), tail_(0), epoch_(0), next_(nullptr)
  {
  }

  Epoch first_epoch() const
  {
    assert(head_ != tail_);
    return e_[head_].u_.epoch;
  }

  void push_back(void *ptr, Epoch epoch, MemTag tag)
  {
    assert(tail_ + 2 <= capacity);
    if (head_ == tail_ || epoch_ != epoch)
    {
      e_[tail_].ptr_ = nullptr;
      e_[tail_].u_.epoch = epoch;
      epoch_ = epoch;
      ++tail_;
    }
    e_[tail_].ptr_ = ptr;
    e_[tail_].u_.tag = tag;
    ++tail_;
  }

  inline uint32_t clean_until(ThreadInfo &ti, Epoch epoch_bound, uint32_t count);
};

class LimboHandle
{
public:
  enum { dealloc_cache_size = 5 };
  LimboHandle *prev_;
  LimboHandle *next_;
  ThreadInfo *ti_;
  Epoch my_epoch_;
  void *ptrbuf_[dealloc_cache_size];
  MemTag ptrtags_[dealloc_cache_size];
  int ptrbuf_size_;

public:
  LimboHandle()
      : prev_(this),
        next_(this),
        ti_(nullptr),
        my_epoch_(0),
        ptrbuf_size_(0)
  {
  }

  inline ~LimboHandle();

  inline void *alloc(size_t size, MemTag tag = MemTagNone);

  inline void dealloc(void *p, MemTag tag = MemTagNone);
};

struct MrcuCallback
{
  virtual ~MrcuCallback(){}
  virtual void operator()(ThreadInfo *ti) = 0;
};

class ThreadInfo
{
public:
  int32_t index_;
  int32_t handle_cnt_;
  LimboHandle limbo_handle_;  // handle list
  LimboHandle *empty_handle_; // empty handle list
  volatile Epoch min_epoch_;

  LimboGroup *group_head_;
  LimboGroup *group_tail_;

  enum { pool_max_nlines = 20 };
  void *pool_[pool_max_nlines];

public:
  ThreadInfo();
  ~ThreadInfo();

  LimboHandle *new_handle();
  void delete_handle(LimboHandle *handle);

  void *alloc(size_t size, MemTag tag = MemTagNone)
  {
    (void)tag;
    return calloc(1, size);
  }
  void dealloc(void *p, MemTag tag = MemTagNone)
  {
    record_rcu(p, tag);
  }
  void dealloc(void **pp, MemTag *ptag, int sz)
  {
    Epoch epoch = atomic_load_relaxed(&global_epoch);
    for (int i = 0; i < sz; i++)
    {
      record_rcu(pp[i], epoch, ptag[i]);
    }
  }

  void register_rcu(MrcuCallback *cb)
  {
    record_rcu(cb, MemTagRcuCallback);
  }

  static void* direct_alloc(size_t size, MemTag tag = MemTagNone)
  {
    (void)tag;
    return calloc(1, size);
  }
  static void direct_free(void *p, MemTag tag = MemTagNone)
  {
    (void) tag;
    if (p) free(p);
  }

private:
  void link(LimboHandle *prev, LimboHandle *cur, LimboHandle *next);
  void refill_group();
  void hard_free();

  void free_rcu(void *p, MemTag tag)
  {
    if ((tag & MemTagPoolMask) == 0)
    {
      if (p) ::free(p);
    }
    else if (tag == MemTagRcuCallback)
    {
      (*static_cast<MrcuCallback*>(p))(this);
    }
    else 
    {
      int nl = tag & MemTagPoolMask;
      *reinterpret_cast<void **>(p) = pool_[nl - 1];
      pool_[nl - 1] = p;
    }
  }

  void record_rcu(void *p, MemTag tag)
  {
    if (!p) return;
    Epoch epoch = atomic_load_relaxed(&global_epoch);
    record_rcu(p, epoch, tag);
  }

  void record_rcu(void *p, Epoch epoch, MemTag tag)
  {
    if (!p) return;
    if (group_tail_->tail_ + 2 > group_tail_->capacity)
    {
      refill_group();
    }
    group_tail_->push_back(p, epoch, tag);
  }

  friend struct LimboGroup;
};


extern std::vector<ThreadInfo> *g_all_threads;

inline Epoch min_active_epoch()
{
  Epoch ae = 1UL << 63;
  assert(g_all_threads);
  for (size_t i = 0; i < g_all_threads->size(); i++)
  {
    ThreadInfo &ti = (*g_all_threads)[i];
    Epoch ti_min_epoch = atomic_load_relaxed(&(ti.min_epoch_));
    if (ti_min_epoch && (ti_min_epoch < ae))
    {
      ae = ti_min_epoch;
    }
  }
  return ae;
}

  inline LimboHandle::~LimboHandle()
  {
    if (ptrbuf_size_ != 0)
    {
      ti_->dealloc(ptrbuf_, ptrtags_, dealloc_cache_size);
      ptrbuf_size_ = 0;      
    }
  }

  inline void *LimboHandle::alloc(size_t size, MemTag tag)
  {
    return ti_->alloc(size, tag);
  }

  inline void LimboHandle::dealloc(void *p, MemTag tag)
  {
    ptrbuf_[ptrbuf_size_] = p;
    ptrtags_[ptrbuf_size_] = tag;
    ptrbuf_size_++;
    if (ptrbuf_size_ == dealloc_cache_size)
    {
      ti_->dealloc(ptrbuf_, ptrtags_, dealloc_cache_size);
      ptrbuf_size_ = 0;
    }
  }
} // end namespace
