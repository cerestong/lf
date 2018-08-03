#pragma once

#include "masstree/mt_struct.hh"
#include "masstree/mt_key.hh"
#include <stdio.h>
#include <inttypes.h>

namespace lf
{

extern uint64_t initial_timestamp;

class KeyUnparsePrintableString
{
  public:
    static int unparse_key(MtKey key, char *buf, int buflen)
    {
        return key.unparse(buf, buflen);
    }
};

template <typename T>
class ValuePrint
{
  public:
    static void print(T value, FILE *f, const char *prefix,
                      int indent, Slice key, uint64_t initial_timestamp, char *suffix)
    {
        value->print(f, prefix, indent, key, initial_timestamp, suffix);
    }
};

template <>
class ValuePrint<unsigned char *>
{
  public:
    static void print(unsigned char *value, FILE *f, const char *prefix,
                      int indent, Slice key, uint64_t, char *suffix)
    {
        fprintf(f, "%s%*s%.*s = %p%s\n", prefix, indent, "", key.size(), key.data(), value, suffix);
    }
};

template <>
class ValuePrint<uint64_t>
{
  public:
    static void print(uint64_t value, FILE *f, const char *prefix,
                      int indent, Slice key, uint64_t, char *suffix)
    {
        fprintf(f, "%s%*s%.*s = %" PRIu64 "%s\n",
                prefix, indent, "", key.size(), key.data(), value, suffix);
    }
};

template <typename P>
void Leaf::print(FILE *f, const char *prefix, int depth, int kdepth) const
{
    f = f ? f : stderr;
    prefix = prefix ? prefix : "";
    NodeVersion v;
    permuter_type perm;

    do
    {
        v = *this;
        compiler_barrier();
        perm = permutation_;
    } while (has_changed(v));

    int indent = 2 * depth;

    {
        char buf[1024];
        int l = 0;
        if (ksuf_ && extrasize64_ < -1)
            l = snprintf(buf, sizeof(buf), " [ksuf i%dx%d]", -extrasize64_ - 1, (int)ksuf_->capacity() / 64);
        else if (ksuf_)
            l = snprintf(buf, sizeof(buf), " [ksuf x%d]", (int)ksuf_->capacity() / 64);
        else if (extrasize64_)
            l = snprintf(buf, sizeof(buf), " [ksuf i%d]", extrasize64_);

        static const char *const modstates[] = {"", "-", "D"};
        fprintf(f, "%s%*sLeaf %p: %d %s, version %" PRIx64 "%s, permutation %s, parent %p, prev %p, next %p%.*s\n",
                prefix, indent, "", this,
                perm.size(), perm.size() == 1 ? "key" : "keys",
                v.version_value(),
                modstate_ <= 2 ? modstates[modstate_] : "??",
                perm.unparse().c_str(),
                parent_, prev_, next_.ptr, l, buf);
    }

    if (v.deleted() || (perm[0] != 0 && prev_))
    {
        std::string ibound;
        MtKey(ikey_bound()).unparse(ibound);
        fprintf(f, "%s%*s%s = [] #0\n", prefix, indent + 2, "", ibound.c_str());
    }

    char keybuf[256];
    char xbuf[15];
    for (int idx = 0; idx < perm.size(); ++idx)
    {
        int p = perm[idx];
        int l = P::key_unparse_type::unparse_key(this->get_key(p), keybuf, sizeof(keybuf));
        snprintf(xbuf, sizeof(xbuf), " #%x/%d", p, keylenx_[p]);
        LeafValue lv = lv_[p];
        if (this->has_changed(v))
        {
            fprintf(f, "%s%*s[NODE CHANGED]\n", prefix, indent + 2, "");
            break;
        }
        else if (!lv)
        {
            fprintf(f, "%s%*s%.*s = []%s\n", prefix, indent + 2, "", l, keybuf, xbuf);
        }
        else if (is_layer(p))
        {
            fprintf(f, "%s%*s%.*s = SUBTREE%s\n", prefix, indent + 2, "", l, keybuf, xbuf);
            NodeBase *n = lv.layer();
            while (!n->is_root())
            {
                n = n->maybe_parent();
            }
            n->print<P>(f, prefix, depth + 1, int(kdepth + MtKey::ikey_size));
        }
        else
        {
            P::value_print_type::print(lv, f, prefix, indent + 2, Slice(keybuf, l), initial_timestamp, xbuf);
        }
    }

    if (v.deleted())
    {
        fprintf(f, "%s%*s[DELETED]\n", prefix, indent + 2, "");
    }
}

template <typename P>
void InterNode::print(FILE *f, const char *prefix, int depth, int kdepth) const
{
    f = f ? f : stderr;
    prefix = prefix ? prefix : "";
    InterNode copy(*this);
    for (int i = 0; i < 100 && (copy.has_changed(*this) || this->inserting() || this->splitting()); ++i)
    {
        memcpy(&copy, this, sizeof(copy));
    }
    int indent = 2 * depth;

    {
        char buf[1024];
        int l = 0;
        fprintf(f, "%s%*sInterNode %p[%u]%s: %d keys, version %" PRIx64 ", parent %p%.*s\n",
                prefix, indent, "", this,
                height_, this->deleted() ? " [DELETED]" : "",
                copy.size(), (uint64_t)copy.version_value(), copy.parent_,
                l, buf);
    }

    char keybuf[256];
    for (int p = 0; p < copy.size(); ++p)
    {
        if (copy.child_[p])
            copy.child_[p]->print<P>(f, prefix, depth + 1, kdepth);
        else
            fprintf(f, "%s%*s[]\n", prefix, indent, "");
        int l = P::key_unparse_type::unparse_key(copy.get_key(p), keybuf, sizeof(keybuf));
        fprintf(f, "%s%*s%p[%u.%d] %.*s\n",
                prefix, indent, "", this, height_, p, l, keybuf);
    }
    if (copy.child_[copy.size()])
        copy.child_[copy.size()]->print<P>(f, prefix, depth + 1, kdepth);
    else
        fprintf(f, "%s%*s[]\n", prefix, indent, "");
}

template <typename P>
void BasicTable::print(FILE* f) const
{
    root_->print<P>(f ? f : stdout, "", 0, 0);
}



} // namespace lf