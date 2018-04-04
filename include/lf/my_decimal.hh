#pragma once

#include <assert.h>
#include <string>
#include "lf/decimal.hh"
#include "lf/logger.hh"

namespace lf
{

#define DECIMAL_BUFF_LENGTH 9
#define DECIMAL_MAX_POSSIBLE_PRECISION (DECIMAL_BUFF_LENGTH * 9)

/**
  maximum guaranteed precision of number in decimal digits (number of our
  digits * number of decimal digits in one our big digit - number of decimal
  digits in one our big digit decreased by 1 (because we always put decimal
  point on the border of our big digits))
*/
#define DECIMAL_MAX_PRECISION (DECIMAL_MAX_POSSIBLE_PRECISION - 8 * 2)
#define DECIMAL_MAX_SCALE 30
#define DECIMAL_NOT_SPECIFIED 31

#define NOT_FIXED_DEC 31

/**
  maximum length of string representation (number of maximum decimal
  digits + 1 position for sign + 1 position for decimal point, no terminator)
*/
#define DECIMAL_MAX_STR_LENGTH (DECIMAL_MAX_POSSIBLE_PRECISION + 2)

/**
  maximum size of packet length.
*/
#define DECIMAL_MAX_FIELD_SIZE DECIMAL_MAX_PRECISION

class my_decimal : public decimal_t
{

#if !defined(DBUG_OFF)
  int foo1;
#endif

  decimal_digit_t buffer[DECIMAL_BUFF_LENGTH];

#if !defined(DBUG_OFF)
  int foo2;
  static const int test_value = 123;
#endif

public:
  my_decimal(const my_decimal &rhs)
      : decimal_t(rhs)
  {
    rhs.sanity_check();

#if !defined(DBUG_OFF)
    foo1 = test_value;
    foo2 = test_value;
#endif

    for (int i = 0; i < DECIMAL_BUFF_LENGTH; i++)
    {
      buffer[i] = rhs.buffer[i];
    }
    buf = buffer;
  }

  my_decimal &operator=(const my_decimal &rhs)
  {
    sanity_check();
    rhs.sanity_check();

    if (this == &rhs)
    {
      return *this;
    }

    decimal_t::operator=(rhs);
    for (int i = 0; i < DECIMAL_BUFF_LENGTH; i++)
    {
      buffer[i] = rhs.buffer[i];
    }
    buf = buffer;
    return *this;
  }

  void init()
  {
#if !defined(DBUG_OFF)
    foo1 = test_value;
    foo2 = test_value;
#endif

    len = DECIMAL_BUFF_LENGTH;
    buf = buffer;
  }

  my_decimal()
  {
    init();
  }

  ~my_decimal()
  {
    sanity_check();
  }

  void sanity_check() const
  {
#if !defined(DBUG_OFF)
    assert(foo1 == test_value);
    assert(foo2 == test_value);
#endif
    assert(buf == buffer);
  }

  bool sign() const
  {
    return decimal_t::sign;
  }

  void sign(bool s)
  {
    decimal_t::sign = s;
  }

  int precision() const
  {
    return intg + frac;
  }

  /* swap two my_decimal values */
  void swap(my_decimal &rhs)
  {
    my_decimal tmp = *this;
    *this = rhs;
    rhs = tmp;
  }

  int check_result(uint32_t mask, int result) const;
};

void print_decimal(const my_decimal *dec);
void print_decimal_buff(const my_decimal *dec, const char *ptr, int length);
const char *dbug_decimal_as_string(char *buff, const my_decimal *val);

inline void max_my_decimal(my_decimal *to, int precision, int frac)
{
  assert((precision <= DECIMAL_MAX_PRECISION) &&
         (frac <= DECIMAL_MAX_SCALE));
  max_decimal(precision, frac, to);
}

inline void max_internal_decimal(my_decimal *to)
{
  max_my_decimal(to, DECIMAL_MAX_PRECISION, 0);
}

inline int check_result_and_overflow(uint32_t mask, int result, my_decimal *val)
{
  if (val->check_result(mask, result) & E_DEC_OVERFLOW)
  {
    bool sign = val->sign();
    val->sanity_check();
    max_internal_decimal(val);
    val->sign(sign);
  }
  /*
      Avoid returning negative zero, cfr. decimal_cmp()
      For result == E_DEC_DIV_ZERO *val has not been assigned.
    */
  if ((result != E_DEC_DIV_ZERO) && val->sign() && decimal_is_zero(val))
  {
    val->sign(false);
  }
  return result;
}

inline uint32_t my_decimal_length_to_precision(uint32_t length,
                                               uint32_t scale,
                                               bool unsigned_flag)
{
  /* Precision can't be negative thus ignore unsigned_flag when length is 0. */
  assert(length || !scale);
  uint32_t retval = (uint32_t)(length - (scale > 0 ? 1 : 0) -
                               (unsigned_flag || !length ? 0 : 1));
  return retval;
}

inline uint32_t my_decimal_precision_to_length_no_truncation(uint32_t precision,
                                                             uint8_t scale,
                                                             bool unsigned_flag)
{
  /*
      When precision is 0 it means that original length was also 0. Thus
      unsigned_flag is ignored in this case.
    */
  assert(precision || !scale);
  uint32_t retval = (uint32_t)(precision + (scale > 0 ? 1 : 0) +
                               (unsigned_flag || !precision ? 0 : 1));
  return retval;
}

inline uint32_t my_decimal_precision_to_length(uint32_t precision,
                                               uint8_t scale,
                                               bool unsigned_flag)
{
  /*
      When precision is 0 it means that original length was also 0. Thus
      unsigned_flag is ignored in this case.
    */
  assert(precision || !scale);
  if (precision > DECIMAL_MAX_PRECISION)
    precision = DECIMAL_MAX_PRECISION;
  return my_decimal_precision_to_length_no_truncation(precision, scale,
                                                      unsigned_flag);
}

inline int my_decimal_string_length(const my_decimal *d)
{
  /* length of string representation including terminating '\0' */
  return decimal_string_size(d);
}

inline int my_decimal_max_length(const my_decimal *d)
{
  /* -1 because we do not count \0 */
  return decimal_string_size(d) - 1;
}

inline int my_decimal_get_binary_size(uint32_t precision, uint32_t scale)
{
  return decimal_bin_size((int)precision, (int)scale);
}

inline void my_decimal2decimal(const my_decimal *from, my_decimal *to)
{
  *to = *from;
}

int my_decimal2binary(uint32_t mask, const my_decimal *d, char *bin, int prec,
                      int scale);

inline int binary2my_decimal(uint32_t mask, const char *bin, my_decimal *d, int prec,
                             int scale)
{
  return d->check_result(mask, bin2decimal(bin, d, prec, scale));
}

inline int my_decimal_set_zero(my_decimal *d)
{
  /*
      We need the up-cast here, since my_decimal has sign() member functions,
      which conflicts with decimal_t::size
      (and decimal_make_zero is a macro, rather than a funcion).
    */
  decimal_make_zero(static_cast<decimal_t *>(d));
  return 0;
}

inline bool my_decimal_is_zero(const my_decimal *decimal_value)
{
  return decimal_is_zero(decimal_value) ? true : false;
}

inline int my_decimal_round(uint32_t mask, const my_decimal *from, int scale,
                            bool truncate, my_decimal *to)
{
  return from->check_result(mask, decimal_round(from, to, scale,
                                                (truncate ? TRUNCATE : HALF_UP)));
}

inline int my_decimal_floor(uint32_t mask, const my_decimal *from, my_decimal *to)
{
  return from->check_result(mask, decimal_round(from, to, 0, FLOOR));
}

inline int my_decimal_ceiling(uint32_t mask, const my_decimal *from, my_decimal *to)
{
  return from->check_result(mask, decimal_round(from, to, 0, CEILING));
}

int my_decimal2string(uint32_t mask, const my_decimal *d, uint32_t fixed_prec,
                      uint32_t fixed_dec, char filler, std::string *str);

inline bool str_set_decimal(const my_decimal *val, std::string *str)
{
  my_decimal2string(E_DEC_FATAL_ERROR, val, 0, 0, 0, str);
  return true;
}

inline int my_decimal2int(uint32_t mask, const my_decimal *d, bool unsigned_flag,
                          int64_t *l)
{
  my_decimal rounded;
  /* decimal_round can return only E_DEC_TRUNCATED */
  decimal_round(d, &rounded, 0, HALF_UP);
  return d->check_result(mask, (unsigned_flag ? decimal2uint64(&rounded, (uint64_t *)l) : decimal2int64(&rounded, l)));
}

inline int my_decimal2double(uint32_t, const my_decimal *d, double *result)
{
  /* No need to call check_result as this will always succeed */
  return decimal2double(d, result);
}

inline int my_decimal2lldiv_t(uint32_t mask, const my_decimal *d, lldiv_t *to)
{
  return d->check_result(mask, decimal2lldiv_t(d, to));
}

inline int str2my_decimal(uint32_t mask, const char *str,
                          my_decimal *d, char **end)
{
  return check_result_and_overflow(mask, string2decimal(str, d, end), d);
}

int str2my_decimal(uint32_t mask, const char *from, size_t length,
                   my_decimal *decimal_value);

inline int string2my_decimal(uint32_t mask, const std::string *str, my_decimal *d)
{
  return str2my_decimal(mask, str->data(), str->size(), d);
}

inline int double2my_decimal(uint32_t mask, double val, my_decimal *d)
{
  return check_result_and_overflow(mask, double2decimal(val, d), d);
}

inline int int2my_decimal(uint32_t mask, int64_t i, bool unsigned_flag, my_decimal *d)
{
  return d->check_result(mask, (unsigned_flag ? uint64_2decimal((uint64_t)i, d) : int64_2decimal(i, d)));
}

inline void my_decimal_neg(decimal_t *arg)
{
  // Avoid returning negative zero, cfr. decimal_cmp()
  if (decimal_is_zero(arg))
  {
    arg->sign = 0;
    return;
  }
  arg->sign ^= 1;
}

inline int my_decimal_add(uint32_t mask, my_decimal *res, const my_decimal *a,
                          const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_add(a, b, res),
                                   res);
}

inline int my_decimal_sub(uint32_t mask, my_decimal *res, const my_decimal *a,
                          const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_sub(a, b, res),
                                   res);
}

inline int my_decimal_mul(uint32_t mask, my_decimal *res, const my_decimal *a,
                          const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_mul(a, b, res),
                                   res);
}

inline int my_decimal_div(uint32_t mask, my_decimal *res, const my_decimal *a,
                          const my_decimal *b, int div_scale_inc)
{
  return check_result_and_overflow(mask,
                                   decimal_div(a, b, res, div_scale_inc),
                                   res);
}

inline int my_decimal_mod(uint32_t mask, my_decimal *res, const my_decimal *a,
                          const my_decimal *b)
{
  return check_result_and_overflow(mask,
                                   decimal_mod(a, b, res),
                                   res);
}

/**
     @return
     -1 if a<b, 1 if a>b and 0 if a==b
  */
inline int my_decimal_cmp(const my_decimal *a, const my_decimal *b)
{
  return decimal_cmp(a, b);
}

inline bool operator<(const my_decimal &lhs, const my_decimal &rhs)
{
  return my_decimal_cmp(&lhs, &rhs) < 0;
}

inline bool operator!=(const my_decimal &lhs, const my_decimal &rhs)
{
  return my_decimal_cmp(&lhs, &rhs) != 0;
}

inline int my_decimal_intg(const my_decimal *a)
{
  return decimal_intg(a);
}

my_decimal *lldiv_t2my_decimal(const lldiv_t *lld, bool neg, my_decimal *dec);

void my_decimal_trim(uint32_t *precision, uint32_t *scale);

inline int my_decimal2number(const my_decimal *d, int8_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, false, &val);
  *l = (int8_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, uint8_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, true, &val);
  *l = (uint8_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, int16_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, false, &val);
  *l = (int16_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, uint16_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, true, &val);
  *l = (uint16_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, int32_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, false, &val);
  *l = (int32_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, uint32_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, true, &val);
  *l = (uint32_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, int64_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, false, &val);
  *l = (int64_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, uint64_t *l)
{
  int64_t val = 0;
  int ret = my_decimal2int(E_DEC_FATAL_ERROR, d, true, &val);
  *l = (uint64_t)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, float *l)
{
  double val = 0.0;
  int ret = my_decimal2double(E_DEC_FATAL_ERROR, d, &val);
  *l = (float)val;
  return ret;
}

inline int my_decimal2number(const my_decimal *d, double *l)
{
  double val = 0.0;
  int ret = my_decimal2double(E_DEC_FATAL_ERROR, d, &val);
  *l = val;
  return ret;
}

} // namespace lf
