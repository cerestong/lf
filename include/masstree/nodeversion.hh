#pragma once

#include <stdint.h>
#include "lf/compiler.hh"

namespace lf
{

enum
{ 
    lock_bit = (1ULL << 8),
    inserting_shift = 9,
    inserting_bit = (1ULL << 9),
    splitting_bit = (1ULL << 10),
    dirty_mask = inserting_bit | splitting_bit,
    vinsert_lowbit = (1ULL << 11), // == inserting_bit << 2
    vsplit_lowbit = (1ULL << 27),
    unused1_bit = (1ULL << 60),
    deleted_bit = (1ULL << 61),
    root_bit = (1ULL << 62),
    isleaf_bit = (1ULL << 63),
    split_unlock_mask = ~(root_bit | unused1_bit | (vsplit_lowbit - 1)),
    unlock_mask = ~(unused1_bit | (vinsert_lowbit - 1)),
    top_stable_bits = 4
};

class NodeVersion
{
  public:
    NodeVersion() { v_ = 0; }
    explicit NodeVersion(bool isleaf)
    {
        v_ = isleaf ? isleaf_bit : 0;
    }

    bool isleaf() const
    {
        return v_ & isleaf_bit;
    }

    template <typename SF>
    NodeVersion stable(SF spin_function) const
    {
        //uint64_t x = atomic_load_relaxed(&v_);
        uint64_t x = v_;
        while (x & dirty_mask)
        {
            spin_function();
            //x = atomic_load_relaxed(&v_);
            x = v_;
        }
        acquire_fence();
        return x;
    }

    NodeVersion stable() const
    {
        return stable(spin_hint_function());
    }

    bool locked() const
    {
        return v_ & lock_bit;
    }

    bool inserting() const 
    {
        return v_ & inserting_bit;
    }

    bool splitting() const
    {
        return v_ & splitting_bit;
    }

    bool deleted() const
    {
        return v_ & deleted_bit;
    }

    bool has_changed(NodeVersion x) const
    {
        compiler_barrier();
        return (x.v_ ^ v_) > lock_bit;
    }

    bool is_root() const
    {
        return v_ & root_bit;
    }

    bool has_split(NodeVersion x) const
    {
        compiler_barrier();
        return (x.v_ ^ v_) >= vsplit_lowbit;
    }

    bool simple_has_split(NodeVersion x) const
    {
        return (x.v_ ^ v_) >= vsplit_lowbit;
    }

    NodeVersion lock()
    {
        return lock(*this);
    }

    NodeVersion lock(NodeVersion expected)
    {
        return lock(expected, spin_hint_function());
    }

    template <typename SF>
    NodeVersion lock(NodeVersion expected, SF spin_function)
    {
        while (1)
        {
            if (!(expected.v_ & lock_bit)
                && atomic_cas64_relaxed(&v_, &(expected.v_), expected.v_ | lock_bit))
            {
                break;
            }
            spin_function();
            expected.v_ = v_;
        }
        lf_invariant(!(expected.v_ & dirty_mask));
        expected.v_ |= lock_bit;
        acquire_fence();
        lf_invariant(expected.v_ == v_);
        return expected;
    }

    void unlock()
    {
        unlock(*this);
    }

    void unlock(NodeVersion x)
    {
        lf_invariant((compiler_barrier(), x.v_ == v_));
        lf_invariant(x.v_ & lock_bit);
        if (x.v_ & splitting_bit)
        {
            x.v_ = (x.v_ + vsplit_lowbit) & split_unlock_mask;
        }
        else
        {
            x.v_ = (x.v_ + ((x.v_ & inserting_bit) << 2)) & unlock_mask;
        }
        release_fence();
        atomic_store_relaxed(&v_, x.v_);
    }

     void mark_insert()
     {
         lf_invariant(locked());
         v_ |= inserting_bit;
        acquire_fence();
     }

     NodeVersion mark_insert(NodeVersion current_version)
     {
         lf_invariant((compiler_barrier(), v_ == current_version.v_));
         lf_invariant(current_version.v_ & lock_bit);
         v_ = (current_version.v_ |= inserting_bit);
         acquire_fence();
         return current_version;
     }

     void mark_split()
     {
         lf_invariant(locked());
         v_ |= splitting_bit;
         acquire_fence();
     }

    void mark_change(bool is_split)
    {
        lf_invariant(locked());
        v_ |= (is_split + 1) << inserting_shift;
        acquire_fence();
    }

    NodeVersion mark_deleted()
    {
        lf_invariant(locked());
        v_ |= deleted_bit | splitting_bit;
        acquire_fence();
        return *this;
    }

    void mark_deleted_tree()
    {
        lf_invariant(locked() && is_root());
        v_ |= deleted_bit;
        acquire_fence();
    }

    void mark_root()
    {
        v_ |= root_bit;
        acquire_fence();
    }

    void mark_nonroot()
    {
        v_ &= ~root_bit;
        acquire_fence();
    }

    void assign_version(NodeVersion x)
    {
        v_ = x.v_;
    }

    uint64_t version_value() const
    {
        return v_;
    }

    uint64_t unlocked_version_value() const
    {
        return v_ & unlock_mask;
    }

  private:
    uint64_t v_;
    NodeVersion(uint64_t v)
        : v_(v) {}
};
} // namespace lf