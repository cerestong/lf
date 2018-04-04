#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <stdlib.h>
#include "lf/slice.hh"
#include "lf/dtoa.hh"
#include "lf/itoa.hh"

namespace lf
{

std::string Slice::to_string(bool hex) const
{
  std::string result;
  if (hex)
  {
    char buf[10];
    for (size_t i = 0; i < size_; i++)
    {
      snprintf(buf, 10, "%02X", (unsigned char)data_[i]);
      result += buf;
    }
    return result;
  }
  else
  {
    result.assign(data_, size_);
    return result;
  }
}

bool Slice::to_int64(int64_t &out) const
{
  if (empty())
  {
    return false;
  }
  else
  {
    int error;
    char *end = (char *)data_ + size_;
    out = myitoa::strtoint64(data_, &end, &error);
    return ((error == MYITOA_ERRNO_ERANGE) ||
            (error == MYITOA_ERRNO_EDOM))
               ? false
               : true;
  }
}

bool Slice::to_uint64(uint64_t &out) const
{
  if (empty())
  {
    return false;
  }
  else
  {
    int error;
    char *end = (char *)data_ + size_;
    out = myitoa::strtouint64(data_, &end, &error);

    return (error == 0) ? true : false;
  }
}

bool Slice::to_float(float &out) const
{
  double d;
  bool ret = to_double(d);
  out = (float)d;
  return ret;
}

bool Slice::to_double(double &out) const
{
  if (empty())
  {
    return false;
  }
  else
  {
    int err;
    char *end = (char *)data_ + size_;
    out = mydtoa::strtod(data_, &end, &err);
    return (err == 0) ? true : false;
  }
}

Slice Slice::set_number(double num, int decimals, std::string *str)
{
  char buff[FLOATING_POINT_BUFFER];
  bool dummy_errors;
  size_t len = 0;

  if (str == NULL)
    return Slice();

  if (decimals >= NOT_FIXED_DEC)
  {
    len = mydtoa::gcvt(num, GCVT_ARG_DOUBLE, FLOATING_POINT_BUFFER - 1, buff, &dummy_errors);
  }
  else
  {
    len = mydtoa::fcvt(num, decimals, buff, NULL);
  }
  str->assign(buff, len);

  return Slice(*str);
}

Slice Slice::set_number(int64_t num, std::string *str)
{
  char buf[128];
  int len = 0;
  if (str == NULL)
    return Slice();

  len = snprintf(buf, sizeof(buf), "%" PRId64, num);
  str->assign(buf, len);

  return Slice(*str);
}

Slice Slice::set_number(uint64_t num, std::string *str)
{
  char buf[128];
  int len = 0;
  if (str == NULL)
    return Slice();

  len = snprintf(buf, sizeof(buf), "%" PRIu64, num);
  str->assign(buf, len);

  return Slice(*str);
}
}
