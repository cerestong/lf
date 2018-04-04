#include "lf/my_decimal.hh"

namespace lf
{

/**
     report result of decimal operation.

     @param mask    bitmask filtering result, most likely E_DEC_FATAL_ERROR
     @param result  decimal library return code (E_DEC_* see include/decimal.h)

     @return
     result
  */
int my_decimal::check_result(uint32_t mask, int result) const
{
  if (result & mask)
  {
    int length = DECIMAL_MAX_STR_LENGTH + 1;
    char strbuff[DECIMAL_MAX_STR_LENGTH + 2];

    switch (result)
    {
    case E_DEC_TRUNCATED:
      // "Data truncated for column \'%s\' at row %ld"
      log("E_DEC_TRUNCATED");
      break;
    case E_DEC_OVERFLOW:
      // "Truncated incorrect %-.32s value: \'%-.128s\'"
      decimal2string(this, strbuff, &length, 0, 0, 0);
      log("E_DEC_OVERFLOW decimal: %s", strbuff);
      break;
    case E_DEC_DIV_ZERO:
      // "Division by 0"
      log("E_DEC_DIV_ZERO");
      break;
    case E_DEC_BAD_NUM:
      // "Incorrect %-.32s value: \'%-.128s\' for column \'%.192s\' at row %ld"
      decimal2string(this, strbuff, &length, 0, 0, 0);
      log("E_DEC_BAD_NUM decimal: %s", strbuff);
      break;
    case E_DEC_OOM:
      log("ER_OUT_OF_RESOURCES");
      break;
    default:
      assert(0);
    }
  }
  return result;
}

/**
     @brief Converting decimal to string
     
     @details Convert given my_decimal to string; allocate buffer as needed.
     
     @param[in]   mask        what problems to warn on (mask of E_DEC_* values)
     @param[in]   d           the decimal to print
     @param[in]   fixed_prec  overall number of digits if ZEROFILL, 0 otherwise
     @param[in]   fixed_dec   number of decimal places (if fixed_prec != 0)
     @param[in]   filler      what char to pad with (ZEROFILL et al.)
     @param[out]  *str        where to store the resulting string
     
     @return error coce
     @retval E_DEC_OK
     @retval E_DEC_TRUNCATED
     @retval E_DEC_OVERFLOW
     @retval E_DEC_OOM
  */

int my_decimal2string(uint32_t mask, const my_decimal *d,
                      uint32_t fixed_prec, uint32_t fixed_dec,
                      char filler, std::string *str)
{
  /*
      Calculate the size of the string: For DECIMAL(a,b), fixed_prec==a
      holds true iff the type is also ZEROFILL, which in turn implies
      UNSIGNED. Hence the buffer for a ZEROFILLed value is the length
      the user requested, plus one for a possible decimal point, plus
      one if the user only wanted decimal places, but we force a leading
      zero on them, plus one for the '\0' terminator. Because the type
      is implicitly UNSIGNED, we do not need to reserve a character for
      the sign. For all other cases, fixed_prec will be 0, and
      my_decimal_string_length() will be called instead to calculate the
      required size of the buffer.
    */
  int length = (fixed_prec
                    ? (fixed_prec + ((fixed_prec == fixed_dec) ? 1 : 0) + 1 + 1)
                    : my_decimal_string_length(d));
  int result;
  str->resize(length);
  result = decimal2string((decimal_t *)d, &(*str)[0],
                          &length, (int)fixed_prec, (int)fixed_dec,
                          filler);
  str->resize(length);
  return d->check_result(mask, result);
}

/*
    Convert from decimal to binary representation

    SYNOPSIS
    my_decimal2binary()
    mask        error processing mask
    d           number for conversion
    bin         pointer to buffer where to write result
    prec        overall number of decimal digits
    scale       number of decimal digits after decimal point

    NOTE
    Before conversion we round number if it need but produce truncation
    error in this case

    RETURN
    E_DEC_OK
    E_DEC_TRUNCATED
    E_DEC_OVERFLOW
  */

int my_decimal2binary(uint32_t mask, const my_decimal *d, char *bin, int prec,
                      int scale)
{
  int err1 = E_DEC_OK, err2;
  my_decimal rounded;
  my_decimal2decimal(d, &rounded);
  rounded.frac = decimal_actual_fraction(&rounded);
  if (scale < rounded.frac)
  {
    err1 = E_DEC_TRUNCATED;
    /* decimal_round can return only E_DEC_TRUNCATED */
    decimal_round(&rounded, &rounded, scale, HALF_UP);
  }
  err2 = decimal2bin(&rounded, bin, prec, scale);
  if (!err2)
    err2 = err1;
  return d->check_result(mask, err2);
}

/*
  SYNOPSIS
    str2my_decimal()
    mask            error processing mask
    from            string to process
    length          length of given string
    decimal_value   buffer for result storing

  RESULT
    E_DEC_OK
    E_DEC_TRUNCATED
    E_DEC_OVERFLOW
    E_DEC_BAD_NUM
    E_DEC_OOM
*/

int str2my_decimal(uint32_t mask, const char *from, size_t length,
                   my_decimal *decimal_value)
{
  char *end, *from_end;
  int err;

  from_end = end = (char *)from + length;
  err = string2decimal((char *)from, (decimal_t *)decimal_value, &end);
#if 0
    if (end != from_end && !err) {
      /* Give warning if there is something other than end space */
      for ( ; end < from_end; end++) {
	if (!(*end == ' ' || *end == '\t')) {
	  err= E_DEC_TRUNCATED;
	  break;
	}
      }
    }
#endif
  check_result_and_overflow(mask, err, decimal_value);
  return err;
}

/**
     Convert lldiv_t value to my_decimal value.
     Integer part of the result is set to lld->quot.
     Fractional part of the result is set to lld->rem divided to 1000000000.

     @param       lld  The lldiv_t variable to convert from.
     @param       neg  Sign flag (negative, 0 positive).
     @param  OUT  dec  Decimal numbert to convert to.
  */
my_decimal *lldiv_t2my_decimal(const lldiv_t *lld, bool neg,
                               my_decimal *dec)
{
  if (int2my_decimal(E_DEC_FATAL_ERROR, lld->quot, false, dec))
    return dec;
  if (lld->rem)
  {
    dec->buf[(dec->intg - 1) / 9 + 1] = static_cast<decimal_digit_t>(lld->rem);
    dec->frac = 6;
  }
  if (neg)
    my_decimal_neg(dec);
  return dec;
}

void my_decimal_trim(uint32_t *precision, uint32_t *scale)
{
  if (!(*precision) && !(*scale))
  {
    *precision = 10;
    *scale = 0;
    return;
  }
}

#define DIG_PER_DEC1 9
#define ROUND_UP(X) (((X) + DIG_PER_DEC1 - 1) / DIG_PER_DEC1)

/* print decimal */
void print_decimal(const my_decimal *dec)
{
  int i, end;
  char buff[512], *pos;
  pos = buff;
  pos += sprintf(buff, "Decimal: sign: %d  intg: %d  frac: %d  { ",
                 dec->sign(), dec->intg, dec->frac);
  end = ROUND_UP(dec->frac) + ROUND_UP(dec->intg) - 1;
  for (i = 0; i < end; i++)
    pos += sprintf(pos, "%09d, ", dec->buf[i]);
  pos += sprintf(pos, "%09d }\n", dec->buf[i]);

  log(buff);
}

/* print decimal with its binary representation */
void print_decimal_buff(const my_decimal *dec, const char *ptr, int length)
{
  print_decimal(dec);
  log("Record: ");
  for (int i = 0; i < length; i++)
  {
    log("%02X ", (uint32_t)((char *)ptr)[i]);
  }
  log("\n");
}

const char *dbug_decimal_as_string(char *buff, const my_decimal *val)
{
  int length = DECIMAL_MAX_STR_LENGTH + 1; /* minimum size for buff */
  if (!val)
    return "NULL";
  (void)decimal2string((decimal_t *)val, buff, &length, 0, 0, 0);
  return buff;
}

} // namespace pi
