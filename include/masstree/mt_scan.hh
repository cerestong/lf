#pragma once

#include "masstree/mt_struct.hh"
#include "masstree/mt_tcursor.hh"

namespace lf
{

class ScanStackElt
{
  public:
    typedef Kpermuter permuter_type;
    typedef typename Leaf::bound_type bound_type;

    Leaf *node() const
    {
        return n_;
    }

    uint64_t full_version_value() const
    {
        return (v_.version_value() << permuter_type::size_bits) + perm_.size();
    }

    int size() const
    {
        return perm_.size();
    }

    permuter_type permutation() const
    {
        return perm_;
    }

    int operator()(const MtKey &k, const ScanStackElt &n, int p)
    {
        return n.n_->compare_key(k, p);
    }

  private:
    NodeBase *root_;
    Leaf *n_;
    NodeVersion v_;
    permuter_type perm_;
    int ki_;
    std::vector<NodeBase *> node_stack_;

    enum
    {
        scan_emit,
        scan_find_next,
        scan_down,
        scan_up,
        scan_retry
    };

    ScanStackElt() {}

    template <typename H>
    int find_initial(H &helper, MtKey &ka, bool emit_equal,
                     LeafValue &entry, ThreadInfo *ti);

    template <typename H>
    int find_retry(H &helper, MtKey &ka, ThreadInfo *ti);

    template <typename H>
    int find_next(H &helper, MtKey &ka, LeafValue &entry);

    int kp() const
    {
        if (unsigned(ki_) < unsigned(perm_.size()))
        {
            return perm_[ki_];
        }
        else
            return -1;
    }

    friend class BasicTable;
};

struct ForwardScanHelper
{
    bool initial_ksuf_match(int ksuf_compare, bool emit_equal) const
    {
        return ksuf_compare > 0 || (ksuf_compare == 0 && emit_equal);
    }

    bool is_duplicate(const MtKey &k,
                      uint64_t ikey, int keylenx) const
    {
        return k.compare(ikey, keylenx) >= 0;
    }

    template <typename N>
    int lower(const MtKey &k, const N *n) const
    {
        return N::bound_type::lower_by(k, *n, *n).i;
    }

    template <typename N>
    KeyIndexedPosition lower_with_position(const MtKey &k, const N *n) const
    {
        return N::bound_type::lower_by(k, *n, *n);
    }

    void found() const
    {
    }

    int next(int ki) const
    {
        return ki + 1;
    }

    Leaf *advance(const Leaf *n, const MtKey &) const
    {
        return n->safe_next();
    }

    template <typename N>
    NodeVersion stable(const N *n, const MtKey &) const
    {
        return n->stable();
    }

    void shift_clear(MtKey &ka) const
    {
        ka.shift_clear();
    }
};

struct ReverseScanHelper
{
    ReverseScanHelper()
        : upper_bound_(false)
    {
    }

    bool initial_ksuf_match(int ksuf_compare, bool emit_equal) const
    {
        return ksuf_compare < 0 || (ksuf_compare == 0 && emit_equal);
    }

    bool is_duplicate(const MtKey &k, uint64_t ikey, int keylenx) const
    {
        return k.compare(ikey, keylenx) <= 0 && !upper_bound_;
    }

    template <typename N>
    int lower(const MtKey &k, const N *n) const
    {
        if (upper_bound_)
            return n->size() - 1;
        KeyIndexedPosition kx = N::bound_type::lower_by(k, *n, *n);
        return kx.i - (kx.p < 0);
    }

    template <typename N>
    KeyIndexedPosition lower_with_position(const MtKey &k, const N *n) const
    {
        KeyIndexedPosition kx = N::bound_type::lower_by(k, *n, *n);
        kx.i -= kx.p < 0;
        return kx;
    }

    int next(int ki) const
    {
        return ki - 1;
    }

    void found() const
    {
        upper_bound_ = false;
    }

    template <typename N>
    N *advance(const N *n, MtKey &k) const
    {
        k.assign_store_ikey(n->ikey_bound());
        k.assign_store_length(0);
        return n->prev_;
    }

    template <typename N>
    NodeVersion stable(N *&n, const MtKey &k) const
    {
        while (true)
        {
            NodeVersion v = n->stable();
            N *next = n->safe_next();
            int cmp;
            if (!next || (cmp == StringSlice::compare(k.ikey(), next->ikey_bound())) < 0 || (cmp == 0 && k.length() == 0))
                return v;
            n = next;
        }
    }

    void shift_clear(MtKey &ka) const
    {
        ka.shift_clear_reverse();
        upper_bound_ = true;
    }

  private:
    mutable bool upper_bound_;
};

template <typename H>
int ScanStackElt::find_initial(H &helper, MtKey &ka, bool emit_equal,
                               LeafValue &entry, ThreadInfo *ti)
{
    KeyIndexedPosition kx;
    int keylenx = 0;
    char suffixbuf[LF_MAXKEYLEN];
    Slice suffix;

retry_root:
    n_ = root_->reach_leaf(ka, v_);

retry_node:
    if (v_.deleted())
        goto retry_root;
    perm_ = n_->permutation();

    kx = helper.lower_with_position(ka, this);
    if (kx.p >= 0)
    {
        keylenx = n_->keylenx_[kx.p];
        compiler_barrier();
        entry = n_->lv_[kx.p];
        if (n_->keylenx_has_ksuf(keylenx))
        {
            suffix = n_->ksuf(kx.p);
            memcpy(suffixbuf, suffix.data(), suffix.size());
            suffix = Slice(suffixbuf, suffix.size());
        }
    }
    if (n_->has_changed(v_))
    {
        n_ = n_->advance_to_key(ka, v_);
        goto retry_node;
    }

    ki_ = kx.i;
    if (kx.p >= 0)
    {
        if (n_->keylenx_is_layer(keylenx))
        {
            node_stack_.push_back(root_);
            node_stack_.push_back(n_);
            root_ = entry.layer();
            return scan_down;
        }
        else if (n_->keylenx_has_ksuf(keylenx))
        {
            int ksuf_compare = suffix.compare(ka.suffix());
            if (helper.initial_ksuf_match(ksuf_compare, emit_equal))
            {
                int keylen = ka.assign_store_suffix(suffix);
                ka.assign_store_length(keylen);
                return scan_emit;
            }
        }
        else if (emit_equal)
        {
            return scan_emit;
        }
        // otherwise, this entry must be skipped
        ki_ = helper.next(ki_);
    }
    return scan_find_next;
}

template <typename H>
int ScanStackElt::find_retry(H &helper, MtKey &ka, ThreadInfo *ti)
{
retry:
    n_ = root_->reach_leaf(ka, v_);
    if (v_.deleted())
    {
        goto retry;
    }

    perm_ = n_->permutation();
    ki_ = helper.lower(ka, this);
    return scan_find_next;
}

template <typename H>
int ScanStackElt::find_next(H &helper, MtKey &ka, LeafValue &entry)
{
    int kp;

    if (v_.deleted())
    {
        return scan_retry;
    }

retry_entry:
    kp = this->kp();
    if (kp >= 0)
    {
        uint64_t ikey = n_->ikey0_[kp];
        int keylenx = n_->keylenx_[kp];
        int keylen = keylenx;

        compiler_barrier();
        entry = n_->lv_[kp];
        if (n_->keylenx_has_ksuf(keylenx))
        {
            keylen = ka.assign_store_suffix(n_->ksuf(kp));
        }

        if (n_->has_changed(v_))
        {
            goto changed;
        }
        else if (helper.is_duplicate(ka, ikey, keylenx))
        {
            ki_ = helper.next(ki_);
            goto retry_entry;
        }

        // we know we can emit the data collected above.
        ka.assign_store_ikey(ikey);
        helper.found();
        if (n_->keylenx_is_layer(keylenx))
        {
            node_stack_.push_back(root_);
            node_stack_.push_back(n_);
            root_ = entry.layer();
            return scan_down;
        }
        else
        {
            ka.assign_store_length(keylen);
            return scan_emit;
        }
    }

    if (!n_->has_changed(v_))
    {
        n_ = helper.advance(n_, ka);
        if (!n_)
        {
            return scan_up;
        }
    }

changed:
    v_ = helper.stable(n_, ka);
    perm_ = n_->permutation();
    ki_ = helper.lower(ka, this);
    return scan_find_next;
}

template <typename H, typename F>
int BasicTable::scan(H helper,
                     Slice firstkey, bool emit_firstkey,
                     F &scanner, ThreadInfo *ti) const
{
    union {
        uint64_t x[(LF_MAXKEYLEN + sizeof(uint64_t) - 1) / sizeof(uint64_t)];
        char s[LF_MAXKEYLEN];
    } keybuf;

    lf_precondition(firstkey.size() <= (int)sizeof(keybuf));
    memcpy(keybuf.s, firstkey.data(), firstkey.size());
    MtKey ka(keybuf.s, firstkey.size());

    ScanStackElt stack;
    stack.root_ = root_;
    LeafValue entry = LeafValue::make_empty();

    int scancount = 0;
    int state;

    while (true)
    {
        state = stack.find_initial(helper, ka, emit_firstkey, entry, ti);
        scanner.visit_leaf(stack, ka, ti);
        if (state != ScanStackElt::scan_down)
            break;
        ka.shift();
    }

    while (true)
    {
        switch (state)
        {
        case ScanStackElt::scan_emit:
        {
            ++scancount;
            if (!scanner.visit_value(ka, entry, ti))
            {
                goto done;
            }
            stack.ki_ = helper.next(stack.ki_);
            state = stack.find_next(helper, ka, entry);
            break;
        }
        case ScanStackElt::scan_find_next:
        {
        find_next:
            state = stack.find_next(helper, ka, entry);
            if (state != ScanStackElt::scan_up)
            {
                scanner.visit_leaf(stack, ka, ti);
            }
            break;
        }
        case ScanStackElt::scan_up:
        {
            do
            {
                if (stack.node_stack_.empty())
                    goto done;
                stack.n_ = static_cast<Leaf *>(stack.node_stack_.back());
                stack.node_stack_.pop_back();
                stack.root_ = stack.node_stack_.back();
                stack.node_stack_.pop_back();
                ka.unshift();
            } while (unlikely(ka.empty()));
            stack.v_ = helper.stable(stack.n_, ka);
            stack.perm_ = stack.n_->permutation();
            stack.ki_ = helper.lower(ka, &stack);
            goto find_next;
        }
        case ScanStackElt::scan_down:
        {
            helper.shift_clear(ka);
            goto retry;
        }
        case ScanStackElt::scan_retry:
        {
        retry:
            state = stack.find_retry(helper, ka, ti);
            break;
        }
        }
    }

done:
    return scancount;
}

template <typename F>
int BasicTable::scan(Slice firstkey, bool emit_firstkey,
                     F &scanner,
                     ThreadInfo *ti) const
{
    return scan(ForwardScanHelper(), firstkey, emit_firstkey, scanner, ti);
}

template <typename F>
int BasicTable::rscan(Slice firstkey, bool emit_firstkey,
                      F &scanner,
                      ThreadInfo *ti) const
{
    return scan(ReverseScanHelper(), firstkey, emit_firstkey, scanner, ti);
}

} // namespace lf
