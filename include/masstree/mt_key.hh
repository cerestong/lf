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

    // 通过单个内存访问指令，获取s的前缀
    // @pre 要求可以安全访问[s- size - 1, s + size)
    static uint64_t make_sloppy(const char *s, int len)
    {
        if (len <= 0)
        {
            return 0;
        }
#if HAVE_UNALIGNED_ACCESS
        if (len >= size)
            return *reinterpret_cast<const uint64_t *>(s);
#if __BYTE_ORDER == __LITTLE_ENDIAN
        return *reinterpret_cast<const uint64_t *>(s - (size - len)) >> (8 * (size - len));
#else
        return *reinterpret_cast<const uint64_t *>(s) & (~uint64_t(0) << (8 * (size - len)));
#endif
#else
        union_type u(0);
        memcpy(u.s, s, std::min(len, size));
        return u.x;
#endif
    }

    static uint64_t make_comparable_sloppy(const char *s, int len)
    {
        return net_to_host_order(make_sloppy(s, len));
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
#if __BYTE_ORDER == __LITTLE_ENDIAN
                return (delta << (8 * (size - len))) == 0;
#else
                return (delta >> (8 * (size - len))) == 0;
#endif
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
    explicit MtKey(uint64_t i_key)
        : ikey0_(i_key),
          len_(i_key ? ikey_size - ctz(i_key) / 8 : 0),
          s_(0), first_(0)
    {
    }
    MtKey(uint64_t i_key, int len)
        : ikey0_(i_key),
          len_(std::min(len, ikey_size)), s_(0), first_(0)
    {
    }
    MtKey(uint64_t i_key, Slice suf)
        : ikey0_(i_key),
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
        return len_ > ikey_size;
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
        ikey0_ = StringSlice::make_comparable_sloppy(s_, len_);
    }

    // unshift 在scan函数中使用，scan_up时MtKey向后退8位
    // ikey0_保持不变，len_置为ikey_size+1
    void unshift()
    {
        lf_precondition(is_shifted());
        s_ -= ikey_size;
        ikey0_ = StringSlice::make_comparable_sloppy(s_, ikey_size);
        len_ = ikey_size + 1;
    }

    /*
    @pre has_suffix()
    */
    void shift_by(int delta)
    {
        s_ += delta;
        len_ -= delta;
        ikey0_ = StringSlice::make_comparable_sloppy(s_, len_);
    }

    bool is_shifted() const
    {
        return first_ != s_;
    }

    // Undo all previous shift() calls
    void unshift_all()
    {
        if (s_ != first_)
        {
            len_ += s_ - first_;
            s_ = first_;
            ikey0_ = StringSlice::make_comparable(s_, len_);
        }
    }

    // shift_clear 在scan中使用， 遍历过程中scan_down时，
    // 希望获取下一层中最小的MtKey: ikey0_ == 0 && len_ ==
    void shift_clear()
    {
        ikey0_ = 0;
        len_ = 0;
        s_ += ikey_size;
    }

    // shift_clear_reverse 在scan中反向遍历时使用，
    // 希望获取下一层中最大的MtKey
    void shift_clear_reverse()
    {
        ikey0_ = ~uint64_t(0);
        len_ = ikey_size + 1;
        s_ += ikey_size;
    }

    int compare(uint64_t i_key, int keylen) const
    {
        int cmp = StringSlice::compare(this->ikey(), i_key);
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

    void assign_store_ikey(uint64_t i_key)
    {
        ikey0_ = i_key;
        *reinterpret_cast<uint64_t *>(const_cast<char *>(s_)) = host_to_net_order(i_key);
    }

    void assign_store_length(int len)
    {
        len_ = len;
    }

    int assign_store_suffix(Slice s)
    {
        memcpy(const_cast<char *>(s_ + ikey_size), s.data(), s.size());
        return ikey_size + s.size();
    }

    Slice full_string() const
    {
        return Slice(first_, s_ + len_ - first_);
    }

    operator Slice() const
    {
        return full_string();
    }

  private:
    uint64_t ikey0_;
    int len_;
    const char *s_;
    const char *first_;
};

} // namespace lf
