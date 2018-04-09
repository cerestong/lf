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

struct LimboGroup
{
  enum
  {
    capacity = (4076 - sizeof(int32_t) * 2 - sizeof(Epoch) - sizeof(LimboGroup *)) / sizeof(uintptr_t)
  };
  uint32_t head_;
  uint32_t tail_;
  Epoch epoch_;
  LimboGroup *next_;
  uintptr_t e_[capacity];

  LimboGroup()
      : head_(0), tail_(0), epoch_(0), next_(nullptr)
  {
  }

  inline Epoch clear_mask(uintptr_t v) const
  {
    return (Epoch)(v & (((uintptr_t)1 << 63) - 1));
  }
  inline uintptr_t set_mask(Epoch e) const
  {
    return ((uintptr_t)e | ((uintptr_t)1 << 63));
  }
  inline bool is_epoch(uintptr_t v)
  {
    return (v >> 63);
  }

  Epoch first_epoch() const
  {
    assert(head_ != tail_);
    return clear_mask(e_[head_]);
  }

  void push_back(void *ptr, Epoch epoch)
  {
    assert(tail_ + 2 <= capacity);
    if (head_ == tail_ || epoch_ != epoch)
    {
      e_[tail_++] = set_mask(epoch);
      epoch_ = epoch;
    }
    e_[tail_++] = (uintptr_t)ptr;
  }

  inline uint32_t clean_until(ThreadInfo &ti, Epoch epoch_bound, uint32_t count);
};

class LimboHandle
{
public:
  LimboHandle *prev_;
  LimboHandle *next_;
  ThreadInfo *ti_;
  Epoch my_epoch_;

public:
  LimboHandle()
      : prev_(this),
        next_(this),
        ti_(nullptr),
        my_epoch_(0)
  {
  }

  ~LimboHandle()
  {}

  void *alloc(size_t size);

  void dealloc(void *p);
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

public:
  ThreadInfo();
  ~ThreadInfo();

  LimboHandle *new_handle();
  void delete_handle(LimboHandle *handle);

  void *alloc(size_t size)
  {
    return calloc(1, size);
  }
  void dealloc(void *p);

private:
  void link(LimboHandle *prev, LimboHandle *cur, LimboHandle *next);
  void refill_group();
  void hard_free();
};


extern std::vector<ThreadInfo> *g_all_threads;

inline Epoch min_active_epoch()
{
  Epoch ae = 1UL << 63;
  assert(g_all_threads);
  for (size_t i = 0; i < g_all_threads->size(); i++)
  {
    ThreadInfo &ti = (*g_all_threads)[i];
    if (ti.min_epoch_ && (ti.min_epoch_ < ae))
    {
      ae = ti.min_epoch_;
    }
  }
  return ae;
}

} // end namespace
