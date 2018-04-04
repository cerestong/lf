#pragma once

#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <cstdio>
#include <string>
#include <stdint.h>

namespace lf
{

class Slice
{
public:
  const char *data_;
  size_t size_;

public:
  Slice() : data_(""), size_(0) {}
  // d[0, n-1]
  Slice(const char *d, size_t n) : data_(d), size_(n) {}
  Slice(const std::string &s) : data_(s.data()), size_(s.size()) {}
  // s[0, strlen(s)-1]
  Slice(const char *s) : data_(s), size_(strlen(s)) {}

  const char *data() const { return data_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  char operator[](size_t n) const
  {
    assert(n < size());
    return data_[n];
  }

  void clear()
  {
    data_ = "";
    size_ = 0;
  }

  void remove_prefix(size_t n)
  {
    assert(n <= size());
    data_ += n;
    size_ -= n;
  }

  void remove_suffix(size_t n)
  {
    assert(n <= size());
    size_ -= n;
  }

  size_t find(char c)
  {
    size_t i = 0;
    for (; i < size_; i++)
    {
      if (data_[i] == c)
      {
        break;
      }
    }
    return i;
  }

  std::string to_string(bool hex = false) const;

  // < 0 *this < b
  // == 0 *this == b
  // > 0 *this > b
  int compare(const Slice &b) const;
  size_t difference_offset(const Slice &b) const;

  bool starts_with(const Slice &x) const
  {
    return ((size_ >= x.size_) &&
            (memcmp(data_, x.data_, x.size_) == 0));
  }

  bool ends_with(const Slice &x) const
  {
    return ((size_ >= x.size_) &&
            (memcmp(data_ + size_ - x.size_, x.data_, x.size_) == 0));
  }

  bool to_int64(int64_t &out) const;

  bool to_uint64(uint64_t &out) const;

  bool to_float(float &out) const;

  bool to_double(double &out) const;

  bool to_number(int8_t &out) const
  {
    int64_t tmp = 0;
    bool ret = to_int64(tmp);
    if (ret)
      out = (int8_t)tmp;
    return ret;
  }

  bool to_number(uint8_t &out) const
  {
    uint64_t tmp = 0;
    bool ret = to_uint64(tmp);
    if (ret)
      out = (uint8_t)tmp;
    return ret;
  }

  bool to_number(int16_t &out) const
  {
    int64_t tmp = 0;
    bool ret = to_int64(tmp);
    if (ret)
      out = (int16_t)tmp;
    return ret;
  }

  bool to_number(uint16_t &out) const
  {
    uint64_t tmp = 0;
    bool ret = to_uint64(tmp);
    if (ret)
      out = (uint16_t)tmp;
    return ret;
  }

  bool to_number(int32_t &out) const
  {
    int64_t tmp = 0;
    bool ret = to_int64(tmp);
    if (ret)
      out = (int32_t)tmp;
    return ret;
  }

  bool to_number(uint32_t &out) const
  {
    uint64_t tmp = 0;
    bool ret = to_uint64(tmp);
    if (ret)
      out = (uint32_t)tmp;
    return ret;
  }

  bool to_number(int64_t &out) const
  {
    int64_t tmp = 0;
    bool ret = to_int64(tmp);
    if (ret)
      out = (int64_t)tmp;
    return ret;
  }

  bool to_number(uint64_t &out) const
  {
    uint64_t tmp = 0;
    bool ret = to_uint64(tmp);
    if (ret)
      out = (uint64_t)tmp;
    return ret;
  }

  bool to_number(float &out) const
  {
    return to_float(out);
  }

  bool to_number(double &out) const
  {
    return to_double(out);
  }

  static Slice set_number(double num, int decimals, std::string *str);
  static Slice set_number(int64_t num, std::string *str);
  static Slice set_number(uint64_t num, std::string *str);
};

inline bool operator==(const Slice &x, const Slice &y)
{
  return ((x.size() == y.size()) &&
          (memcmp(x.data(), y.data(), x.size()) == 0));
}

inline bool operator!=(const Slice &x, const Slice &y)
{
  return !(x == y);
}

inline int Slice::compare(const Slice &b) const
{
  const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
  int r = memcmp(data_, b.data_, min_len);
  if (r == 0)
  {
    if (size_ < b.size_)
      r = -1;
    else if (size_ > b.size_)
      r = 1;
  }
  return r;
}

inline size_t Slice::difference_offset(const Slice &b) const
{
  size_t off = 0;
  const size_t len = (size_ < b.size_) ? size_ : b.size_;
  for (; off < len; off++)
  {
    if (data_[off] != b.data_[off])
      break;
  }
  return off;
}
}
