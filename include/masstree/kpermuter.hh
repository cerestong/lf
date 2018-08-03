#pragma once

#include <stdint.h>
#include <string>

namespace lf
{

class IdentityKpermuter
{
    int size_;

  public:
    IdentityKpermuter(int size)
        : size_(size) {}
    int size() const
    {
        return size_;
    }
    int operator[](int i) const
    {
        return i;
    }
    bool operator==(const IdentityKpermuter &) const
    {
        return true;
    }
    bool operator!=(const IdentityKpermuter &) const
    {
        return false;
    }
};

class Kpermuter
{
  public:
    typedef uint64_t storage_type;
    typedef uint64_t value_type;
    enum
    {
        initial_value = (uint64_t)0x0123456789ABCDE0ULL,
        full_value = (uint64_t)0xEDCBA98765432100ULL,
        max_width = (int)(sizeof(storage_type) * 2 - 1),
        width = 15,
        size_bits = 4
    };

    Kpermuter() : x_(0) {}
    Kpermuter(value_type x) : x_(x) {}

    /* 返回空的permuter, size 0
       按顺序分配元素 0,1,2... witdh-1
    */
    static inline value_type make_empty()
    {
        value_type p = (value_type)initial_value >> ((max_width - width) << 2);
        return p & ~(value_type)15;
    }

    static inline value_type make_sorted(int n)
    {
        value_type mask =
            (n == width ? (value_type)0 : (value_type)16 << (n << 2)) - 1;
        return (make_empty() << (n << 2)) |
               ((value_type)full_value & mask) |
               n;
    }

    int size() const
    {
        return x_ & 15;
    }

    int operator[](int i) const
    {
        return (x_ >> ((i << 2) + 4)) & 15;
    }

    int back() const
    {
        return (*this)[width - 1];
    }

    value_type value() const
    {
        return x_;
    }

    value_type value_from(int i) const
    {
        return x_ >> ((i + 1) << 2);
    }

    void set_size(int n)
    {
        x_ = (x_ & ~(value_type)15) | n;
    }

    /* Allocate a new element and insert it at position i.
        @pre 0 <= i < width
        @pre size() < width
        @return The newly allocated element.
    */
    int insert_from_back(int i)
    {
        int value = back();
        // increase size, leave lower slots unchanged
        x_ = ((x_ + 1) & (((value_type)16 << (i << 2)) - 1)) |
             // insert slot
             ((value_type)value << ((i << 2) + 4)) |
             ((x_ << 4) & ~(((value_type)256 << (i << 2)) - 1));
        return value;
    }

    /* Insert an unallocated element from position si at posiotion di
        @pre 0 <= di < width
        @pre size() < width && size() <= si
        @return The newly allocated element.
    */
    void insert_selected(int di, int si)
    {
        int value = (*this)[si];
        value_type mask = ((value_type)256 << (si << 2)) - 1;
        x_ = ((x_ + 1) & (((value_type)16 << (di << 2)) - 1)) |
             ((value_type)value << ((di << 2) + 4)) |
             ((x_ << 4) & mask & ~(((value_type)256 << (di << 2)) - 1)) |
             (x_ & ~mask);
    }

    /* remove the element at position i
        @pre 0 <= i < size()
        @pre size() < width

        把 p[i]放到p[p.size()],并长度减一
        q[q.size()] == p[i]
    */
    void remove(int i)
    {
        if (int(x_ & 15) == i + 1)
            --x_;
        else
        {
            int rot_amount = ((x_ & 15) - i - 1) << 2;
            value_type rot_mask =
                (((value_type)16 << rot_amount) - 1) << ((i + 1) << 2);
            x_ = ((x_ - 1) & ~rot_mask) |
                 (((x_ & rot_mask) >> 4) & rot_mask) |
                 (((x_ & rot_mask) << rot_amount) & rot_mask);
        }
    }

    /* Remove the element at position i to the back
        @pre 0 <= i < size()
        @pre size() < width
        把 p[i]放到p.back(), 并长度减一
    */
    void remove_to_back(int i)
    {
        value_type mask = ~(((value_type)16 << (i << 2)) - 1);
        value_type x = x_ & (((value_type)16 << (width << 2)) - 1);
        x_ = ((x - 1) & ~mask) |
             ((x >> 4) & mask) |
             ((x & mask) << ((width - i - 1) << 2));
    }

    /* Rotate the permuter's element between i and size().
        @pre 0 <= i <= j <= size()
        小于i的元素不变，其余元素向右旋转 j-i 位
    */
    void rotate(int i, int j)
    {
        value_type mask =
            (i == width ? (value_type)0 : (value_type)16 << (i << 2)) - 1;
        value_type x = x_ & (((value_type)16 << (width << 2)) - 1);
        x_ = (x & mask) |
             ((x >> ((j - i) << 2)) & ~mask) |
             ((x & ~mask) << ((width - j) << 2));
    }

    /* Exchange the element at position i and j
    */
    void exchange(int i, int j)
    {
        value_type diff = ((x_ >> (i << 2)) ^ (x_ >> (j << 2))) & 240;
        x_ ^= (diff << (i << 2)) | (diff << (j << 2));
    }

    /* Exchange positions of values x and y.
    */
    void exchange_values(int x, int y)
    {
        value_type diff = 0, p = x_;
        for (int i = 0; i < width; ++i, diff <<= 4, p <<= 4)
        {
            int v = (p >> (width << 2)) & 15;
            diff ^= -((v == x) | (v == y)) & (x ^ y);
        }
        x_ ^= diff;
    }

    std::string unparse() const
    {
        char buf[max_width + 3], *s = buf;
        value_type p(x_);
        value_type seen(0);
        int n = p & 15;
        p >>= 4;
        for (int i = 0; true; ++i)
        {
            if (i == n)
                *s++ = ':';
            if (i == width)
                break;
            if ((p & 15) < 10)
                *s++ = '0' + (p & 15);
            else
                *s++ = 'a' + (p & 15) - 10;
            seen |= 1 << (p & 15);
            p >>= 4;
        }
        if (seen != (1 << width) - 1)
        {
            *s++ = '?';
            *s++ = '!';
        }
        return std::string(buf, s - buf);
    }

    bool operator==(const Kpermuter &x) const
    {
        return x_ == x.x_;
    }
    bool operator!=(const Kpermuter &x) const
    {
        return !(*this == x);
    }

    static inline int size(value_type p)
    {
        return p & 15;
    }

  private:
    value_type x_;
};

template <typename T> struct has_permuter_type
{
    template <typename C> static char test(typename C::permuter_type *);
    template <typename> static int test(...);
    static constexpr bool value = sizeof(test<T>(0)) == 1;
};

template <typename T, bool HP = has_permuter_type<T>::value>
struct KeyPermuter
{};

template <typename T> 
struct KeyPermuter<T, true>
{
    typedef typename T::permuter_type type;
    static type permutation(const T& n)
    {
        return n.permutation();
    }
};

template <typename T>
struct KeyPermuter<T, false>
{
    typedef IdentityKpermuter type;
    static type permutation(const T& n)
    {
        return IdentityKpermuter(n.size());
    }
};

} // namespace lf
