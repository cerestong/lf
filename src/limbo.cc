#include "lf/limbo.hh"
#include "lf/logger.hh"
#include <assert.h>

namespace lf
{

volatile Epoch global_epoch = 0;
std::vector<ThreadInfo> *g_all_threads;

inline uint32_t LimboGroup::clean_until(ThreadInfo &ti, Epoch epoch_bound, uint32_t count)
{
    Epoch epoch = 0;
    while (head_ != tail_)
    {
        if (e_[head_].ptr_)
        {
            ti.free_rcu(e_[head_].ptr_, e_[head_].u_.tag);
            --count;
            if (!count)
            {
                e_[head_].ptr_ = nullptr;
                e_[head_].u_.epoch = epoch;
                break;
            }
        }
        else
        {
            epoch = e_[head_].u_.epoch;
            if (epoch_bound < epoch)
            {
                break;
            }
        }
        ++head_;
    }
    if (head_ == tail_)
    {
        head_ = tail_ = 0;
    }
    return count;
}

ThreadInfo::ThreadInfo()
    : index_(0),
      handle_cnt_(0),
      empty_handle_(nullptr),
      group_head_(nullptr),
      group_tail_(nullptr),
      max_epoch_(0)
{
    atomic_store_relaxed(&min_epoch_, 0);
    group_head_ = group_tail_ = new LimboGroup();
    memset(pool_, 0x00, sizeof(pool_));
}

ThreadInfo::~ThreadInfo()
{}

void ThreadInfo::destroy()
{
    // 用户需要确保此时没有活跃的LimboHandle
    assert(limbo_handle_.prev_ == limbo_handle_.next_);
    assert(limbo_handle_.prev_ = &limbo_handle_);
    while (group_head_->head_ != group_head_->tail_)
    {
        hard_free();
    }
    while (empty_handle_)
    {
        LimboHandle *next = empty_handle_->next_;
        delete empty_handle_;
        empty_handle_ = next;
    }
    while (group_head_)
    {
        LimboGroup *next = group_head_->next_;
        delete group_head_;
        group_head_ = next;
    }
    group_tail_ = nullptr;
    for (int i = 0; i < pool_max_nlines; i++)
    {
        void *head = pool_[i];
        while (head)
        {
            direct_free(head);
            head = *reinterpret_cast<void **>(head);
        }
        pool_[i] = nullptr;
    } 
}

LimboHandle *ThreadInfo::new_handle()
{
    LimboHandle *handle = nullptr;
    if (empty_handle_)
    {
        handle = empty_handle_;
        empty_handle_ = empty_handle_->next_;
    }
    else
    {
        handle = new LimboHandle();
        handle_cnt_++;
    }
    handle->ti_ = this;
    handle->my_epoch_ = atomic_add64_relaxed(&global_epoch, 1) + 1;

    link(limbo_handle_.prev_, handle, &limbo_handle_);
    if (limbo_handle_.next_ == handle)
    {
        atomic_store_relaxed(&min_epoch_, handle->my_epoch_);
    }
    max_epoch_ = handle->my_epoch_;

    return handle;
}

void ThreadInfo::delete_handle(LimboHandle *handle)
{
    handle->~LimboHandle();
    Epoch epoch = limbo_handle_.next_->my_epoch_;
    assert(handle != &limbo_handle_);
    handle->prev_->next_ = handle->next_;
    handle->next_->prev_ = handle->prev_;

    handle->prev_ = nullptr;
    handle->next_ = empty_handle_;
    empty_handle_ = handle;
    compiler_barrier();
    if (epoch != limbo_handle_.next_->my_epoch_)
    {
        atomic_store_relaxed(&min_epoch_, limbo_handle_.next_->my_epoch_);
        hard_free();
    }
}

void ThreadInfo::link(LimboHandle *prev, LimboHandle *cur, LimboHandle *next)
{
    prev->next_ = cur;
    cur->prev_ = prev;
    cur->next_ = next;
    next->prev_ = cur;
}

void ThreadInfo::hard_free()
{
    LimboGroup *empty_head = nullptr;
    LimboGroup *empty_tail = nullptr;
    uint32_t count = 1024 * 10;

    Epoch epoch_bound = min_active_epoch() - 1;
    if (group_head_->head_ == group_head_->tail_ ||
        group_head_->first_epoch() > epoch_bound)
    {
        return;
    }
    while (count)
    {
        count = group_head_->clean_until(*this, epoch_bound, count);
        if (group_head_->head_ != group_head_->tail_)
        {
            break;
        }
        if (!empty_head)
        {
            empty_head = group_head_;
        }
        empty_tail = group_head_;
        if (group_head_ == group_tail_)
        {
            group_head_ = group_tail_ = empty_head;
            return;
        }
        group_head_ = group_head_->next_;
    }
    if (empty_head)
    {
        empty_tail->next_ = group_tail_->next_;
        group_tail_->next_ = empty_head;
    }
    return;
}

void ThreadInfo::refill_group()
{
    if (!group_tail_->next_)
    {
        group_tail_->next_ = new LimboGroup();
    }
    group_tail_ = group_tail_->next_;
    assert(group_tail_->head_ == 0 && group_tail_->tail_ == 0);
}

} // namespace lf
