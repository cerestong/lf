#pragma once

#include "masstree/mt_struct.hh"
#include <vector>

namespace lf
{
struct GcLayerRcuCallback;

class UnlockedTCursor
{
  public:
    typedef Kpermuter permuter_type;

    UnlockedTCursor(const BasicTable& table, Slice str)
        : n_(nullptr), ka_(str), lv_(LeafValue::make_empty()), root_(table.root()){}
    UnlockedTCursor(BasicTable& table, Slice str)
        : n_(nullptr), ka_(str), lv_(LeafValue::make_empty()), root_(table.fix_root()) {}
    UnlockedTCursor(const BasicTable& table, const char *s, int len)
        : n_(nullptr), ka_(s, len), lv_(LeafValue::make_empty()), root_(table.root()){}
    UnlockedTCursor(BasicTable& table, const char *s, int len)
        : n_(nullptr), ka_(s, len), lv_(LeafValue::make_empty()), root_(table.fix_root()) {}

    bool find_unlocked(ThreadInfo *ti);

    inline LeafValue value() const
    {
        return lv_;
    }

    inline Leaf *node() const
    {
        return n_;
    }

    inline permuter_type permutation() const
    {
        return perm_;
    }

    inline int compare_key(const MtKey& a, int bp) const
    {
        return n_->compare_key(a, bp);
    }
    inline uint64_t full_version_value() const
    {
        return (v_.version_value() << Leaf::permuter_type::size_bits) + perm_.size();
    }
    
  private:
    Leaf *n_;
    MtKey ka_;
    NodeVersion v_;
    permuter_type perm_;
    LeafValue lv_;
    const NodeBase *root_;
};

class TCursor
{
  public:
    typedef Kpermuter permuter_type;
    typedef std::vector<std::pair<Leaf *, uint64_t>> new_nodes_type;

    TCursor(BasicTable &table, Slice str)
        : ka_(str), root_(table.fix_root())
    {
    }
    TCursor(BasicTable &table, const char *s, int len)
        : ka_(s, len), root_(table.fix_root())
    {
    }
    TCursor(BasicTable &table, const unsigned char *s, int len)
        : ka_(reinterpret_cast<const char *>(s), len), root_(table.fix_root())
    {
    }
    TCursor(NodeBase *root, const char *s, int len)
        : ka_(s, len), root_(root)
    {
    }
    TCursor(NodeBase *root, const unsigned char *s, int len)
        : ka_(reinterpret_cast<const char *>(s), len), root_(root)
    {
    }

    inline bool has_value() const
    {
        return kx_.p >= 0;
    }

    inline LeafValue &value() const
    {
        return n_->lv_[kx_.p];
    }

    inline bool is_first_layer() const
    {
        return !ka_.is_shifted();
    }

    inline Leaf *node() const
    {
        return n_;
    }

    inline Leaf *original_node() const
    {
        return original_n_;
    }

    inline uint64_t original_version_value() const
    {
        return original_v_;
    }

    inline uint64_t updated_version_value() const
    {
        return updated_v_;
    }

    inline const new_nodes_type &new_nodes() const
    {
        return new_nodes_;
    }

    inline bool find_locked(ThreadInfo *ti);
    inline bool find_insert(ThreadInfo *ti);

    inline void finish(int answer, ThreadInfo *ti);

    inline uint64_t previous_full_version_value() const;
    inline uint64_t next_full_version_value(int state) const;

  private:
    Leaf *n_;
    MtKey ka_;
    KeyIndexedPosition kx_;
    NodeBase *root_;
    int state_;

    Leaf *original_n_;
    uint64_t original_v_;
    uint64_t updated_v_;
    new_nodes_type new_nodes_;

    inline NodeBase *reset_retry()
    {
        ka_.unshift_all();
        return root_;
    }

    bool make_new_layer(ThreadInfo *ti);
    bool make_split(ThreadInfo *ti);
    inline void finish_insert();
    inline bool finish_remvoe(ThreadInfo *ti);

    static void redirect(InterNode *n, uint64_t ikey,
                         uint64_t replacement, ThreadInfo *ti);
    static bool remove_leaf(Leaf *leaf, NodeBase *root,
                            Slice prefix, ThreadInfo *ti);

    bool gc_layer(ThreadInfo *ti);

    friend struct GcLayerRcuCallback;
};

inline uint64_t TCursor::previous_full_version_value() const
{
    return (n_->unlocked_version_value() << Leaf::permuter_type::size_bits) + n_->size();
}

inline uint64_t TCursor::next_full_version_value(int state) const
{
    NodeVersion v(*n_);
    v.unlock();
    uint64_t result = (v.version_value() << Leaf::permuter_type::size_bits) + n_->size();
    if (state < 0 && (state_ & 1))
        return result - 1;
    else if (state > 0 && state_ == 2)
        return result + 1;
    else
        return result;
}

} // namespace lf
