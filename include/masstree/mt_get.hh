#pragma once

#include "masstree/mt_tcursor.hh"

namespace lf
{

bool UnlockedTCursor::find_unlocked(ThreadInfo *ti)
{
    int match;
    KeyIndexedPosition kx;
    NodeBase *root = const_cast<NodeBase *>(root_);

retry:
    n_ = root->reach_leaf(ka_, v_);

forward:
    if (v_.deleted())
        goto retry;
    perm_ = n_->permutation();
    kx = Leaf::bound_type::lower(ka_, *this);
    if (kx.p >= 0)
    {
        lv_ = n_->lv_[kx.p];
        match = n_->ksuf_matches(kx.p, ka_);
    }
    else
    {
        match = 0;
    }
    if (n_->has_changed(v_))
    {
        n_ = n_->advance_to_key(ka_, v_);
        goto forward;
    }

    if (match < 0)
    {
        ka_.shift_by(-match);
        root = lv_.layer();
        goto retry;
    }
    else
    {
        return match;
    }
}

bool BasicTable::get(Slice& key, LeafValue& value, ThreadInfo *ti) const
{
    UnlockedTCursor lp(*this, key);
    bool found = lp.find_unlocked(ti);
    if (found)
        value = lp.value();
    return found;
}

bool TCursor::find_locked(ThreadInfo *ti)
{
    NodeBase *root = const_cast<NodeBase *>(root_);
    NodeVersion v;
    permuter_type perm;

retry:
    n_ = root->reach_leaf(ka_, v);

forward:
    if (v.deleted())
        goto retry;
    perm = n_->permutation();
    compiler_barrier();

    kx_ = Leaf::bound_type::lower(ka_, *n_);
    if (kx_.p >= 0)
    {
        LeafValue lv = n_->lv_[kx_.p];
        state_ = n_->ksuf_matches(kx_.p, ka_);
        compiler_barrier();
        if (state_ < 0 && !n_->has_changed(v) && lv.layer()->is_root())
        {
            ka_.shift_by(-state_);
            root = lv.layer();
            goto retry;
        }
    }
    else
    {
        state_ = 0;
    }

    n_->lock(v);
    if (n_->has_changed(v) || n_->permutation() != perm)
    {
        // 检查是为了保证之前获得的状态（kx_等）与n_加锁后处于一致状态
        n_->unlock();
        n_ = n_->advance_to_key(ka_, v);
        goto forward;
    }
    else if (unlikely(state_ < 0))
    {
        // 与上一个比较条件的不同是： lv.layer()->is_root()
        // 注意： 此时n_是locked状态 惰性更新分成layer的root指针
        ka_.shift_by(-state_);
        n_->lv_[kx_.p] = root = n_->lv_[kx_.p].layer()->maybe_parent();
        n_->unlock();
        goto retry;
    }
    else if (unlikely(n_->deleted_layer()))
    {
        ka_.unshift_all();
        root = const_cast<NodeBase *>(root_);
        goto retry;
    }
    return state_;
}

} // namespace lf
