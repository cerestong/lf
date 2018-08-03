#pragma once

#include <algorithm>
#include "lf/pack.hh"
#include "lf/slice.hh"
#include "lf/compiler.hh"

namespace lf
{

struct StringSlice
{
  private:
    union union_type {
        uint64_t x;
        char s[sizeof(uint64_t)];
        union_type(uint64_t px)
            : x(px) {}
    };

  public:
    static int size;

    static uint64_t make(const char *s, int len)
    {
        if (len <= 0)
            return 0;
#if HAVE_UNALIGNED_ACCESS
        if (len >= size)
            return *reinterpret_cast<const uint64_t *>(s);
#endif
        union_type u(0);
        memcpy(u.s, s, std::min(len, size));
        return u.x;
    }

    static uint64_t make_comparable(const char *s, int len)
    {
        return net_to_host_order(make(s, len));
    }

    static int unparse_comparable(char *buf, int buflen, uint64_t value, int len)
    {
        union_type u(host_to_net_order(value));
        int l = std::min(std::min(len, size), buflen);
        memcpy(buf, u.s, l);
        return l;
    }

    static bool equals_sloppy(const char *a, const char *b, int len)
    {
#if HAVE_UNALIGNED_ACCESS
        if (len <= size)
        {
            uint64_t delta = *reinterpret_cast<const uint64_t *>(a) ^ *reinterpret_cast<const uint64_t *>(b);
            if (unlikely(len <= 0))
                return true;
            if (lfLittleEndian)
            {
                return (delta << (8 * (size - len))) == 0;
            }
            else
            {
                return (delta >> (8 * (size - len))) == 0;
            }
        }
#endif
        return memcmp(a, b, len) == 0;
    }

    static int compare(uint64_t a, uint64_t b)
    {
        if (a == b)
            return 0;
        else
            return a < b ? -1 : 1;
    }
};

class MtKey
{
  public:
    static int ikey_size;

    MtKey() {}
    MtKey(const char *s, int len)
        : ikey0_(StringSlice::make_comparable(s, len)),
          len_(len), s_(s), first_(s)
    {
    }
    MtKey(Slice s)
        : ikey0_(StringSlice::make_comparable(s.data(), s.size())),
          len_(s.size()), s_(s.data()), first_(s_)
    {
    }
    explicit MtKey(uint64_t ikey)
        : ikey0_(ikey),
          len_(ikey ? ikey_size - ctz(ikey) / 8 : 0),
          s_(0), first_(0)
    {
    }
    MtKey(uint64_t ikey, int len)
        : ikey0_(ikey),
          len_(std::min(len, ikey_size)), s_(0), first_(0)
    {
    }
    MtKey(uint64_t ikey, Slice suf)
        : ikey0_(ikey),
          len_(ikey_size + suf.size()),
          s_(suf.data() - ikey_size),
          first_(s_)
    {
    }

    bool empty() const
    {
        return ikey0_ == 0 && len_ == 0;
    }

    uint64_t ikey() const
    {
        return ikey0_;
    }

    int length() const
    {
        return len_;
    }

    bool has_suffix() const
    {
        return len_ > StringSlice::size;
    }

    Slice suffix() const
    {
        return Slice(s_ + ikey_size, len_ - ikey_size);
    }

    int suffix_length() const
    {
        return len_ - ikey_size;
    }

    /*
    @pre has_suffix()
    */
    void shift()
    {
        s_ += ikey_size;
        len_ -= ikey_size;
        ikey0_ = StringSlice::make_comparable(s_, len_);
    }

    /*
    @pre has_suffix()
    */
    void shift_by(int delta)
    {
        s_ += delta;
        len_ -= delta;
        ikey0_ = StringSlice::make_comparable(s_, len_);
    }

    bool is_shifted() const
    {
        return first_ != s_;
    }

    void unshift_all()
    {
        if (s_ != first_)
        {
            len_ += s_ - first_;
            s_ = first_;
            ikey0_ = StringSlice::make_comparable(s_, len_);
        }
    }

    int compare(uint64_t ikey, int keylen) const
    {
        int cmp = StringSlice::compare(this->ikey(), ikey);
        if (cmp == 0)
        {
            if (len_ > ikey_size)
                cmp = keylen <= ikey_size;
            else
                cmp = len_ - keylen;
        }
        return cmp;
    }

    int compare(const MtKey &x) const
    {
        return compare(x.ikey(), x.length());
    }

    int unparse(char *data, int datalen) const
    {
        int cplen = std::min(len_, datalen);
        StringSlice::unparse_comparable(data, cplen, ikey0_, ikey_size);
        if (cplen > ikey_size)
            memcpy(data + ikey_size, s_ + ikey_size, cplen - ikey_size);
        return cplen;
    }

    Slice unparse(std::string &s) const
    {
        s.resize(len_);
        unparse(&(s[0]), len_);
        return s;
    }

    Slice prefix_string() const
    {
        return Slice(first_, s_-first_);
    }

  private:
    uint64_t ikey0_;
    int len_;
    const char *s_;
    const char *first_;
};

} // namespace lf
