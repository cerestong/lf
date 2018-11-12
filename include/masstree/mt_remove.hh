#pragma once

#include "masstree/mt_tcursor.hh"
#include "masstree/mt_leaflink.hh"

namespace lf
{

struct GcLayerRcuCallback : public MrcuCallback
{
    NodeBase *root_;
    int len_;
    char s_[0];

    GcLayerRcuCallback(NodeBase *root, Slice prefix)
        : root_(root), len_(prefix.size())
    {
        memcpy(s_, prefix.data(), len_);
    }

    size_t size() const
    {
        return len_ + sizeof(*this);
    }

    void operator()(ThreadInfo *ti)
    {
        while (!root_->is_root())
        {
            root_ = root_->maybe_parent();
        }
        if (!root_->deleted())
        {
            TCursor lp(root_, s_, len_);
            bool do_remove = lp.gc_layer(ti);
            if (!do_remove || !lp.finish_remvoe(ti))
            {
                lp.n_->unlock();
            }
            ti->direct_free(this);
        }
    }

    static void make(NodeBase *root, Slice prefix, ThreadInfo *ti)
    {
        size_t sz = prefix.size() + sizeof(GcLayerRcuCallback);
        void *data = ti->alloc(sz);
        GcLayerRcuCallback *cb = new (data) GcLayerRcuCallback(root, prefix);
        ti->register_rcu(cb);
    }
};

bool TCursor::gc_layer(ThreadInfo *ti)
{
    find_locked(ti);
    lf_precondition(!n_->deleted() && !n_->deleted_layer());

    // find_locked might return early if another gc_layer attempt has
    // succeeded at removing multiple tree layers. So check that the whole
    // key has been consumed.
    if (ka_.has_suffix())
    {
        return false;
    }

    kx_.i += has_value();
    if (kx_.i >= n_->size())
    {
        return false;
    }
    permuter_type perm(n_->permutation_);
    kx_.p = perm[kx_.i];
    if (n_->ikey0_[kx_.p] != ka_.ikey() || !n_->is_layer(kx_.p))
        return false;
    
    // remove redundant internode layers
    NodeBase *layer;
    while (true)
    {
        layer = n_->lv_[kx_.p].layer();
        if (!layer->is_root())
        {
            n_->lv_[kx_.p] = layer->maybe_parent();
            continue;
        }

        if (layer->isleaf())
        {
            break;
        }

        InterNode *in = static_cast<InterNode *>(layer);
        if (in->size() > 0)
        {
            return false;
        }
        in->lock(*layer);
        if (!in->is_root() || in->size() > 0)
        {
            goto unlock_layer;
        }

        NodeBase *child = in->child_[0];
        child->make_layer_root();
        n_->lv_[kx_.p] = child;
        in->mark_split();
        in->set_parent(child);
        in->unlock();
        in->deallocate(ti);
    }

    {
        Leaf *lf = static_cast<Leaf *>(layer);
        if (lf->size() > 0)
        {
            return false;
        }
        lf->lock(*lf);
        if (!lf->is_root() || lf->size() > 0)
        {
            goto unlock_layer;
        }

        // child is an empty leaf: kill it
        lf_invariant(!lf->prev_ && !lf->next_.ptr);
        lf_invariant(!lf->deleted());
        lf_invariant(!lf->deleted_layer());
        if (n_->phantom_epoch_[0] < lf->phantom_epoch_[0])
        {
            n_->phantom_epoch_[0] = lf->phantom_epoch_[0];
        }
        lf->mark_deleted_layer();
        lf->unlock();
        lf->deallocate(ti);
        return true;
    }

unlock_layer:
    layer->unlock();
    return false;
}

inline bool TCursor::finish_remvoe(ThreadInfo *ti)
{
    if (n_->modstate_ == Leaf::modstate_insert)
    {
        n_->mark_insert();
        n_->modstate_ = Leaf::modstate_remove;
    }

    permuter_type perm(n_->permutation_);
    perm.remove(kx_.i);
    n_->permutation_ = perm.value();
    if (perm.size())
        return false;
    else
        return remove_leaf(n_, root_, ka_.prefix_string(), ti);
}

bool TCursor::remove_leaf(Leaf *leaf, NodeBase *root,
                          Slice prefix, ThreadInfo *ti)
{
    if (!leaf->prev_)
    {
        // 第一个Leaf节点空，表示整棵树也是空的
        if (!leaf->next_.ptr && !prefix.empty())
            GcLayerRcuCallback::make(root, prefix, ti);
        return false;
    }

    // mark Leaf deleted, RCU-free
    leaf->mark_deleted();
    leaf->deallocate(ti);

    // Ensure node that becomes responsible for
    // our keys has its phantom epoch kept up to data
    while (true)
    {
        Leaf *prev = leaf->prev_;
        uint64_t prev_ts = prev->phantom_epoch();
        while (prev_ts < leaf->phantom_epoch() &&
               !atomic_cas64_relaxed(&prev->phantom_epoch_[0], &prev_ts, leaf->phantom_epoch()))
        {
        }
        compiler_barrier();
        if (prev == leaf->prev_)
            break;
    }

    // Unlink leaf from doubly-linked  leaf list
    btree_leaflink<Leaf>::unlink(leaf);

    // Remove leaf from tree, collapse trivial chains, and rewrite ikey bounds
    uint64_t ikey = leaf->ikey_bound();
    NodeBase *n = leaf;
    NodeBase *replacement = nullptr;

    while (true)
    {
        InterNode *p = n->locked_parent();
        p->mark_insert();
        lf_invariant(!p->deleted());

        // 注意： upper返回的位置可能是p->size()
        int kp = InterNode::bound_type::upper(ikey, *p);
        // Notice: ikey may not equal!
        lf_invariant(kp == 0 || p->ikey0_[kp - 1] <= ikey);
        lf_invariant(p->child_[kp] == n);

        p->child_[kp] = replacement;

        if (replacement)
        {
            replacement->set_parent(p);
        }
        else if (kp > 0)
        {
            p->shift_down(kp - 1, kp, p->nkeys_ - kp);
            --p->nkeys_;
        }

        // !p->child_[0] 与之前的 p->child_[kp] = replacement 对应
        // 当kp == 1时，p->child_[0]是否会等于nullptr???
        if (kp <= 1 && p->nkeys_ > 0 && !p->child_[0])
        {
            // Leaf节点的删除引起的父节点区间变化
            redirect(p, ikey, p->ikey0_[0], ti);
            ikey = p->ikey0_[0];
        }

        n->unlock();

        // (p->nkeys_ <= (p->child_[0] == nullptr)) 单个子节点的中间节点
        if (p->nkeys_ > (p->child_[0] == nullptr) ||
            p->is_root())
        {
            p->unlock();
            return true;
        }

        p->mark_deleted();
        p->deallocate(ti);
        n = p;
        replacement = p->child_[p->nkeys_];
        p->child_[p->nkeys_] = nullptr;
    }
}

/*
    redirect 重定向， 将一部分区间[)与父节点中的前一个区间合并，完成重新定位
*/
void TCursor::redirect(InterNode *n, uint64_t ikey,
                       uint64_t replacement_ikey, ThreadInfo *ti)
{
    int kp = -1;
    do
    {
        InterNode *p = n->locked_parent();
        if (kp >= 0)
            n->unlock();
        n = p;
        kp = InterNode::bound_type::upper(ikey, *n);
        if (kp > 0)
        {
            // Notice: n->ikey0_[kp-1] might not equal ikey
            n->ikey0_[kp - 1] = replacement_ikey;
        }
    } while (kp == 0 || (kp == 1 && !n->child_[0]));
    n->unlock();
}

struct DestroyRcuCallback : public MrcuCallback
{
    NodeBase *root_;
    int count_;
    DestroyRcuCallback(NodeBase *root)
        : root_(root), count_(0)
    {
    }
    void operator()(ThreadInfo *ti);
    static void make(NodeBase *root, Slice prefix, ThreadInfo *ti);

  private:
    static inline NodeBase **link_ptr(NodeBase *n);
    static inline void enqueue(NodeBase *n, NodeBase **&tailp);
};

inline NodeBase **DestroyRcuCallback::link_ptr(NodeBase *n)
{
    if (n->isleaf())
    {
        return &static_cast<Leaf *>(n)->parent_;
    }
    else
    {
        return &static_cast<InterNode *>(n)->parent_;
    }
}

inline void DestroyRcuCallback::enqueue(NodeBase *n, NodeBase **&tailp)
{
    *tailp = n;
    tailp = link_ptr(n);
}

void DestroyRcuCallback::operator()(ThreadInfo *ti)
{
    if (++count_ == 1)
    {
        while (!root_->is_root())
        {
            root_ = root_->maybe_parent();
        }
        root_->lock();
        root_->mark_deleted_tree(); // i.e., deleted but not splitting
        root_->unlock();
        ti->register_rcu(this);
        log("DestroyRcuCallback first called");
        return;
    }

    log("DestroyRcuCallback second called");

    NodeBase *workq;
    NodeBase **tailp = &workq;
    enqueue(root_, tailp);

    while (NodeBase *n = workq)
    {
        NodeBase **linkp = link_ptr(n);
        if (linkp != tailp)
        {
            workq = *linkp;
        }
        else
        {
            workq = nullptr;
            tailp = &workq;
        }

        if (n->isleaf())
        {
            Leaf *l = static_cast<Leaf *>(n);
            Leaf::permuter_type perm = l->permutation();
            for (int i = 0; i != l->size(); i++)
            {
                int p = perm[i];
                if (l->is_layer(p))
                {
                    enqueue(l->lv_[p].layer(), tailp);
                }
            }
            l->deallocate(ti);
        }
        else
        {
            InterNode *in = static_cast<InterNode *>(n);
            for (int i = 0; i != in->size() + 1; ++i)
            {
                if (in->child_[i])
                {
                    enqueue(in->child_[i], tailp);
                }
            }
            in->deallocate(ti);
        }
    }
    ti->dealloc(this);
}

// 如果BasicTable中还有value，则value值并没有被释放 
void BasicTable::destroy(ThreadInfo *ti)
{
    if (root_)
    {
        void *data = ti->alloc(sizeof(DestroyRcuCallback));
        DestroyRcuCallback *cb = new (data) DestroyRcuCallback(root_);
        ti->register_rcu(cb);
        root_ = nullptr;
    }
}

} // namespace lf
