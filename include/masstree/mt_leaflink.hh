#pragma once

namespace lf
{

/*
    管理B+树的叶子节点的双向链表
    N : 表示node类型 
*/
template <typename N, bool CONCURRENT = N::concurrent>
struct btree_leaflink
{
};

/*
  并发安全
*/
template <typename N>
struct btree_leaflink<N, true>
{
  private:
    static inline N *mark(N *n)
    {
        return reinterpret_cast<N *>(reinterpret_cast<uintptr_t>(n) + 1);
    }
    static inline bool is_marked(N *n)
    {
        return reinterpret_cast<uintptr_t>(n) & 1;
    }
    template <typename SF>
    static inline N *lock_next(N *n, SF spin_function)
    {
        while (true)
        {
            N *next = n->next_.ptr;
            if (!next ||
                (!is_marked(next) &&
                 atomic_casptr_relaxed((void* volatile*)(&n->next_.ptr), (void **)(&next), mark(next))))
            {
                return next;
            }
            spin_function();
        }
    }
  public:
    static void link_split(N *n, N *nr)
    {
        link_split(n, nr, spin_hint_function());
    }

    template <typename SF>
    static void link_split(N *n, N *nr, SF spin_function)
    {
        nr->prev_ = n;
        N *next = lock_next(n, spin_function);
        nr->next_.ptr = next;
        if (next)
            next->prev_ = nr;

        compiler_barrier();
        n->next_.ptr = nr;
    }
    static void unlink(N *n)
    {
        unlink(n, spin_hint_function());
    }

    template <typename SF>
    static void unlink(N *n, SF spin_function)
    {
        N *next = lock_next(n, spin_function);
        N *prev;
        while (true)
        {
            N *nx = n;
            prev = n->prev_;
            if (atomic_casptr_relaxed((void* volatile*)(&prev->next_.ptr), (void **)(&nx), mark(nx)))
                break;
            spin_function();
        }
        if (next)
            next->prev_ = prev;
        compiler_barrier();
        prev->next_.ptr = next;
    }
};

// 单线程版本
template <typename N>
struct btree_leaflink<N, false>
{
    static void link_split(N *n, N *nr)
    {
        link_split(n, nr, do_nothing());
    }

    template <typename SF>
    static void link_split(N *n, N *nr, SF)
    {
        nr->prev_ = n;
        nr->next_.ptr = n->next_.ptr;
        n->next_.ptr = nr;
        if (nr->next_.ptr)
            nr->next_.ptr->prev_ = nr;
    }

    static void unlink(N *n)
    {
        unlink(n, do_nothing());
    }

    template <typename SF>
    static void unlink(N *n, SF)
    {
        if (n->next_.ptr)
            n->next_.ptr->prev_ = n->prev_;
        n->prev_->next_.ptr = n->next_.ptr;
    }
};

} // namespace lf
