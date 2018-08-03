#pragma once

#include "masstree/mt_get.hh"
#include "masstree/mt_split.hh"

namespace lf
{
/*
     state_ ： 
        0  没有找到
        1  match找到
        -1 match并且是layer指针（find_locked的中间状态）
        2  正在插入值，但值还没有到位
*/
bool TCursor::find_insert(ThreadInfo *ti)
{
    find_locked(ti);
    original_n_ = n_;
    original_v_ = n_->full_unlocked_version_value();

    // maybe we found it
    if (state_)
        return true;

    // otherwise mark as inserted but not present
    state_ = 2;

    // maybe we need a new layer
    if (kx_.p >= 0)
    {
        // 前缀相等，后缀不同
        return make_new_layer(ti);
    }

    // mark insertion if we are changing modification state
    if (unlikely(n_->modstate_ != Leaf::modstate_insert))
    {
        lf_invariant(n_->modstate_ == Leaf::modstate_remove);
        n_->mark_insert();
        n_->modstate_ = Leaf::modstate_insert;
    }

    // try inserting into this node
    if (n_->size() < n_->width)
    {
        kx_.p = permuter_type(n_->permutation_).back();
        // don't inappropriately reuse position 0, which holds the ikey_bound
        // 非0 位置, 或者是第一个Leaf节点，或者是ka_.ikey()与0位置值相等。
        if (likely(kx_.p != 0) ||
            !n_->prev_ ||
            n_->ikey_bound() == ka_.ikey())
        {
            n_->assign(kx_.p, ka_, ti);
            return false;
        }
    }

    // otherwise must split
    return make_split(ti);
}

bool TCursor::make_new_layer(ThreadInfo *ti)
{
    MtKey oka(n_->ksuf(kx_.p));
    ka_.shift();
    int kcmp = oka.compare(ka_);

    // Create a twig of nodes until the suffixs diverge
    Leaf *twig_head = n_;
    Leaf *twig_tail = n_;
    while (kcmp == 0)
    {
        Leaf *nl = Leaf::make_root(0, twig_tail, ti);
        nl->assign_initialize_for_layer(0, oka);
        if (twig_head != n_)
        {
            twig_tail->lv_[0] = nl;
        }
        else
        {
            twig_head = nl;
        }
        nl->permutation_ = permuter_type::make_sorted(1);
        twig_tail = nl;
        new_nodes_.emplace_back(nl, nl->full_unlocked_version_value());
        oka.shift();
        ka_.shift();
        kcmp = oka.compare(ka_);
    }

    // Estimate how much space will be required for keysuffixes
    size_t ksufsize;
    if (ka_.has_suffix() || oka.has_suffix())
    {
        ksufsize = (std::max(0, ka_.suffix_length()) + std::max(0, oka.suffix_length())) * (n_->width / 2) + n_->iksuf_[0].overhead(n_->width);
    }
    else
    {
        ksufsize = 0;
    }
    Leaf *nl = Leaf::make_root(ksufsize, twig_tail, ti);
    nl->assign_initialize(0, kcmp < 0 ? oka : ka_, ti);
    nl->assign_initialize(1, kcmp < 0 ? ka_ : oka, ti);
    nl->lv_[kcmp > 0] = n_->lv_[kx_.p];
    nl->lock(*nl);
    if (kcmp < 0)
    {
        nl->permutation_ = permuter_type::make_sorted(1);
    }
    else
    {
        permuter_type permnl = permuter_type::make_sorted(2);
        permnl.remove_to_back(0);
        nl->permutation_ = permnl.value();
    }
    //
    n_->mark_insert();
    compiler_barrier();

    if (twig_tail != n_)
    {
        twig_tail->lv_[0] = nl;
    }
    if (twig_head != n_)
    {
        n_->lv_[kx_.p] = twig_head;
    }
    else
    {
        n_->lv_[kx_.p] = nl;
    }
    n_->keylenx_[kx_.p] = n_->layer_keylenx;
    updated_v_ = n_->full_unlocked_version_value();
    n_->unlock();
    n_ = nl;
    kx_.i = kx_.p = kcmp < 0;
    return false;
}

void TCursor::finish_insert()
{
    permuter_type perm(n_->permutation_);
    lf_invariant(perm.back() == kx_.p);
    perm.insert_from_back(kx_.i);
    compiler_barrier();

    n_->permutation_ = perm.value();
}

void TCursor::finish(int state, ThreadInfo *ti)
{
    if (state < 0 && state_ == 1)
    {
        if (finish_remvoe(ti))
        {
            return;
        }
    }
    else if (state > 0 && state_ == 2)
    {
        finish_insert();
    }

    if (n_ == original_n_)
        updated_v_ = n_->full_unlocked_version_value();
    else
        new_nodes_.emplace_back(n_, n_->full_unlocked_version_value());
    n_->unlock();
}

} // namespace lf