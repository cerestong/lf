#include "lf/limbo.hh"
#include "lf/logger.hh"
#include <assert.h>

namespace lf
{

Limbo::Limbo()
    : latest_epoch_(0),
      first_active_epoch_(1),
      head_group_(nullptr),
      tail_group_(nullptr),
      head_handle_(nullptr),
      tail_handle_(nullptr)
{
}

Limbo::~Limbo()
{
    std::lock_guard<std::mutex> lck(mutex_);
    assert(head_handle_ == nullptr);

    for (LimboGroup *next = (head_group_ ? head_group_->next : nullptr);
         head_group_ != nullptr;
         head_group_ = next, next = (next == nullptr ? nullptr : next->next))
    {
        for (int32_t i = 0; i < head_group_->tail; i++)
        {
            free(head_group_->e[i]);
        }
        free(head_group_);
    }
    tail_group_ = nullptr;
}

LimboHandle *Limbo::new_handle()
{
    std::lock_guard<std::mutex> lck(mutex_);
    int64_t my_epoch = ++latest_epoch_;

    LimboHandle *handle = new LimboHandle(tail_handle_, nullptr, this, my_epoch);

    if (tail_handle_)
    {
        tail_handle_->next_ = handle;
    }

    tail_handle_ = handle;

    if (head_handle_ == nullptr)
    {
        head_handle_ = handle;
    }

    return handle;
}

void Limbo::release_handle(LimboHandle *handle)
{
    std::lock_guard<std::mutex> lck(mutex_);

    if (handle->head_group_)
    {
        assert(handle->tail_group_);
        // set epoch
        for (LimboGroup *g = handle->head_group_; g != nullptr; g = g->next)
        {
            g->epoch = latest_epoch_;
        }
        // limbogroup list
        if (tail_group_)
        {
            tail_group_->next = handle->head_group_;
        }
        else
        {
            assert(head_group_ == nullptr);
            head_group_ = handle->head_group_;
        }
        tail_group_ = handle->tail_group_;

        handle->head_group_ = nullptr;
        handle->tail_group_ = nullptr;
    }

    // handle list
    if (handle->prev_)
    {
        handle->prev_->next_ = handle->next_;
    }
    else
    {
        head_handle_ = handle->next_;
        first_active_epoch_ = (head_handle_ ? head_handle_->my_epoch_ : latest_epoch_ + 1);
    }
    if (handle->next_)
    {
        handle->next_->prev_ = handle->prev_;
    }
    else
    {
        tail_handle_ = handle->prev_;
    }
    handle->prev_ = nullptr;
    handle->next_ = nullptr;
}

void Limbo::try_free_some()
{
    LimboGroup *head = nullptr;
    LimboGroup *tail = nullptr;
    {
        std::lock_guard<std::mutex> lck(mutex_);

        if ((!head_group_) || (head_group_->epoch >= first_active_epoch_))
        {
            return;
        }
        head = head_group_;
        assert(head);

        for (tail = head_group_->next;
             (tail != nullptr) && (tail->epoch < first_active_epoch_);
             tail = tail->next)
        {
        }
        head_group_ = tail;
        if (tail == nullptr)
        {
            tail_group_ = nullptr;
        }
    }

    for (LimboGroup *next = (head ? head->next : nullptr);
         head != tail;
         head = next, next = (next == nullptr ? nullptr : next->next))
    {
        for (int32_t i = 0; i < head->tail; i++)
        {
            free(head->e[i]);
        }
        free(head);
    }
}

void LimboHandle::dealloc(void *p)
{
    if ((tail_group_ == nullptr) ||
        (tail_group_->tail >= tail_group_->capacity))
    {
        // expand
        LimboGroup *ng = (LimboGroup *)alloc(sizeof(LimboGroup));
        if (tail_group_)
        {
            tail_group_->next = ng;
        }
        tail_group_ = ng;
        if (!head_group_)
        {
            head_group_ = tail_group_;
        }
    }
    tail_group_->e[(tail_group_->tail)++] = p;
}

LimboHandle::~LimboHandle()
{
    limbo_->release_handle(this);
    limbo_->try_free_some();
}

} // end namespace
