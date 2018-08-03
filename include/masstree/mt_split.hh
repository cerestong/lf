#pragma once

#include "masstree/mt_struct.hh"
#include "masstree/mt_leaflink.hh"

namespace lf
{

// 假设在 ka_i 处插入ka， 返回在i位置的ikey
uint64_t Leaf::ikey_after_insert(const permuter_type &perm, int i,
                                 const MtKey &ka, int ka_i) const
{
    if (i < ka_i)
        return this->ikey0_[perm[i]];
    else if (i == ka_i)
        return ka.ikey();
    else
        return this->ikey0_[perm[i - 1]];
}

/* 将自己split到nr, 在p位置插入ka.
    @pre nr 是个新的空Leaf
    @pre this->locked() && nr->locked()
    @post split_ikey是nr的第一个key
    @retrun split type

    若p == this->size() 并且 *this是本层的最右端节点，
    就认为这是顺序插入， split过程不会移动其他keys。

    返回值 split_type : 
        (0, ka属于*this), 
        (1, ka属于*nr),
        (2, ka属于*nr且是顺序优化，没有其他key移动到nr)
*/
int Leaf::split_into(Leaf *nr, int p, const MtKey &ka,
                     uint64_t &split_ikey, ThreadInfo *ti)
{
    // B+ tree 叶子节点插入
    // 分割 *this, [0, width), 到*this和 nr， 同时在p位置插入 ka （0 <= p <= width）.
    // Let mid = floor(width / 2) + 1.
    // 分割后 *this含[0, mid)， nr 含[mid, width+1).
    lf_precondition(this->locked() && nr->locked());
    lf_precondition(this->size() >= this->width - 1);

    // == this->width or this->width - 1
    int sz = this->size();
    // 假装ka已经插入，计算mid
    int mid = width / 2 + 1;
    if (p == 0 && !this->prev_)
        mid = 1;
    else if (p == sz && !this->next_.ptr)
        mid = sz;

    // never separate keys with the same ikey0.
    permuter_type perml(this->permutation_);
    uint64_t mid_ikey = ikey_after_insert(perml, mid, ka, p);
    if (mid_ikey == ikey_after_insert(perml, mid - 1, ka, p))
    {
        int midl = mid - 2, midr = mid + 1;
        while (true)
        {
            // 最多可能存在2个相同的key
            if (midr <= sz &&
                mid_ikey != ikey_after_insert(perml, midr, ka, p))
            {
                mid = midr;
                break;
            }
            else if (midl >= 0 &&
                     mid_ikey != ikey_after_insert(perml, midl, ka, p))
            {
                mid = midl + 1;
                break;
            }
            --midl, ++midr;
        }
        lf_invariant(mid > 0 && mid <= sz);
    }

    uint64_t pv = perml.value_from(mid - (p < mid));
    for (int x = mid; x <= sz; ++x)
    {
        if (x == p)
            nr->assign_initialize(x - mid, ka, ti);
        else
        {
            nr->assign_initialize(x - mid, this, pv & 15, ti);
            pv >>= 4;
        }
    }
    permuter_type permr = permuter_type::make_sorted(sz + 1 - mid);
    if (p >= mid)
    {
        // ??? 这里为什么需要在permuter中移除插入的key
        // 因为在 finish_insert()中会统一再加到permuter中
        permr.remove_to_back(p - mid);
    }
    nr->permutation_ = permr.value();
    btree_leaflink<Leaf>::link_split(this, nr);

    split_ikey = nr->ikey0_[0];
    return p >= mid ? 1 + (mid == sz) : 0;
}

/*
    B+ 树内部节点的插入操作
    split *this, with items [0, width), into *this + nr,
    同时在 p 位置插入 ka:value (0 <= p <= width).
    midpoint元素的结果存在 split_key.

    let mid = ceil(width /2). 
    p < mid : ka应该插在左侧，但实际还未插入，只是把size置为 mid-1
    p > mid : ka真实的插入右侧
*/
int InterNode::split_into(InterNode *nr, int p,
                          uint64_t ka, NodeBase *value,
                          uint64_t &split_ikey, int split_type)
{
    lf_precondition(this->locked() && nr->locked());

    int mid = (split_type == 2 ? this->width : (this->width + 1) / 2);
    nr->nkeys_ = this->width + 1 - (mid + 1);

    if (p < mid)
    {
        nr->child_[0] = this->child_[mid];
        nr->shift_from(0, this, mid, this->width - mid);
        split_ikey = this->ikey0_[mid - 1];
    }
    else if (p == mid)
    {
        nr->child_[0] = value;
        nr->shift_from(0, this, mid, this->width - mid);
        split_ikey = ka;
    }
    else
    {
        nr->child_[0] = this->child_[mid + 1];
        nr->shift_from(0, this, mid + 1, p - (mid + 1));
        nr->assign(p - (mid + 1), ka, value);
        nr->shift_from(p + 1 - (mid + 1), this, p, this->width - p);
        split_ikey = this->ikey0_[mid];
    }

    for (int i = 0; i <= nr->nkeys_; ++i)
        nr->child_[i]->set_parent(nr);

    this->mark_split();
    if (p < mid)
    {
        this->nkeys_ = mid - 1;
        return p;
    }
    else
    {
        this->nkeys_ = mid;
        return -1;
    }
}

bool TCursor::make_split(ThreadInfo *ti)
{
    // 有两种情况调用这个函数。 1. node满， 2. 我们尝试在0位置插入值。
    // （0位置存储的是ikey_bound, 在此节点的生命期内，ikey_bound不会变，
    // 小于ikey_bound的key永远不会路由到此节点。）
    // 对于情景2 我们可以先尝试从permutation中获取其他空闲位置。
    if (n_->size() < n_->width)
    {
        permuter_type perm(n_->permutation_);
        perm.exchange(perm.size(), n_->width - 1);
        kx_.p = perm.back();
        if (kx_.p != 0)
        {
            n_->permutation_ = perm.value();
            compiler_barrier();
            n_->assign(kx_.p, ka_, ti);
            return false;
        }
    }

    NodeBase *child = Leaf::make(n_->ksuf_used_capacity(), n_->phantom_epoch(), ti);
    child->assign_version(*n_);
    uint64_t xikey[2];
    int split_type = n_->split_into(static_cast<Leaf *>(child),
                                    kx_.i, ka_, xikey[0], ti);
    bool sense = false;
    NodeBase *n = n_;
    uint32_t height = 0;

    while (true)
    {
        lf_invariant(n->locked() && child->locked() && (n->isleaf() || n->splitting()));
        InterNode *next_child = nullptr;
        InterNode *p = n->locked_parent();

        int kp = -1;
        if (n->parent_exists(p))
        {
            kp = InterNode::bound_type::upper(xikey[sense], *p);
            p->mark_insert();
        }

        if (kp < 0 || p->height_ > height + 1)
        {
            InterNode *nn = InterNode::make(height + 1, ti);
            nn->child_[0] = n;
            nn->assign(0, xikey[sense], child);
            nn->nkeys_ = 1;
            if (kp < 0)
            {
                nn->make_layer_root();
            }
            else
            {
                nn->set_parent(p);
                p->child_[kp] = nn;
            }
            compiler_barrier();
            n->set_parent(nn);
        }
        else
        {
            if (p->size() >= p->width)
            {
                next_child = InterNode::make(height + 1, ti);
                next_child->assign_version(*p);
                next_child->mark_nonroot();
                kp = p->split_into(next_child, kp, xikey[sense],
                                   child, xikey[!sense], split_type);
            }
            if (kp >= 0)
            {
                p->shift_up(kp + 1, kp, p->size() - kp);
                p->assign(kp, xikey[sense], child);

                compiler_barrier();
                ++p->nkeys_;
            }
        }

        if (n->isleaf())
        {
            Leaf *nl = static_cast<Leaf *>(n);
            Leaf *nr = static_cast<Leaf *>(child);
            permuter_type perml(nl->permutation_);
            int sz = perml.size();
            perml.set_size(sz - nr->size());
            // removed item, if any, must be perml.size()
            if (sz != nl->width)
            {
                perml.exchange(perml.size(), nl->width - 1);
            }
            nl->mark_split();
            nl->permutation_ = perml.value();
            if (split_type == 0)
            {
                kx_.p = perml.back();
                nl->assign(kx_.p, ka_, ti);
            }
            else
            {
                kx_.i = kx_.p = kx_.i - perml.size();
                n_ = nr;
            }

            // versions/sizes shouldn't change after this
            if (nl != n_)
            {
                assert(nr == n_);
                // we don't add n_ until lp.finish() is called
                // this avoids next_version_value() annoyances
                updated_v_ = nl->full_unlocked_version_value();
            }
            else
            {
                new_nodes_.emplace_back(nr, nr->full_unlocked_version_value());
            }
        }

        if (n != n_)
        {
            n->unlock();
        }

        if (child != n_)
        {
            child->unlock();
        }

        if (next_child)
        {
            n = p;
            child = next_child;
            sense = !sense;
            ++height;
        }
        else if (p)
        {
            p->unlock();
            break;
        }
        else
        {
            break;
        }
    }
    return false;
}

} // namespace lf