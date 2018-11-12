#pragma once

#include "masstree/nodeversion.hh"
#include "masstree/mt_key.hh"
#include "masstree/stringbag.hh"
#include "masstree/mt_ksearch.hh"
#include "masstree/masstree.hh"
#include "lf/limbo.hh"

namespace lf
{

class NodeBase;
class InterNode;
class Leaf;

class NodeBase : public NodeVersion
{
  public:
    static constexpr bool concurrent = true;

    NodeBase(bool bisleaf)
        : NodeVersion(bisleaf)
    {
    }

    inline NodeBase *parent() const;

    inline bool parent_exists(NodeBase *p) const
    {
        return p != nullptr;
    }

    inline bool has_parent() const
    {
        return parent_exists(parent());
    }

    inline InterNode *locked_parent() const;

    inline void set_parent(NodeBase *p);

    inline NodeBase *maybe_parent() const;

    inline void make_layer_root()
    {
        set_parent(nullptr);
        this->mark_root();
    }

    inline Leaf *reach_leaf(const MtKey &ka, NodeVersion &version) const;

    template <typename P>
    void print(FILE *f, const char *prefix, int depth, int kdepth) const;
};

class InterNode : public NodeBase
{
  public:
    static constexpr int width = 15;
    typedef KeyBoundBinary bound_type;

    uint8_t nkeys_;
    uint32_t height_;
    uint64_t ikey0_[width];
    NodeBase *child_[width + 1];
    NodeBase *parent_;

    InterNode(uint32_t height)
        : NodeBase(false), nkeys_(0), height_(height), parent_(nullptr)
    {
    }

    static InterNode *make(uint32_t height, ThreadInfo *ti)
    {
        void *ptr = ti->alloc(sizeof(InterNode));
        InterNode *n = new (ptr) InterNode(height);
        assert(n);
        return n;
    }

    int size() const
    {
        return nkeys_;
    }

    MtKey get_key(int p) const
    {
        return MtKey(ikey0_[p]);
    }

    uint64_t ikey(int p) const
    {
        return ikey0_[p];
    }

    int compare_key(uint64_t a, int bp) const
    {
        return StringSlice::compare(a, ikey(bp));
    }

    int compare_key(const MtKey &a, int bp) const
    {
        return StringSlice::compare(a.ikey(), ikey(bp));
    }

    inline int stable_last_key_compare(const MtKey &k, NodeVersion v) const;

    void deallocate(ThreadInfo *ti)
    {
        ti->dealloc(this);
    }

    template <typename P>
    void print(FILE *f, const char *prefix, int depth, int kdepth) const;

  private:
    void assign(int p, uint64_t i_key, NodeBase *child)
    {
        child->set_parent(this);
        child_[p + 1] = child;
        ikey0_[p] = i_key;
    }

    void shift_from(int p, const InterNode *x, int xp, int n)
    {
        lf_precondition(x != this);
        if (n)
        {
            memcpy(ikey0_ + p, x->ikey0_ + xp, sizeof(ikey0_[0]) * n);
            memcpy(child_ + p + 1, x->child_ + xp + 1, sizeof(child_[0]) * n);
        }
    }

    void shift_up(int p, int xp, int n)
    {
        memmove(ikey0_ + p, ikey0_ + xp, sizeof(ikey0_[0]) * n);
        for (NodeBase **a = child_ + p + n, **b = child_ + xp + n;
             n;
             --a, --b, --n)
        {
            *a = *b;
        }
    }

    void shift_down(int p, int xp, int n)
    {
        memmove(ikey0_ + p, ikey0_ + xp, sizeof(ikey0_[0]) * n);
        for (NodeBase **a = child_ + p + 1, **b = child_ + xp + 1;
             n;
             ++a, ++b, --n)
        {
            *a = *b;
        }
    }

    int split_into(InterNode *nr, int p, uint64_t ka,
                   NodeBase *value, uint64_t& split_ikey, int split_type);

    friend class TCursor;
};

class LeafValue
{
  public:
    LeafValue() { u_.v = 0; }
    LeafValue(uint64_t v)
    {
        u_.v = v;
    }
    LeafValue(NodeBase *n)
    {
        u_.n = n;
    }

    static LeafValue make_empty()
    {
        return LeafValue(uint64_t());
    }

    typedef bool (LeafValue::*unspecified_bool_type)() const;
    operator unspecified_bool_type() const
    {
        return u_.x ? &LeafValue::empty : 0;
    }

    bool empty() const
    {
        return !u_.x;
    }

    uint64_t value() const
    {
        return u_.v;
    }

    uint64_t &value()
    {
        return u_.v;
    }

    uintptr_t pvalue() const
    {
        return u_.x;
    }

    uintptr_t& pvalue()
    {
        return u_.x;
    }

    NodeBase *layer() const
    {
        return reinterpret_cast<NodeBase *>(u_.x);
    }

  private:
    union {
        NodeBase *n;
        uint64_t v;
        uintptr_t x;
    } u_;
};

class Leaf : public NodeBase
{
  public:
    static constexpr int width = 15;
    typedef Kpermuter permuter_type;
    typedef KeyBoundBinary bound_type;
    typedef Stringbag<uint8_t> internal_ksuf_type;
    typedef Stringbag<uint16_t> external_ksuf_type;
    typedef uint64_t phantom_epoch_type;
    static constexpr int ksuf_keylenx = 64;
    static constexpr int layer_keylenx = 128;

    enum
    {
        modstate_insert = 0,
        modstate_remove = 1,
        modstate_deleted_layer = 2
    };

    int8_t extrasize64_;
    uint8_t modstate_;
    uint8_t keylenx_[width];
    Kpermuter::storage_type permutation_;
    uint64_t ikey0_[width];
    LeafValue lv_[width];
    external_ksuf_type *ksuf_;
    union {
        Leaf *ptr;
        uintptr_t x;
    } next_;
    Leaf *prev_;
    NodeBase *parent_;
    phantom_epoch_type phantom_epoch_[1];
    internal_ksuf_type iksuf_[0];

    Leaf(size_t sz, phantom_epoch_type p_phantom_epoch)
        : NodeBase(true),
          modstate_(modstate_insert),
          permutation_(Kpermuter::make_empty()),
          ksuf_(), parent_(), iksuf_{}
    {
        lf_precondition(sz % 64 == 0 && sz / 64 < 128);
        extrasize64_ = (int(sz) >> 6) - ((int(sizeof(*this)) + 63) >> 6);
        if (extrasize64_ > 0)
            new ((void *)&iksuf_[0]) internal_ksuf_type(width, sz - sizeof(*this));
        phantom_epoch_[0] = p_phantom_epoch;
    }

    static Leaf *make(int ksufsize, phantom_epoch_type phantom_epoch, ThreadInfo *ti)
    {
        size_t sz = iceil(sizeof(Leaf) + std::min(ksufsize, 128), 64);
        void *ptr = ti->alloc(sz);
        Leaf *n = new (ptr) Leaf(sz, phantom_epoch);
        assert(n);
        return n;
    }

    static Leaf *make_root(int ksufsize, Leaf *parent, ThreadInfo *ti)
    {
        Leaf *n = make(ksufsize,
                       parent ? parent->phantom_epoch() : phantom_epoch_type(),
                       ti);
        n->next_.ptr = n->prev_ = 0;
        n->make_layer_root();
        return n;
    }

    static size_t min_allocated_size()
    {
        return (sizeof(Leaf) + 63) & ~size_t(63);
    }
    size_t allocated_size() const
    {
        int es = (extrasize64_ >= 0 ? extrasize64_ : -extrasize64_ - 1);
        return (sizeof(*this) + es * 64 + 63) & ~size_t(63);
    }

    phantom_epoch_type phantom_epoch() const
    {
        return phantom_epoch_[0];
    }

    int size() const
    {
        return permuter_type::size(permutation_);
    }

    permuter_type permutation() const
    {
        return permuter_type(permutation_);
    }

    uint64_t full_version_value() const
    {
        static_assert(int(top_stable_bits) >= int(permuter_type::size_bits),
                      "not enough bits to add size to version");
        return (this->version_value() << permuter_type::size_bits) + size();
    }

    uint64_t full_unlocked_version_value() const
    {
        static_assert(int(top_stable_bits) >= int(permuter_type::size_bits),
                      "not enough bits to add size to version");
        NodeVersion v(*this);
        if (v.locked())
            v.unlock();
        return (v.version_value() << permuter_type::size_bits) + size();
    }

    using NodeBase::has_changed;
    bool has_changed(NodeVersion oldv,
                     typename permuter_type::storage_type oldperm) const
    {
        return this->has_changed(oldv) || oldperm != permutation_;
    }

    MtKey get_key(int p) const
    {
        int keylenx = keylenx_[p];
        if (!keylenx_has_ksuf(keylenx))
            return MtKey(ikey0_[p], keylenx);
        else
            return MtKey(ikey0_[p], ksuf(p));
    }

    uint64_t ikey(int p) const
    {
        return ikey0_[p];
    }

    uint64_t ikey_bound() const
    {
        return ikey0_[0];
    }

    int compare_key(const MtKey &a, int bp) const
    {
        return a.compare(ikey(bp), keylenx_[bp]);
    }

    inline int stable_last_key_compare(const MtKey &k, NodeVersion v) const;

    inline Leaf *advance_to_key(const MtKey &k, NodeVersion &version) const;

    static bool keylenx_is_layer(int keylenx)
    {
        return keylenx > 127;
    }

    static bool keylenx_has_ksuf(int keylenx)
    {
        return keylenx == ksuf_keylenx;
    }

    bool is_layer(int p) const
    {
        return keylenx_is_layer(keylenx_[p]);
    }

    bool has_ksuf(int p) const
    {
        return keylenx_has_ksuf(keylenx_[p]);
    }

    Slice ksuf(int p, int keylenx) const
    {
        (void)keylenx;
        lf_precondition(keylenx_has_ksuf(keylenx));
        return ksuf_ ? ksuf_->get(p) : iksuf_[0].get(p);
    }

    Slice ksuf(int p) const
    {
        return ksuf(p, keylenx_[p]);
    }

    bool ksuf_equals(int p, const MtKey &ka) const
    {
        return ksuf_equals(p, ka, keylenx_[p]);
    }

    bool ksuf_equals(int p, const MtKey &ka, int keylen) const
    {
        if (!keylenx_has_ksuf(keylen))
            return true;
        Slice s = ksuf(p, keylen);
        return s.size() == ka.suffix().size() &&
               StringSlice::equals_sloppy(s.data(), ka.suffix().data(), s.size());
    }

    /* Returns 1 if match & not layer, 0 if no match, <0 if match and layer */
    int ksuf_matches(int p, const MtKey &ka) const
    {
        int keylenx = keylenx_[p];
        if (keylenx < ksuf_keylenx)
            return 1;
        if (keylenx == layer_keylenx)
            return -(int)sizeof(uint64_t);
        Slice s = ksuf(p, keylenx);
        return s.size() == ka.suffix().size() &&
               StringSlice::equals_sloppy(s.data(), ka.suffix().data(), s.size());
    }
    int ksuf_compare(int p, const MtKey &ka) const
    {
        int keylenx = keylenx_[p];
        if (!keylenx_has_ksuf(keylenx))
            return 0;
        return ksuf(p, keylenx).compare(ka.suffix());
    }

    size_t ksuf_used_capacity() const
    {
        if (ksuf_)
            return ksuf_->used_capacity();
        else if (extrasize64_ > 0)
            return iksuf_[0].used_capacity();
        else
            return 0;
    }

    size_t ksuf_capacity() const
    {
        if (ksuf_)
            return ksuf_->capacity();
        else if (extrasize64_ > 0)
            return iksuf_[0].capacity();
        else
            return 0;
    }

    bool ksuf_external() const
    {
        return ksuf_;
    }

    Slice ksuf_storage(int p) const
    {
        if (ksuf_)
            return ksuf_->get(p);
        else if (extrasize64_ > 0)
            return iksuf_[0].get(p);
        else
            return Slice();
    }

    bool deleted_layer() const
    {
        return modstate_ == modstate_deleted_layer;
    }

    Leaf *safe_next() const
    {
        return reinterpret_cast<Leaf *>(next_.x & ~(uintptr_t)1);
    }

    void deallocate(ThreadInfo *ti)
    {
        if (ksuf_)
            ti->dealloc(ksuf_);
        if (extrasize64_ != 0)
            iksuf_[0].~Stringbag();
        ti->dealloc(this);
    }

    template <typename P>
    void print(FILE *f, const char *prefix, int depth, int kdepth) const;

  private:
    inline void mark_deleted_layer()
    {
        modstate_ = modstate_deleted_layer;
    }

    inline void assign(int p, const MtKey &ka, ThreadInfo *ti)
    {
        lv_[p] = LeafValue::make_empty();
        ikey0_[p] = ka.ikey();
        if (!ka.has_suffix())
            keylenx_[p] = ka.length();
        else
        {
            keylenx_[p] = ksuf_keylenx;
            assign_ksuf(p, ka.suffix(), false, ti);
        }
    }

    inline void assign_initialize(int p, const MtKey &ka, ThreadInfo *ti)
    {
        lv_[p] = LeafValue::make_empty();
        ikey0_[p] = ka.ikey();
        if (!ka.has_suffix())
            keylenx_[p] = ka.length();
        else
        {
            keylenx_[p] = ksuf_keylenx;
            assign_ksuf(p, ka.suffix(), true, ti);
        }
    }

    inline void assign_initialize(int p, Leaf *x, int xp, ThreadInfo *ti)
    {
        lv_[p] = x->lv_[xp];
        ikey0_[p] = x->ikey0_[xp];
        keylenx_[p] = x->keylenx_[xp];
        if (x->has_ksuf(xp))
            assign_ksuf(p, x->ksuf(xp), true, ti);
    }

    inline void assign_initialize_for_layer(int p, const MtKey &ka)
    {
        assert(ka.has_suffix());
        ikey0_[p] = ka.ikey();
        keylenx_[p] = layer_keylenx;
    }

    void assign_ksuf(int p, Slice s, bool initializing, ThreadInfo *to);

    inline uint64_t ikey_after_insert(const permuter_type &perm, int i,
                                      const MtKey &ka, int ka_i) const;

    int split_into(Leaf *nr, int p, const MtKey &ka, uint64_t& split_ikey,
                   ThreadInfo *ti);
    friend class TCursor;
};

/*
    return this node's parent in locked state.
    @pre this->locked()
    @post this->parent() == result && (!result || result->lockec())
*/
InterNode *NodeBase::locked_parent() const
{
    NodeBase *p;
    lf_precondition(this->locked());
    while (1)
    {
        p = this->parent();
        if (!this->parent_exists(p))
            break;
        NodeVersion pv = p->lock();
        if (p == this->parent())
        {
            lf_invariant(!p->isleaf());
            break;
        }
        p->unlock(pv);
        spin_hint();
    }
    return static_cast<InterNode *>(p);
}

/* Return the result of compare_key(k, last_key in node)
    Returns the comparison until a stable comparison is obtained.
*/
inline int InterNode::stable_last_key_compare(const MtKey &k, NodeVersion v) const
{
    while (1)
    {
        int cmp = compare_key(k, size() - 1);
        if (likely(!this->has_changed(v)))
            return cmp;
        v = stable();
    }
}

inline int Leaf::stable_last_key_compare(const MtKey &k, NodeVersion v) const
{
    while (true)
    {
        Leaf::permuter_type perm(permutation_);
        int p = perm[perm.size() - 1];
        int cmp = compare_key(k, p);
        if (likely(!this->has_changed(v)))
            return cmp;
        v = this->stable();
    }
}

/*
    Return the Leaf at or after *this responsible for ka.
    @pre *this was responsible for ka at version v.

    检�?从版本v以来�?this 是否分割过�?
    若split过，则通过B^link-tree指针，定位与ka相关的Leaf节点，v对应那个Leaf节点�?
    stable版本
*/
inline Leaf *Leaf::advance_to_key(const MtKey &ka, NodeVersion &v) const
{
    const Leaf *n = this;
    NodeVersion oldv = v;
    v = n->stable();
    if (v.has_split(oldv) &&
        n->stable_last_key_compare(ka, v) > 0)
    {
        Leaf *next;
        while (likely(!v.deleted()) &&
               (next = n->safe_next()) &&
               StringSlice::compare(ka.ikey(), next->ikey_bound()) >= 0)
        {
            n = next;
            v = n->stable();
        }
    }
    return const_cast<Leaf *>(n);
}

// 若key的长度非常大�?64K），会触发断言
void Leaf::assign_ksuf(int p, Slice s, bool initializing, ThreadInfo *ti)
{
    if ((ksuf_ && ksuf_->assign(p, s)) ||
        (extrasize64_ > 0 && iksuf_[0].assign(p, s)))
        return;

    external_ksuf_type *oksuf = ksuf_;

    permuter_type perm(permutation_);
    int n = initializing ? p : perm.size();

    size_t csz = 0;
    for (int i = 0; i < n; ++i)
    {
        int mp = initializing ? i : perm[i];
        if (mp != p && has_ksuf(mp))
            csz += ksuf(mp).size();
    }

    size_t sz = iceil_log2(external_ksuf_type::safe_size(width, csz + s.size()));
    if (oksuf)
        sz = std::max(sz, oksuf->capacity());

    void *ptr = ti->alloc(sz);
    external_ksuf_type *nksuf = new (ptr) external_ksuf_type(width, sz);
    for (int i = 0; i < n; ++i)
    {
        int mp = initializing ? i : perm[i];
        if (mp != p && has_ksuf(mp))
        {
            bool ok = nksuf->assign(mp, ksuf(mp));
            assert(ok);
            (void)ok;
        }
    }
    bool ok = nksuf->assign(p, s);
    assert(ok);
    (void)ok;
    compiler_barrier();

    lf_invariant(modstate_ != modstate_remove);

    ksuf_ = nksuf;
    compiler_barrier();

    // now the new ksuf_ installed, mark old dead.
    if (extrasize64_ >= 0)
        extrasize64_ = -extrasize64_ - 1;

    if (oksuf)
        ti->dealloc(oksuf);
}

BasicTable::BasicTable()
    : root_(nullptr)
{}

inline NodeBase *BasicTable::root() const
{
    return root_;
}

inline NodeBase *BasicTable::fix_root()
{
    NodeBase *old_root = root_;
    if (unlikely(!old_root->is_root()))
    {
        NodeBase *new_root = old_root->maybe_parent();
        atomic_casptr((void **)&root_, (void **)&old_root, new_root);
    }
    return old_root;
}

void BasicTable::initialize(ThreadInfo *ti)
{
    lf_precondition(!root_);
    root_ = Leaf::make_root(0, nullptr, ti);
}

inline NodeBase *NodeBase::parent() const
{
    // almost always an internode
    if (this->isleaf())
        return static_cast<const Leaf *>(static_cast<const NodeBase*>(this))->parent_;
    else
        return static_cast<const InterNode *>(this)->parent_;
}

inline NodeBase *NodeBase::maybe_parent() const
{
    NodeBase *x = parent();
    return parent_exists(x) ? x : const_cast<NodeBase *>(this);
}

inline void NodeBase::set_parent(NodeBase *p)
{
    if (this->isleaf())
        static_cast<Leaf *>(this)->parent_ = p;
    else
        static_cast<InterNode *>(this)->parent_ = p;
}

inline Leaf *NodeBase::reach_leaf(const MtKey &ka, NodeVersion &version) const
{
    const NodeBase *n[2];
    NodeVersion v[2];
    bool sense;

// Get a non-stable root.
// Detect staleness by checking where n has ever split.
// The true root has never split.
retry:
    sense = false;
    n[sense] = this;
    while (1)
    {
        v[sense] = n[sense]->stable();
        if (v[sense].is_root())
            break;
        n[sense] = n[sense]->maybe_parent();
    }

    // Loop over internal nodes.
    while (!v[sense].isleaf())
    {
        const InterNode *in = static_cast<const InterNode *>(n[sense]);
        int kp = InterNode::bound_type::upper(ka, *in);
        n[!sense] = in->child_[kp];
        if (!n[!sense])
            goto retry;
        v[!sense] = n[!sense]->stable();

        if (likely(!in->has_changed(v[sense])))
        {
            sense = !sense;
            continue;
        }

        NodeVersion oldv = v[sense];
        v[sense] = in->stable();
        if (oldv.has_split(v[sense]) &&
            in->stable_last_key_compare(ka, v[sense]) > 0)
        {
            // root retry
            goto retry;
        }
        else
        {
            // internode retry
        }
    }

    version = v[sense];
    return const_cast<Leaf *>(static_cast<const Leaf *>(n[sense]));
}

template <typename P>
void NodeBase::print(FILE *f, const char *prefix, int depth, int kdepth) const
{
    if (this->isleaf())
    {
        static_cast<const Leaf *>(this)->print<P>(f, prefix, depth, kdepth);
    }
    else
    {
        static_cast<const InterNode *>(this)->print<P>(f, prefix, depth, kdepth);
    }
}

} // end namespace lf
