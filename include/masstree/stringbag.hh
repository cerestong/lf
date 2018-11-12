#pragma once

#include "lf/slice.hh"

namespace lf
{
template <typename T>
class Stringbag
{
  public:
    typedef T offset_type;
    typedef StringSlice slice_type;

  private:
    struct info_type
    {
        offset_type pos;
        offset_type len;
        info_type(unsigned p, unsigned l)
            : pos(p), len(l)
        {
        }
    };

  public:
    // Return the maximum allowed capacity of a stringbag.
    static constexpr unsigned max_size()
    {
        return ((unsigned)(offset_type)-1) + 1;
    }
    static constexpr size_t overhead(int width)
    {
        return sizeof(Stringbag<T>) + width * sizeof(info_type);
    }
    static constexpr size_t safe_size(int width, unsigned len)
    {
        return overhead(width) + len + slice_type::size - 1;
    }

    Stringbag(int width, size_t icapacity)
    {
        size_t firstpos = overhead(width);
        assert(icapacity >= firstpos && icapacity <= max_size());
        size_ = firstpos;
        capacity_ = icapacity - 1;
        memset(info_, 0, sizeof(info_type) * width);
    }

    /* Return the capacity used to construct this bag */
    size_t capacity() const
    {
        return capacity_ + 1;
    }

    /* Return the number of bytes used so far (including overhead)*/
    size_t used_capacity() const
    {
        return size_;
    }

    Slice operator[](int p) const
    {
        info_type info = info_[p];
        return Slice(s_ + info.pos, info.len);
    }

    Slice get(int p) const
    {
        info_type info = info_[p];
        return Slice(s_ + info.pos, info.len);
    }

    bool assign(int p, const char *s, int len)
    {
        unsigned pos, mylen = info_[p].len;
        if (mylen >= (unsigned)len)
            pos = info_[p].pos;
        else if (size_ + (unsigned)std::max(len, slice_type::size) <= capacity())
        {
            pos = size_;
            size_ += len;
        }
        else
            return false;
        memcpy(s_ + pos, s, len);
        info_[p] = info_type(pos, len);
        return true;
    }

    bool assign(int p, Slice s)
    {
        return assign(p, s.data(), s.size());
    }

  private:
    union {
        struct
        {
            offset_type size_;
            offset_type capacity_;
            info_type info_[0];
        };
        char s_[0];
    };
};
} // namespace lf
