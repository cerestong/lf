#pragma once

#include <stdint.h>
#include <mutex>
#include "lf/compiler.hh"

namespace lf
{

struct LimboGroup
{
  enum
  {
    capacity = (512 - sizeof(int64_t) - sizeof(void *) - sizeof(int32_t)) / sizeof(void *)
  };
  int64_t epoch;
  LimboGroup *next;
  int32_t tail;
  void *e[capacity];

  // call use memset init
};

class LimboHandle;

class Limbo
{
public:
  int64_t latest_epoch_;
  int64_t first_active_epoch_;

  LimboGroup *head_group_;
  LimboGroup *tail_group_;

  // 按epoch有序的链表
  LimboHandle *head_handle_;
  LimboHandle *tail_handle_;

  mutable std::mutex mutex_;

public:
  Limbo();
  ~Limbo();

  int64_t latest_epoch() const
  {
    return latest_epoch_;
  }
  int64_t first_active_epoch() const
  {
    return first_active_epoch_;
  }

  LimboHandle *new_handle();
  void try_free_some();

  friend class LimboHandle;

private:
  void release_handle(LimboHandle *handle);
};

class LimboHandle
{
public:
  LimboHandle *prev_;
  LimboHandle *next_;
  Limbo *limbo_;

  LimboGroup *head_group_;
  LimboGroup *tail_group_;
  int64_t my_epoch_;

public:
  LimboHandle(LimboHandle *prev, LimboHandle *next, Limbo *limbo, int64_t epoch)
      : prev_(prev),
        next_(next),
        limbo_(limbo),
        head_group_(nullptr),
        tail_group_(nullptr),
        my_epoch_(epoch)
  {
  }

  ~LimboHandle();

  void *alloc(size_t size)
  {
    return calloc(1, size);
  }
  void dealloc(void *p);

};

} // end namespace
