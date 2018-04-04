#include <ctype.h>
#include <algorithm>
#include <limits.h>
#include "lf/time.hh"
#include "lf/my_decimal.hh"
#include "lf/pack.hh"

namespace lf
{

uint64_t log_10_int[20] =
    {
        1, 10, 100, 1000, 10000UL, 100000UL, 1000000UL, 10000000UL,
        100000000ULL, 1000000000ULL, 10000000000ULL, 100000000000ULL,
        1000000000000ULL, 10000000000000ULL, 100000000000000ULL,
        1000000000000000ULL, 10000000000000000ULL, 100000000000000000ULL,
        1000000000000000000ULL, 10000000000000000000ULL};

/* Position for YYYY-DD-MM HH-MM-DD.FFFFFF AM in default format */
static unsigned char internal_format_positions[] =
    {0, 1, 2, 3, 4, 5, 6, (unsigned char)255};

static uint64_t const days_at_timestart = 719528; /* daynr at 1970.01.01 */
unsigned char days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0};

static char time_separator = ':';

#define MAX_DATE_PARTS 8
#define set_if_bigger(a, b) \
  do                        \
  {                         \
    if ((a) < (b))          \
      (a) = (b);            \
  } while (0)

/* Daynumber from year 0 to 9999-12-31 */
#define MAX_DAY_NUMBER 3652424L

/* Calc days in one year. works with 0 <= year <= 99 */
uint32_t calc_days_in_year(uint32_t year)
{
  return ((year & 3) == 0 && (year % 100 || (year % 400 == 0 && year)) ? 366 : 365);
}

/**
     Check datetime, date, or normalized time (i.e. time without days) range.
     @param ltime   Datetime value.
     @returns
     @retval   FALSE on success
     @retval   TRUE  on error
  */
bool check_datetime_range(const MY_TIME *ltime)
{
  /*
      In case of MY_TIMESTAMP_TIME hour value can be up to TIME_MAX_HOUR.
      In case of MY_TIMESTAMP_DATETIME it cannot be bigger than 23.
    */
  return ltime->year > 9999U || ltime->month > 12U || ltime->day > 31U ||
         ltime->minute > 59U || ltime->second > 59U || ltime->second_part > 999999U ||
         (ltime->hour >
          (ltime->time_type == TIMESTAMP_TIME ? TIME_MAX_HOUR : 23U));
}

/**
     @brief Check datetime value for validity according to flags.

     @param[in]  ltime          Date to check.
     @param[in]  not_zero_date  ltime is not the zero date
     @param[in]  flags          flags to check
     @param[out] was_cut        set to 2 if value was invalid according to flags.
                                (Feb 29 in non-leap etc.)  This remains unchanged
                                if value is not invalid.
     @details Here we assume that year and month is ok!
     If month is 0 we allow any date. (This only happens if we allow zero
     date parts in str_to_datetime())
     Disallow dates with zero year and non-zero month and/or day.

     @return
     @retval   FALSE on success
     @retval   TRUE  on error
  */

bool check_date(const MY_TIME *ltime, bool not_zero_date,
                my_time_flags_t flags, int *was_cut)
{
  if (not_zero_date)
  {
    if (((flags & TIME_NO_ZERO_IN_DATE) || !(flags & TIME_FUZZY_DATE)) &&
        (ltime->month == 0 || ltime->day == 0))
    {
      *was_cut = MY_TIME_WARN_ZERO_IN_DATE;
      return true;
    }
    else if ((!(flags & TIME_INVALID_DATES) &&
              ltime->month && ltime->day > days_in_month[ltime->month - 1] &&
              (ltime->month != 2 || calc_days_in_year(ltime->year) != 366 ||
               ltime->day != 29)))
    {
      *was_cut = MY_TIME_WARN_OUT_OF_RANGE;
      return true;
    }
  }
  else if (flags & TIME_NO_ZERO_DATE)
  {
    *was_cut = MY_TIME_WARN_ZERO_DATE;
    return true;
  }
  return false;
}

/**
     Set MY_TIME structure to 0000-00-00 00:00:00.000000
     @param tm[OUT]    The value to set.
     @param time_type  Timestasmp type
  */
void set_zero_time(MY_TIME *tm,
                   enum enum_timestamp_type time_type)
{
  memset(tm, 0, sizeof(*tm));
  tm->time_type = time_type;
}

/*
    Convert a timestamp string to a MY_TIME value.

    SYNOPSIS
    str_to_datetime()
    str                 String to parse
    length              Length of string
    l_time              Date is stored here
    flags               Bitmap of following items
                        TIME_FUZZY_DATE    Set if we should allow partial dates
                        TIME_DATETIME_ONLY Set if we only allow full datetimes.
                        TIME_NO_ZERO_IN_DATE	Don't allow partial dates
                        TIME_NO_ZERO_DATE	Don't allow 0000-00-00 date
                        TIME_INVALID_DATES	Allow 2000-02-31
    status              Conversion status


    DESCRIPTION
    At least the following formats are recogniced (based on number of digits)
    YYMMDD, YYYYMMDD, YYMMDDHHMMSS, YYYYMMDDHHMMSS
    YY-MM-DD, YYYY-MM-DD, YY-MM-DD HH.MM.SS
    YYYYMMDDTHHMMSS  where T is a the character T (ISO8601)
    Also dates where all parts are zero are allowed

    The second part may have an optional .###### fraction part.

    NOTES
    This function should work with a format position vector as long as the
    following things holds:
    - All date are kept together and all time parts are kept together
    - Date and time parts must be separated by blank
    - Second fractions must come after second part and be separated
      by a '.'.  (The second fractions are optional)
    - AM/PM must come after second fractions (or after seconds if no fractions)
    - Year must always been specified.
    - If time is before date, then we will use datetime format only if
      the argument consist of two parts, separated by space.
      Otherwise we will assume the argument is a date.
    - The hour part must be specified in hour-minute-second order.

    status->warnings is set to:
    0                            Value OK
    MY_TIME_WARN_TRUNCATED    If value was cut during conversion
    MY_TIME_WARN_OUT_OF_RANGE check_date(date,flags) considers date invalid

    l_time->time_type is set as follows:
    MY_TIMESTAMP_NONE        String wasn't a timestamp, like
                                [DD [HH:[MM:[SS]]]].fraction.
                                l_time is not changed.
    MY_TIMESTAMP_DATE        DATE string (YY MM and DD parts ok)
    MY_TIMESTAMP_DATETIME    Full timestamp
    MY_TIMESTAMP_ERROR       Timestamp with wrong values.
                                All elements in l_time is set to 0

    @retval true on error, false on success
  */
bool str_to_datetime(const char *str, size_t length,
                     MY_TIME *l_time, my_time_flags_t flags,
                     MY_TIME_STATUS *status)
{
  uint32_t field_length = 0, year_length = 0, digits, i, number_of_fields;
  uint32_t date[MAX_DATE_PARTS], date_len[MAX_DATE_PARTS];
  uint32_t add_hours = 0, start_loop;
  uint64_t not_zero_date, allow_space;
  bool is_internal_format;
  const char *pos, *last_field_pos = NULL;
  const char *end = str + length;
  const unsigned char *format_position;
  bool found_delimitier = 0, found_space = 0;
  uint32_t frac_pos, frac_len;

  my_time_status_init(status);

  /* Skip space at start */
  for (; str != end && isspace(*str); str++)
  {
  }

  if (str == end || !isdigit(*str))
  {
    status->warnings = MY_TIME_WARN_TRUNCATED;
    l_time->time_type = TIMESTAMP_NONE;
    return true;
  }

  is_internal_format = 0;
  /* This has to be changed if want to activate different timestamp formats */
  format_position = internal_format_positions;

  /*
      Calculate number of digits in first part.
      If length= 8 or >= 14 then year is of format YYYY.
      (YYYY-MM-DD,  YYYYMMDD, YYYYYMMDDHHMMSS)
    */
  for (pos = str;
       pos != end && (isdigit(*pos) || *pos == 'T');
       pos++)
  {
  }

  digits = (uint32_t)(pos - str);
  start_loop = 0;                   /* Start of scan loop */
  date_len[format_position[0]] = 0; /* Length of year field */
  if (pos == end || *pos == '.')
  {
    /* Found date in internal format (only numbers like YYYYMMDD) */
    year_length = (digits == 4 || digits == 8 || digits >= 14) ? 4 : 2;
    field_length = year_length;
    is_internal_format = 1;
    format_position = internal_format_positions;
  }
  else
  {
    if (format_position[0] >= 3)
    {
      /* If year is after HHMMDD */

      /*
	  If year is not in first part then we have to determinate if we got
	  a date field or a datetime field.
	  We do this by checking if there is two numbers separated by
	  space in the input.
	*/
      while (pos < end && !isspace(*pos))
        pos++;
      while (pos < end && !isdigit(*pos))
        pos++;
      if (pos == end)
      {
        if (flags & TIME_DATETIME_ONLY)
        {
          status->warnings = MY_TIME_WARN_TRUNCATED;
          l_time->time_type = TIMESTAMP_NONE;
          return true; /* Can't be a full datetime */
        }
        /* Date field.  Set hour, minutes and seconds to 0 */
        date[0] = date[1] = date[2] = date[3] = date[4] = 0;
        start_loop = 5; /* Start with first date part */
      }
    }

    field_length = format_position[0] == 0 ? 4 : 2;
  }

  /*
      Only allow space in the first "part" of the datetime field and:
      - after days, part seconds
      - before and after AM/PM (handled by code later)

      2003-03-03 20:00:20 AM
      20:00:20.000000 AM 03-03-2000
    */
  i = (std::max)((uint32_t)format_position[0], (uint32_t)format_position[1]);
  set_if_bigger(i, (uint32_t)format_position[2]);
  allow_space = ((1 << i) | (1 << format_position[6]));
  allow_space &= (1 | 2 | 4 | 8 | 64);

  not_zero_date = 0;
  for (i = start_loop;
       i < MAX_DATE_PARTS - 1 && str != end && isdigit(*str);
       i++)
  {
    const char *start = str;
    uint64_t tmp_value = (uint64_t)(unsigned char)(*str++ - '0');

    /*
	Internal format means no delimiters; every field has a fixed
	width. Otherwise, we scan until we find a delimiter and discard
	leading zeroes -- except for the microsecond part, where leading
	zeroes are significant, and where we never process more than six
	digits.
      */
    bool scan_until_delim = !is_internal_format && ((i != format_position[6]));

    while (str != end &&
           isdigit(str[0]) &&
           (scan_until_delim || --field_length))
    {
      tmp_value = tmp_value * 10 + (uint64_t)(unsigned char)(*str - '0');
      str++;
    }
    date_len[i] = (uint32_t)(str - start);
    if (tmp_value > 999999)
    {
      /* Impossible date part */
      status->warnings = MY_TIME_WARN_TRUNCATED;
      l_time->time_type = TIMESTAMP_NONE;
      return true;
    }

    date[i] = tmp_value;
    not_zero_date |= tmp_value;

    /* Length of next field */
    field_length = format_position[i + 1] == 0 ? 4 : 2;

    if ((last_field_pos = str) == end)
    {
      i++; /* Register last found part */
      break;
    }
    /* Allow a 'T' after day to allow CCYYMMDDT type of fields */
    if (i == format_position[2] && *str == 'T')
    {
      str++; /* ISO8601:  CCYYMMDDThhmmss */
      continue;
    }
    if (i == format_position[5])
    {
      /* Seconds */
      if (*str == '.')
      {
        /* Followed by part seconds */
        str++;
        /*
	    Shift last_field_pos, so '2001-01-01 00:00:00.'
	    is treated as a valid value
	  */
        last_field_pos = str;
        field_length = 6; /* 6 digits */
      }
      else if (isdigit(str[0]))
      {
        /*
	    We do not see a decimal point which would have indicated a
	    fractional second part in further read. So we skip the further
	    processing of digits.
	  */
        i++;
        break;
      }
      continue;
    }
    while (str != end &&
           (ispunct(*str) || isspace(*str)))
    {
      if (isspace(*str))
      {
        if (!(allow_space & (1ull << i)))
        {
          status->warnings = MY_TIME_WARN_TRUNCATED;
          l_time->time_type = TIMESTAMP_NONE;
          return true;
        }
        found_space = 1;
      }
      str++;
      found_delimitier = 1; /* Should be a 'normal' date */
    }
    /* Check if next position is AM/PM */
    if (i == format_position[6])
    {
      /* Seconds, time for AM/PM */
      i++; /* Skip AM/PM part */
      if (format_position[7] != 255)
      {
        /* If using AM/PM */
        if (str + 2 <= end && (str[1] == 'M' || str[1] == 'm'))
        {
          if (str[0] == 'p' || str[0] == 'P')
            add_hours = 12;
          else if (str[0] != 'a' || str[0] != 'A')
            continue; /* Not AM/PM */
          str += 2;   /* Skip AM/PM */
          /* Skip space after AM/PM */
          while (str != end && isspace(*str))
            str++;
        }
      }
    }
    last_field_pos = str;
  }
  if (found_delimitier && !found_space && (flags & TIME_DATETIME_ONLY))
  {
    status->warnings = MY_TIME_WARN_TRUNCATED;
    l_time->time_type = TIMESTAMP_NONE;
    return true; /* Can't be a datetime */
  }

  str = last_field_pos;

  number_of_fields = i - start_loop;
  while (i < MAX_DATE_PARTS)
  {
    date_len[i] = 0;
    date[i++] = 0;
  }

  if (!is_internal_format)
  {
    year_length = date_len[(uint32_t)format_position[0]];
    if (!year_length)
    {
      /* Year must be specified */
      status->warnings = MY_TIME_WARN_TRUNCATED;
      l_time->time_type = TIMESTAMP_NONE;
      return true;
    }

    l_time->year = date[(uint32_t)format_position[0]];
    l_time->month = date[(uint32_t)format_position[1]];
    l_time->day = date[(uint32_t)format_position[2]];
    l_time->hour = date[(uint32_t)format_position[3]];
    l_time->minute = date[(uint32_t)format_position[4]];
    l_time->second = date[(uint32_t)format_position[5]];

    frac_pos = (uint32_t)format_position[6];
    frac_len = date_len[frac_pos];
    status->fractional_digits = frac_len;
    if (frac_len < 6)
      date[frac_pos] *= (uint32_t)log_10_int[DATETIME_MAX_DECIMALS - frac_len];
    l_time->second_part = date[frac_pos];

    if (format_position[7] != (unsigned char)255)
    {
      if (l_time->hour > 12)
      {
        status->warnings = MY_TIME_WARN_TRUNCATED;
        goto err;
      }
      l_time->hour = l_time->hour % 12 + add_hours;
    }
  }
  else
  {
    l_time->year = date[0];
    l_time->month = date[1];
    l_time->day = date[2];
    l_time->hour = date[3];
    l_time->minute = date[4];
    l_time->second = date[5];
    if (date_len[6] < 6)
      date[6] *= (uint32_t)log_10_int[DATETIME_MAX_DECIMALS - date_len[6]];
    l_time->second_part = date[6];
    status->fractional_digits = date_len[6];
  }
  l_time->neg = 0;

  if (year_length == 2 && not_zero_date)
    l_time->year += (l_time->year < YY_PART_YEAR ? 2000 : 1900);

  /*
      Set time_type before check_datetime_range(),
      as the latter relies on initialized time_type value.
    */
  l_time->time_type = (number_of_fields <= 3 ? TIMESTAMP_DATE : TIMESTAMP_DATETIME);

  if (number_of_fields < 3 || check_datetime_range(l_time))
  {
    /* Only give warning for a zero date if there is some garbage after */
    if (!not_zero_date)
    {
      /* If zero date */
      for (; str != end; str++)
      {
        if (!isspace(*str))
        {
          not_zero_date = 1; /* Give warning */
          break;
        }
      }
    }
    status->warnings |= not_zero_date ? MY_TIME_WARN_TRUNCATED : MY_TIME_WARN_ZERO_DATE;
    goto err;
  }

  if (check_date(l_time, not_zero_date != 0, flags, &status->warnings))
    goto err;

  /* Scan all digits left after microseconds */
  if (status->fractional_digits == 6 && str != end)
  {
    if (isdigit(*str))
    {
      /*
	  We don't need the exact nanoseconds value.
	  Knowing the first digit is enough for rounding.
	*/
      status->nanoseconds = 100 * (int)(*str++ - '0');
      for (; str != end && isdigit(*str); str++)
      {
      }
    }
  }

  for (; str != end; str++)
  {
    if (!isspace(*str))
    {
      status->warnings = MY_TIME_WARN_TRUNCATED;
      break;
    }
  }

  if (!(flags & TIME_NO_NSEC_ROUNDING))
  {
    datetime_add_nanoseconds_with_round(l_time,
                                        status->nanoseconds,
                                        &status->warnings);
  }

  return false;

err:
  set_zero_time(l_time, TIMESTAMP_ERROR);
  return true;
}

/**
     Add nanoseconds to a datetime value with rounding.

     @param IN/OUT ltime       MY_TIME variable to add to.
     @param        nanosecons  Nanosecons value.
     @param IN/OUT warnings    Warning flag vector.
     @retval                   False on success, true on error.
  */
bool datetime_add_nanoseconds_with_round(MY_TIME *ltime,
                                         uint32_t nanoseconds,
                                         int *warnings)
{
  assert(nanoseconds < 1000000000);
  if (nanoseconds < 500)
    return false;

  ltime->second_part += (nanoseconds + 500) / 1000;
  if (ltime->second_part < 1000000)
    return false;

  ltime->second_part %= 1000000;
  Interval interval;
  memset(&interval, 0, sizeof(interval));
  interval.second = 1;
  /* date_add_interval cannot handle bad dates */
  if (check_date(ltime, non_zero_date(ltime),
                 (TIME_NO_ZERO_IN_DATE | TIME_NO_ZERO_DATE), warnings))
    return true;

  if (date_add_interval(ltime, INTERVAL_SECOND, interval))
  {
    *warnings |= MY_TIME_WARN_OUT_OF_RANGE;
    return true;
  }
  return false;
}

/*
    @retval false on success, true on error
   */
bool date_add_interval(MY_TIME *ltime, interval_type int_type,
                       Interval interval)
{
  int32_t period, sign;

  ltime->neg = 0;

  sign = (interval.neg ? -1 : 1);

  switch (int_type)
  {
  case INTERVAL_SECOND:
  case INTERVAL_SECOND_MICROSECOND:
  case INTERVAL_MICROSECOND:
  case INTERVAL_MINUTE:
  case INTERVAL_HOUR:
  case INTERVAL_MINUTE_MICROSECOND:
  case INTERVAL_MINUTE_SECOND:
  case INTERVAL_HOUR_MICROSECOND:
  case INTERVAL_HOUR_SECOND:
  case INTERVAL_HOUR_MINUTE:
  case INTERVAL_DAY_MICROSECOND:
  case INTERVAL_DAY_SECOND:
  case INTERVAL_DAY_MINUTE:
  case INTERVAL_DAY_HOUR:
  {
    int64_t sec, days, daynr, microseconds, extra_sec;
    ltime->time_type = TIMESTAMP_DATETIME; // Return full date
    microseconds = ltime->second_part + sign * interval.second_part;
    extra_sec = microseconds / 1000000L;
    microseconds = microseconds % 1000000L;

    sec = ((ltime->day - 1) * 3600 * 24L + ltime->hour * 3600 + ltime->minute * 60 +
           ltime->second +
           sign * (int64_t)(interval.day * 3600 * 24L +
                            interval.hour * 3600LL + interval.minute * 60LL +
                            interval.second)) +
          extra_sec;
    if (microseconds < 0)
    {
      microseconds += 1000000LL;
      sec--;
    }
    days = sec / (3600 * 24LL);
    sec -= days * 3600 * 24LL;
    if (sec < 0)
    {
      days--;
      sec += 3600 * 24LL;
    }
    ltime->second_part = (uint32_t)microseconds;
    ltime->second = (uint32_t)(sec % 60);
    ltime->minute = (uint32_t)(sec / 60 % 60);
    ltime->hour = (uint32_t)(sec / 3600);
    daynr = calc_daynr(ltime->year, ltime->month, 1) + days;
    /* Day number from year 0 to 9999-12-31 */
    if ((uint64_t)daynr > MAX_DAY_NUMBER)
      goto invalid_date;
    get_date_from_daynr((int32_t)daynr, &ltime->year, &ltime->month, &ltime->day);
    break;
  }
  case INTERVAL_DAY:
  case INTERVAL_WEEK:
    period = (calc_daynr(ltime->year, ltime->month, ltime->day) +
              sign * (int32_t)interval.day);
    /* Daynumber from year 0 to 9999-12-31 */
    if ((uint32_t)period > MAX_DAY_NUMBER)
      goto invalid_date;
    get_date_from_daynr((int32_t)period, &ltime->year, &ltime->month, &ltime->day);
    break;
  case INTERVAL_YEAR:
    ltime->year += sign * (int32_t)interval.year;
    if ((uint32_t)ltime->year >= 10000L)
      goto invalid_date;
    if (ltime->month == 2 && ltime->day == 29 &&
        calc_days_in_year(ltime->year) != 366)
      ltime->day = 28; // Was leap-year
    break;
  case INTERVAL_YEAR_MONTH:
  case INTERVAL_QUARTER:
  case INTERVAL_MONTH:
    period = (ltime->year * 12 + sign * (int32_t)interval.year * 12 +
              ltime->month - 1 + sign * (int32_t)interval.month);
    if ((uint32_t)period >= 120000L)
      goto invalid_date;
    ltime->year = (uint32_t)(period / 12);
    ltime->month = (uint32_t)(period % 12L) + 1;
    /* Adjust day if the new month doesn't have enough days */
    if (ltime->day > days_in_month[ltime->month - 1])
    {
      ltime->day = days_in_month[ltime->month - 1];
      if (ltime->month == 2 && calc_days_in_year(ltime->year) == 366)
        ltime->day++; // Leap-year
    }
    break;
  default:
    goto null_date;
  }

  return false; // Ok

invalid_date:
  log("%s datetime overflow", __FUNCTION__);
null_date:
  return true;
}

/*
    Convert a time string to a MY_TIME struct.

    SYNOPSIS
    str_to_time()
    str                  A string in full TIMESTAMP format or
                         [-] DAYS [H]H:MM:SS, [H]H:MM:SS, [M]M:SS, [H]HMMSS,
                         [M]MSS or [S]S
                         There may be an optional [.second_part] after seconds
    length               Length of str
    l_time               Store result here
    status               Conversion status

    status.warning is set to:
      MY_TIME_WARN_TRUNCATED flag if the input string
                        was cut during conversion, and/or
      MY_TIME_WARN_OUT_OF_RANGE flag, if the value is out of range.

    NOTES
      Because of the extra days argument, this function can only
      work with times where the time arguments are in the above order.

    @retval false on success, true on error
  */
bool str_to_time(const char *str, size_t length,
                 MY_TIME *l_time,
                 my_time_flags_t flags,
                 MY_TIME_STATUS *status)
{
  uint32_t date[5];
  uint64_t value;
  const char *end = str + length, *end_of_days;
  bool found_days, found_hours;
  uint32_t state;

  my_time_status_init(status);
  l_time->neg = 0;
  for (; str != end && isspace(*str); str++)
    length--;
  if (str != end && *str == '-')
  {
    l_time->neg = 1;
    str++;
    length--;
  }
  if (str == end)
    return true;

  /* Check first if this is a full TIMESTAMP */
  if (length >= 12)
  {
    /* Probably full timestamp */
    (void)str_to_datetime(str, length, l_time,
                          (TIME_FUZZY_DATE | TIME_DATETIME_ONLY), status);
    if (l_time->time_type >= TIMESTAMP_ERROR)
      return l_time->time_type == TIMESTAMP_ERROR;
    my_time_status_init(status);
  }

  /* Not a timestamp. Try to get this as a DAYS_TO_SECOND string */
  for (value = 0; str != end && isdigit(*str); str++)
    value = value * 10L + (int32_t)(*str - '0');

  if (value > UINT_MAX)
    return true;

  /* Skip all space after 'days' */
  end_of_days = str;
  for (; str != end && isspace(str[0]); str++)
  {
  }

  state = 0;
  found_days = found_hours = 0;
  if ((uint32_t)(end - str) > 1 && str != end_of_days && isdigit(*str))
  {
    /* Found days part */
    date[0] = (uint32_t)value;
    state = 1; /* Assume next is hours */
    found_days = 1;
  }
  else if ((end - str) > 1 && *str == time_separator && isdigit(str[1]))
  {
    date[0] = 0; /* Assume we found hours */
    date[1] = (uint32_t)value;
    state = 2;
    found_hours = 1;
    str++; /* skip ':' */
  }
  else
  {
    /* String given as one number; assume HHMMSS format */
    date[0] = 0;
    date[1] = (uint32_t)(value / 10000);
    date[2] = (uint32_t)(value / 100 % 100);
    date[3] = (uint32_t)(value % 100);
    state = 4;
    goto fractional;
  }

  /* Read hours, minutes and seconds */
  for (;;)
  {
    for (value = 0; str != end && isdigit(*str); str++)
      value = value * 10L + (int32_t)(*str - '0');
    date[state++] = (uint32_t)value;
    if (state == 4 || (end - str) < 2 || *str != time_separator || !isdigit(str[1]))
      break;
    str++; /* Skip time_separator (':') */
  }

  if (state != 4)
  {
    /* Not HH:MM:SS */
    /* Fix the date to assume that seconds was given */
    if (!found_hours && !found_days)
    {
      size_t len = sizeof(int32_t) * (state - 1);
      memmove((unsigned char *)(date + 4) - len, (unsigned char *)(date + state) - len, len);
      memset(date, 0, sizeof(int32_t) * (4 - state));
    }
    else
      memset((date + state), 0, sizeof(int32_t) * (4 - state));
  }

fractional:
  /* Get fractional second part */
  if ((end - str) >= 2 && *str == '.' && isdigit(str[1]))
  {
    int field_length = 5;
    str++;
    value = (uint32_t)(unsigned char)(*str - '0');
    while (++str != end && isdigit(*str))
    {
      if (field_length-- > 0)
        value = value * 10 + (uint32_t)(unsigned char)(*str - '0');
    }
    if (field_length >= 0)
    {
      status->fractional_digits = DATETIME_MAX_DECIMALS - field_length;
      if (field_length > 0)
        value *= (int32_t)log_10_int[field_length];
    }
    else
    {
      /* Scan digits left after microseconds */
      status->fractional_digits = 6;
      status->nanoseconds = 100 * (int)(str[-1] - '0');
      for (; str != end && isdigit(*str); str++)
      {
      }
    }
    date[4] = (uint32_t)value;
  }
  else if ((end - str) == 1 && *str == '.')
  {
    str++;
    date[4] = 0;
  }
  else
    date[4] = 0;

  /* Check for exponent part: E<gigit> | E<sign><digit> */
  /* (may occur as result of %g formatting of time value) */
  if ((end - str) > 1 &&
      (*str == 'e' || *str == 'E') &&
      (isdigit(str[1]) ||
       ((str[1] == '-' || str[1] == '+') &&
        (end - str) > 2 &&
        isdigit(str[2]))))
    return true;

  if (internal_format_positions[7] != 255)
  {
    /* Read a possible AM/PM */
    while (str != end && isspace(*str))
      str++;
    if (str + 2 <= end && (str[1] == 'M' || str[1] == 'm'))
    {
      if (str[0] == 'p' || str[0] == 'P')
      {
        str += 2;
        date[1] = date[1] % 12 + 12;
      }
      else if (str[0] == 'a' || str[0] == 'A')
        str += 2;
    }
  }

  /* Integer overflow checks */
  if (date[0] > UINT_MAX || date[1] > UINT_MAX ||
      date[2] > UINT_MAX || date[3] > UINT_MAX ||
      date[4] > UINT_MAX)
    return true;

  l_time->year = 0; /* For protocol::store_time */
  l_time->month = 0;

  l_time->day = 0;
  l_time->hour = date[1] + date[0] * 24; /* Mix days and hours */

  l_time->minute = date[2];
  l_time->second = date[3];
  l_time->second_part = date[4];
  l_time->time_type = TIMESTAMP_TIME;

  if (check_time_mmssff_range(l_time))
  {
    status->warnings |= MY_TIME_WARN_OUT_OF_RANGE;
    return true;
  }

  /* Adjust the value into supported MY_TIME range */
  adjust_time_range(l_time, &status->warnings);

  /* Check if there is garbage at end of the MY_TIME specification */
  if (str != end)
  {
    do
    {
      if (!isspace(*str))
      {
        status->warnings |= MY_TIME_WARN_TRUNCATED;
        break;
      }
    } while (++str != end);
  }

  if (!(flags & TIME_NO_NSEC_ROUNDING))
  {
    time_add_nanoseconds_with_round(l_time, status->nanoseconds,
                                    &status->warnings);
  }

  return false;
}

/*
    Calculate nr of day since year 0 in new date-system

    SYNOPSIS
    calc_daynr()
    year		 Year (exact 4 digit year, no year conversions)
    month		 Month
    day			 Day
    
    NOTES: 0000-00-00 is a valid date, and will return 0

    RETURN
    Days since 0000-00-00
  */

int32_t calc_daynr(uint32_t year, uint32_t month, uint32_t day)
{
  int32_t delsum;
  int temp;
  int y = (int)year; /* may be < 0 temporarily */

  if (y == 0 && month == 0)
    return 0; /* Skip errors */

  /* Cast to int to be able to handle month == 0 */
  delsum = (int32_t)(365 * y + 31 * ((int)month - 1) + (int)day);
  if (month <= 2)
    y--;
  else
    delsum -= (int32_t)((int)month * 4 + 23) / 10;
  temp = (int)((y / 100 + 1) * 3) / 4;
  //Debug(g_time_log, "Exit, year: %d  month: %d  day: %d -> daynr: %ld",
  //	  y+(month <= 2),month,day,delsum+y/4-temp);
  assert(delsum + (int)y / 4 - temp >= 0);
  return (delsum + (int)y / 4 - temp);
} /* calc_daynr */

/* Change a daynr to year, month and day */
/* Daynr 0 is returned as date 00.00.00 */
void get_date_from_daynr(int32_t daynr, uint32_t *ret_year, uint32_t *ret_month,
                         uint32_t *ret_day)
{
  uint32_t year, temp, leap_day, day_of_year, days_in_year;
  unsigned char *month_pos;

  if (daynr <= 365L || daynr >= 3652500)
  {
    /* Fix if wrong daynr */
    *ret_year = *ret_month = *ret_day = 0;
  }
  else
  {
    year = (uint32_t)(daynr * 100 / 36525L);
    temp = (((year - 1) / 100 + 1) * 3) / 4;
    day_of_year = (uint32_t)(daynr - (int32_t)year * 365L) - (year - 1) / 4 + temp;
    while (day_of_year > (days_in_year = calc_days_in_year(year)))
    {
      day_of_year -= days_in_year;
      (year)++;
    }
    leap_day = 0;
    if (days_in_year == 366)
    {
      if (day_of_year > 31 + 28)
      {
        day_of_year--;
        if (day_of_year == 31 + 28)
          leap_day = 1; /* Handle leapyears leapday */
      }
    }
    *ret_month = 1;
    for (month_pos = days_in_month;
         day_of_year > (uint32_t)*month_pos;
         day_of_year -= *(month_pos++), (*ret_month)++)
    {
    }
    *ret_year = year;
    *ret_day = day_of_year + leap_day;
  }

  return;
}

/**
     Print the microsecond part: ".NNN"
     @param to        OUT The string pointer to print at
     @param useconds      The microseconds value.
     @param dec           Precision, between 1 and 6.
     @return              The length of the result string.
  */
static inline int
useconds_to_str(char *to, uint32_t useconds, uint32_t dec)
{
  assert(dec <= DATETIME_MAX_DECIMALS);
  return sprintf(to, ".%0*u", (int)dec,
                 useconds / (uint32_t)log_10_int[DATETIME_MAX_DECIMALS - dec]);
}

/*
    Functions to convert time/date/datetime value to a string,
    using default format.
    This functions don't check that given MY_TIME structure members are
    in valid range. If they are not, return value won't reflect any
    valid date either. Additionally, make_time doesn't take into
    account time->day member: it's assumed that days have been converted
    to hours already.

    RETURN
    number of characters written to 'to'
  */
int time_to_str(const MY_TIME *l_time, char *to, uint32_t dec)
{
  uint32_t extra_hours = 0;
  int len = sprintf(to, "%s%02u:%02u:%02u", (l_time->neg ? "-" : ""),
                    extra_hours + l_time->hour, l_time->minute, l_time->second);
  if (dec)
    len += useconds_to_str(to + len, l_time->second_part, dec);
  return len;
}

int date_to_str(const MY_TIME *l_time, char *to)
{
  return sprintf(to, "%04u-%02u-%02u",
                 l_time->year, l_time->month, l_time->day);
}

/*
    Convert datetime to a string 'YYYY-MM-DD hh:mm:ss'.

    @param  to     OUT  The string pointer to print at.
    @param  ltime       The MY_TIME value.
    @return             The length of the result string.
  */
static inline int TIME_to_datetime_str(char *to, const MY_TIME *ltime)
{
  uint32_t temp, temp2;
  /* Year */
  temp = ltime->year / 100;
  *to++ = (char)('0' + temp / 10);
  *to++ = (char)('0' + temp % 10);
  temp = ltime->year % 100;
  *to++ = (char)('0' + temp / 10);
  *to++ = (char)('0' + temp % 10);
  *to++ = '-';
  /* Month */
  temp = ltime->month;
  temp2 = temp / 10;
  temp = temp - temp2 * 10;
  *to++ = (char)('0' + (char)(temp2));
  *to++ = (char)('0' + (char)(temp));
  *to++ = '-';
  /* Day */
  temp = ltime->day;
  temp2 = temp / 10;
  temp = temp - temp2 * 10;
  *to++ = (char)('0' + (char)(temp2));
  *to++ = (char)('0' + (char)(temp));
  *to++ = ' ';
  /* Hour */
  temp = ltime->hour;
  temp2 = temp / 10;
  temp = temp - temp2 * 10;
  *to++ = (char)('0' + (char)(temp2));
  *to++ = (char)('0' + (char)(temp));
  *to++ = ':';
  /* Minute */
  temp = ltime->minute;
  temp2 = temp / 10;
  temp = temp - temp2 * 10;
  *to++ = (char)('0' + (char)(temp2));
  *to++ = (char)('0' + (char)(temp));
  *to++ = ':';
  /* Second */
  temp = ltime->second;
  temp2 = temp / 10;
  temp = temp - temp2 * 10;
  *to++ = (char)('0' + (char)(temp2));
  *to++ = (char)('0' + (char)(temp));
  return 19;
}

/**
     Print a datetime value with an optional fractional part.

     @l_time       The MY_TIME value to print.
     @to      OUT  The string pointer to print at.
     @return       The length of the result string.  
  */
int datetime_to_str(const MY_TIME *l_time, char *to, uint32_t dec)
{
  int len = TIME_to_datetime_str(to, l_time);
  if (dec)
    len += useconds_to_str(to + len, l_time->second_part, dec);
  else
    to[len] = '\0';
  return len;
}

/*
    Convert struct DATE/TIME/DATETIME value to string using built-in
    time conversion formats.

    NOTE
    The string must have at least MAX_DATE_STRING_REP_LENGTH bytes reserved.
  */

int TIME_to_str(const MY_TIME *l_time, char *to, uint32_t dec)
{
  switch (l_time->time_type)
  {
  case TIMESTAMP_DATETIME:
    return datetime_to_str(l_time, to, dec);
  case TIMESTAMP_DATE:
    return date_to_str(l_time, to);
  case TIMESTAMP_TIME:
    return time_to_str(l_time, to, dec);
  case TIMESTAMP_NONE:
  case TIMESTAMP_ERROR:
    to[0] = '\0';
    log("time_type %s",
        l_time->time_type == TIMESTAMP_NONE ? "NONE" : "ERROR");
    return 0;
  default:
    assert(0);
    return 0;
  }
}

/**
     Print a timestamp with an oprional fractional part: XXXXX[.YYYYY]

     @param      tm  The timestamp value to print.
     @param  OUT to  The string pointer to print at. 
     @param      dec Precision, in the range 0..6.
     @return         The length of the result string.
  */
int timeval_to_str(const struct timeval *tm, char *to, uint32_t dec)
{
  int len = sprintf(to, "%d", (int)tm->tv_sec);
  if (dec)
    len += useconds_to_str(to + len, tm->tv_usec, dec);
  return len;
}

/**
     Check if TIME fields are fatally bad and cannot be further adjusted.
     @param ltime  Time value.
     @retval  TRUE   if the value is fatally bad.
     @retval  FALSE  if the value is Ok.
  */
bool check_time_mmssff_range(const MY_TIME *ltime)
{
  return ltime->minute >= 60 || ltime->second >= 60 ||
         ltime->second_part > 999999;
}

/**
     Adjust 'time' value to lie in the MY_TIME range.
     If the time value lies outside of the range [-838:59:59, 838:59:59],
     set it to the closest endpoint of the range and set
     MY_TIME_WARN_OUT_OF_RANGE flag in the 'warning' variable.

     @param  time     pointer to MY_TIME value
     @param  warning  set MY_TIME_WARN_OUT_OF_RANGE flag if the value is out of range
  */
void adjust_time_range(MY_TIME *my_time, int *warning)
{
  assert(!check_time_mmssff_range(my_time));
  if (check_time_range_quick(my_time))
  {
    my_time->day = my_time->second_part = 0;
    set_max_hhmmss(my_time);
    *warning |= MY_TIME_WARN_OUT_OF_RANGE;
  }
}

/**
     Check TIME range. The value can include day part,
     for example:  '1 10:20:30.123456'.

     minute, second and second_part values are not checked
     unless hour is equal TIME_MAX_HOUR.

     @param ltime   Rime value.
     @returns       Test result.
     @retval        FALSE if value is Ok.
     @retval        TRUE if value is out of range. 
  */
bool check_time_range_quick(const MY_TIME *ltime)
{
  int64_t hour = (int64_t)ltime->hour + 24LL * ltime->day;
  /* The input value should not be fatally bad */
  assert(!check_time_mmssff_range(ltime));
  if (hour <= TIME_MAX_HOUR &&
      (hour != TIME_MAX_HOUR || ltime->minute != TIME_MAX_MINUTE ||
       ltime->second != TIME_MAX_SECOND || !ltime->second_part))
    return false;
  return true;
}

/**
     Add nanoseconds to a time value with rounding.

     @param IN/OUT ltime       MY_TIME variable to add to.
     @param        nanosecons  Nanosecons value.
     @param IN/OUT warnings    Warning flag vector.
     @retval                   False on success, true on error.
  */
bool time_add_nanoseconds_with_round(MY_TIME *ltime,
                                     uint32_t nanoseconds, int *warnings)
{
  /* We expect correct input data */
  assert(nanoseconds < 1000000000);
  assert(!check_time_mmssff_range(ltime));

  if (nanoseconds < 500)
    return false;

  ltime->second_part += (nanoseconds + 500) / 1000;
  if (ltime->second_part < 1000000)
    goto ret;

  ltime->second_part %= 1000000;
  if (ltime->second < 59)
  {
    ltime->second++;
    goto ret;
  }

  ltime->second = 0;
  if (ltime->minute < 59)
  {
    ltime->minute++;
    goto ret;
  }
  ltime->minute = 0;
  ltime->hour++;

ret:
  /*
      We can get '838:59:59.000001' at this point, which
      is bigger than the maximum possible value '838:59:59.000000'.
      Checking only "hour > 838" is not enough.
      Do full adjust_time_range().
    */
  adjust_time_range(ltime, warnings);
  return false;
}

/**
     Set hour, minute and second of a MY_TIME variable to maximum time value.
     Unlike set_max_time(), does not touch the other structure members.
  */
void set_max_hhmmss(MY_TIME *tm)
{
  tm->hour = TIME_MAX_HOUR;
  tm->minute = TIME_MAX_MINUTE;
  tm->second = TIME_MAX_SECOND;
}

/**
     Set MY_TIME variable to maximum time value
     @param tm    OUT  The variable to set.
     @param neg        Sign: 1 if negative, 0 if positive.
  */
void set_max_time(MY_TIME *tm, bool neg)
{
  set_zero_time(tm, TIMESTAMP_TIME);
  set_max_hhmmss(tm);
  tm->neg = neg;
}

/**
     Set hour, minute and secondr from a number
     @param ltime    MY_TIME variable
     @param hhmmss   Number in HHMMSS format
  */
void TIME_set_hhmmss(MY_TIME *ltime, uint32_t hhmmss)
{
  ltime->second = (int)(hhmmss % 100);
  ltime->minute = (int)(hhmmss / 100) % 100;
  ltime->hour = (int)(hhmmss / 10000);
}

/*
    Convert datetime value specified as number to broken-down TIME
    representation and form value of DATETIME type as side-effect.

    SYNOPSIS
    number_to_datetime()
      nr         - datetime value as number
      time_res   - pointer for structure for broken-down representation
      flags      - flags to use in validating date, as in str_to_datetime()
      was_cut    0      Value ok
                 1      If value was cut during conversion
                 2      check_date(date,flags) considers date invalid

    DESCRIPTION
    Convert a datetime value of formats YYMMDD, YYYYMMDD, YYMMDDHHMSS,
    YYYYMMDDHHMMSS to broken-down MYSQL_TIME representation. Return value in
    YYYYMMDDHHMMSS format as side-effect.

    This function also checks if datetime value fits in DATETIME range.

    RETURN VALUE
    -1              Timestamp with wrong values
    anything else   DATETIME as integer in YYYYMMDDHHMMSS format
    Datetime value in YYYYMMDDHHMMSS format.

    was_cut         if return value -1: one of
                      - MY_TIME_WARN_OUT_OF_RANGE
                      - MY_TIME_WARN_ZERO_DATE
                      - MY_TIME_WARN_TRUNCATED
                    otherwise 0.
  */

int64_t number_to_datetime(int64_t nr, MY_TIME *time_res,
                           my_time_flags_t flags, int *was_cut)
{
  int32_t part1, part2;

  *was_cut = 0;
  memset(time_res, 0, sizeof(*time_res));
  time_res->time_type = TIMESTAMP_DATE;

  if (nr == 0LL || nr >= 10000101000000LL)
  {
    time_res->time_type = TIMESTAMP_DATETIME;
    if (nr > 99999999999999LL)
    {
      /* 9999-99-99 99:99:99 */
      *was_cut = MY_TIME_WARN_OUT_OF_RANGE;
      return -1LL;
    }
    goto ok;
  }
  if (nr < 101)
    goto err;
  if (nr <= (YY_PART_YEAR - 1) * 10000L + 1231L)
  {
    nr = (nr + 20000000L) * 1000000L; /* YYMMDD, year: 2000-2069 */
    goto ok;
  }
  if (nr < (YY_PART_YEAR)*10000L + 101L)
    goto err;
  if (nr <= 991231L)
  {
    nr = (nr + 19000000L) * 1000000L; /* YYMMDD, year: 1970-1999 */
    goto ok;
  }
  /*
      Though officially we support DATE values from 1000-01-01 only, one can
      easily insert a value like 1-1-1. So, for consistency reasons such dates
      are allowed when TIME_FUZZY_DATE is set.
    */
  if (nr < 10000101L && !(flags & TIME_FUZZY_DATE))
    goto err;
  if (nr <= 99991231L)
  {
    nr = nr * 1000000L;
    goto ok;
  }
  if (nr < 101000000L)
    goto err;

  time_res->time_type = TIMESTAMP_DATETIME;

  if (nr <= (YY_PART_YEAR - 1) * 10000000000LL + 1231235959LL)
  {
    nr = nr + 20000000000000LL; /* YYMMDDHHMMSS, 2000-2069 */
    goto ok;
  }
  if (nr < YY_PART_YEAR * 10000000000LL + 101000000LL)
    goto err;
  if (nr <= 991231235959LL)
    nr = nr + 19000000000000LL; /* YYMMDDHHMMSS, 1970-1999 */

ok:
  part1 = (int32_t)(nr / 1000000LL);
  part2 = (int32_t)(nr - (int64_t)part1 * 1000000LL);
  time_res->year = (int)(part1 / 10000L);
  part1 %= 10000L;
  time_res->month = (int)part1 / 100;
  time_res->day = (int)part1 % 100;
  time_res->hour = (int)(part2 / 10000L);
  part2 %= 10000L;
  time_res->minute = (int)part2 / 100;
  time_res->second = (int)part2 % 100;

  if (!check_datetime_range(time_res) &&
      !check_date(time_res, (nr != 0), flags, was_cut))
    return nr;

  /* Don't want to have was_cut get set if TIME_NO_ZERO_DATE was violated. */
  if (!nr && (flags & TIME_NO_ZERO_DATE))
    return -1LL;

err:
  *was_cut = MY_TIME_WARN_TRUNCATED;
  return -1LL;
}

/**
     Convert number to TIME
     @param nr            Number to convert.
     @param OUT ltime     Variable to convert to.
     @param OUT warnings  Warning vector.

     @retval false OK
     @retval true No. is out of range
  */
bool number_to_time(int64_t nr, MY_TIME *ltime, int *warnings)
{
  if (nr > TIME_MAX_VALUE)
  {
    /* For huge numbers try full DATETIME, like str_to_time does. */
    if (nr >= 10000000000LL)
    {
      /* '0001-00-00 00-00-00' */
      int warnings_backup = *warnings;
      if (number_to_datetime(nr, ltime, 0, warnings) != -1LL)
        return false;
      *warnings = warnings_backup;
    }
    set_max_time(ltime, 0);
    *warnings |= MY_TIME_WARN_OUT_OF_RANGE;
    return true;
  }
  else if (nr < -TIME_MAX_VALUE)
  {
    set_max_time(ltime, 1);
    *warnings |= MY_TIME_WARN_OUT_OF_RANGE;
    return true;
  }
  if ((ltime->neg = (nr < 0)))
    nr = -nr;
  if (nr % 100 >= 60 || nr / 100 % 100 >= 60)
  {
    /* Check hours and minutes */
    set_zero_time(ltime, TIMESTAMP_TIME);
    *warnings |= MY_TIME_WARN_OUT_OF_RANGE;
    return true;
  }
  ltime->time_type = TIMESTAMP_TIME;
  ltime->year = ltime->month = ltime->day = 0;
  TIME_set_hhmmss(ltime, (uint32_t)nr);
  ltime->second_part = 0;
  return false;
}

/**
     Convert lldiv_t to datetime.

     @param         lld      The value to convert from.
     @param[out]    ltime    The variable to convert to.
     @param         flags    Conversion flags.
     @param[in,out] warnings Warning flags.
     @return                False on success, true on error.
  */
static bool lldiv_t_to_datetime(lldiv_t lld, MY_TIME *ltime,
                                my_time_flags_t flags, int *warnings)
{
  if (lld.rem < 0 || // Catch negative numbers with zero int part, e.g: -0.1
      number_to_datetime(lld.quot, ltime, flags, warnings) == -1LL)
  {
    /* number_to_datetime does not clear ltime in case of ZERO DATE */
    set_zero_time(ltime, TIMESTAMP_ERROR);
    if (!*warnings) /* Neither sets warnings in case of ZERO DATE */
      *warnings |= MY_TIME_WARN_TRUNCATED;
    return true;
  }
  else if (ltime->time_type == TIMESTAMP_DATE)
  {
    /*
	Generate a warning in case of DATE with fractional part:
        20011231.1234 -> '2001-12-31'
	unless the caller does not want the warning: for example, CAST does.
      */
    if (lld.rem && !(flags & TIME_NO_DATE_FRAC_WARN))
      *warnings |= MY_TIME_WARN_TRUNCATED;
  }
  else if (!(flags & TIME_NO_NSEC_ROUNDING))
  {
    ltime->second_part = static_cast<uint32_t>(lld.rem / 1000);
    return datetime_add_nanoseconds_with_round(ltime, lld.rem % 1000, warnings);
  }
  return false;
}

/**
     Convert lldiv_t value to time with nanosecond rounding.

     @param         lld      The value to convert from.
     @param[out]    ltime    The variable to convert to,
     @param         flags    Conversion flags.
     @param[in,out] warnings Warning flags.
     @return                 False on success, true on error.
  */
static bool lldiv_t_to_time(lldiv_t lld, MY_TIME *ltime, int *warnings)
{
  if (number_to_time(lld.quot, ltime, warnings))
    return true;
  /*
      Both lld.quot and lld.rem can give negative result value,
      thus combine them using "|=".
    */
  if ((ltime->neg |= (lld.rem < 0)))
    lld.rem = -lld.rem;
  ltime->second_part = static_cast<uint32_t>(lld.rem / 1000);
  return time_add_nanoseconds_with_round(ltime, lld.rem % 1000, warnings);
}

/**
     Convert decimal value to datetime value with a warning.
     @param       decimal The value to convert from.
     @param[out]  ltime   The variable to convert to.
     @param       flags   Conversion flags.
     @return      False on success, true on error.
  */
bool my_decimal_to_datetime(const my_decimal *decimal, MY_TIME *ltime,
                            my_time_flags_t flags,
                            int *warnings)
{
  lldiv_t lld;
  bool rc;

  *warnings = 0;
  if ((rc = my_decimal2lldiv_t(0, decimal, &lld)))
  {
    *warnings = MY_TIME_WARN_TRUNCATED;
    set_zero_time(ltime, TIMESTAMP_NONE);
  }
  else
    rc = lldiv_t_to_datetime(lld, ltime, flags, warnings);

  if (*warnings)
  {
    char dbuf[DECIMAL_MAX_STR_LENGTH + 1];
    log("decimal: %s, to datetime type[%d]",
         dbug_decimal_as_string(dbuf, decimal), ltime->time_type);
  }
  return rc;
}

/**
     Convert decimal number to TIME
     @param      decimal_value  The number to convert from.
     @param[out] ltime          The variable to convert to.
     @return     False on success, true on error.
  */
bool my_decimal_to_time(const my_decimal *decimal, MY_TIME *ltime, int *warnings)
{
  lldiv_t lld;
  bool rc;

  *warnings = 0;
  if ((rc = my_decimal2lldiv_t(0, decimal, &lld)))
  {
    *warnings = MY_TIME_WARN_TRUNCATED;
    set_zero_time(ltime, TIMESTAMP_TIME);
  }
  else
    rc = lldiv_t_to_time(lld, ltime, warnings);

  if (*warnings)
  {
    char dbuf[DECIMAL_MAX_STR_LENGTH + 1];
    log("decimal: %s, to datetime type[%d]",
         dbug_decimal_as_string(dbuf, decimal), ltime->time_type);
  }
  return rc;
}

/**
     Convert double value to datetime value with a warning.
     @param       nr      The value to convert from.
     @param[out]  ltime   The variable to convert to.
     @param       flags   Conversion flags.
     @return              False on success, true on error.
  */
bool my_double_to_datetime_with_warn(double nr, MY_TIME *ltime, my_time_flags_t flags)
{
  lldiv_t lld;
  int warnings = 0;
  bool rc;

  if ((rc = (double2lldiv_t(nr, &lld) != E_DEC_OK)))
  {
    warnings |= MY_TIME_WARN_TRUNCATED;
    set_zero_time(ltime, TIMESTAMP_NONE);
  }
  else
    rc = lldiv_t_to_datetime(lld, ltime, flags, &warnings);

  if (warnings)
  {
    //make_truncated_value_warning(ErrConvString(nr), ltime->time_type);
  }
  return rc;
}

/**
     Convert double number to TIME

     @param      nr      The number to convert from.
     @param[out] ltime   The variable to convert to.
     @return     False on success, true on error.
  */
bool my_double_to_time_with_warn(double nr, MY_TIME *ltime)
{
  lldiv_t lld;
  int warnings = 0;
  bool rc;

  if ((rc = (double2lldiv_t(nr, &lld) != E_DEC_OK)))
  {
    warnings |= MY_TIME_WARN_TRUNCATED;
    set_zero_time(ltime, TIMESTAMP_TIME);
  }
  else
    rc = lldiv_t_to_time(lld, ltime, &warnings);

  if (warnings)
  {
    // make_truncated_value_warning(ErrConvString(nr), TIMESTAMP_TIME);
  }
  return rc;
}

/**
     Convert time value to integer in YYYYMMDDHHMMSS.
     @param  my_time  The MY_TIME value to convert.
     @return          A number in format YYYYMMDDHHMMSS.
  */
uint64_t TIME_to_uint64_datetime(const MY_TIME *my_time)
{
  return ((uint64_t)(my_time->year * 10000UL +
                     my_time->month * 100UL +
                     my_time->day) *
              1000000ULL +
          (uint64_t)(my_time->hour * 10000UL +
                     my_time->minute * 100UL +
                     my_time->second));
}

/**
     Convert MY_TIME value to integer in YYYYMMDD format
     @param my_time  The MY_TIME value to convert.
     @return         A number in format YYYYMMDD.
  */
uint64_t TIME_to_uint64_date(const MY_TIME *my_time)
{
  return (uint64_t)(my_time->year * 10000UL +
                    my_time->month * 100UL +
                    my_time->day);
}

/**
     Convert MY_TIME value to integer in HHMMSS format.
     This function doesn't take into account time->day member:
     it's assumed that days have been converted to hours already.
     @param my_time  The TIME value to convert.
     @return         The number in HHMMSS format.
  */
uint64_t TIME_to_uint64_time(const MY_TIME *my_time)
{
  return (uint64_t)(my_time->hour * 10000UL +
                    my_time->minute * 100UL +
                    my_time->second);
}

/* Rounding functions */
static uint32_t msec_round_add[7] =
    {
        500000000,
        50000000,
        5000000,
        500000,
        50000,
        5000,
        0};

/*
   * Round time value to the given precision.
   * @param IN/OUT ltime The value to round.
   * @param        dec   Precision
   * @return       False on success, true on error.
   */
bool my_time_round(MY_TIME *ltime, uint32_t dec)
{
  int warnings = 0;
  assert(dec <= DATETIME_MAX_DECIMALS);
  /* Add half away from zero */
  bool rc = time_add_nanoseconds_with_round(ltime, msec_round_add[dec], &warnings);
  /* Truncate non-significant digits */
  my_time_trunc(ltime, dec);
  return rc;
}

uint64_t TIME_to_uint64_time_round(const MY_TIME *ltime)
{
  if (ltime->second_part < 500000)
    return TIME_to_uint64_time(ltime);
  if (ltime->second < 59)
    return TIME_to_uint64_time(ltime) + 1;
  // corner case e.g 'hh::mm::59.5'.
  MY_TIME tmp = *ltime;
  my_time_round(&tmp, 0);
  return TIME_to_uint64_time(&tmp);
}

bool my_datetime_round(MY_TIME *ltime, uint32_t dec, int *warnings)
{
  assert(dec <= DATETIME_MAX_DECIMALS);
  /* add half away from zero */
  bool rc = datetime_add_nanoseconds_with_round(ltime, msec_round_add[dec], warnings);
  /* Truncate non-significant digits */
  my_time_trunc(ltime, dec);
  return rc;
}

uint64_t TIME_to_uint64_datetime_round(const MY_TIME *ltime)
{
  if (ltime->second_part < 500000)
    return TIME_to_uint64_datetime(ltime);
  if (ltime->second < 59)
    return TIME_to_uint64_datetime(ltime) + 1;
  // Corner case e.g. 'YYYY-MM-DD hh:mm:59.5'. Proceed with slower method.
  int warnings = 0;
  MY_TIME tmp = *ltime;
  my_datetime_round(&tmp, 0, &warnings);
  return TIME_to_uint64_datetime(&tmp); // + TIME_microseconds_round(ltime);
}

/**
     Convert time value to my_decimal in format hhmmss.ffffff
     @param ltime  Date value to convert from.
     @param dec    Decimal value to convert to.
  */
my_decimal *time2my_decimal(const MY_TIME *ltime, my_decimal *dec)
{
  lldiv_t lld;
  lld.quot = TIME_to_uint64_time(ltime);
  lld.rem = (int64_t)ltime->second_part * 1000;
  return lldiv_t2my_decimal(&lld, ltime->neg, dec);
}

/**
     Convert datetime value to my_decimal in format YYYYMMDDhhmmss.ffffff
     @param ltime  Date value to convert from.
     @param dec    Decimal value to convert to.
  */
my_decimal *date2my_decimal(const MY_TIME *ltime, my_decimal *dec)
{
  lldiv_t lld;
  lld.quot = ltime->time_type > TIMESTAMP_DATE ? TIME_to_uint64_datetime(ltime) : TIME_to_uint64_date(ltime);
  lld.rem = (int64_t)ltime->second_part * 1000;
  return lldiv_t2my_decimal(&lld, ltime->neg, dec);
}

my_decimal *timeval2my_decimal(const struct timeval *tm, my_decimal *dec)
{
  lldiv_t lld;
  lld.quot = tm->tv_sec;
  lld.rem = (int64_t)tm->tv_usec * 1000;
  return lldiv_t2my_decimal(&lld, 0, dec);
}

/*** DATETIME and DATE low-level memory and disk representation routines ***/

/*
    1 bit  sign            (used when on disk)
   17 bits year*13+month   (year 0-9999, month 0-12)
    5 bits day             (0-31)
    5 bits hour            (0-23)
    6 bits minute          (0-59)
    6 bits second          (0-59)
   24 bits microseconds    (0-999999)

   Total: 64 bits = 8 bytes

   SYYYYYYY.YYYYYYYY.YYdddddh.hhhhmmmm.mmssssss.ffffffff.ffffffff.ffffffff
  */

/**
     Convert datetime to packed numeric datetime representation.
     @param ltime  The value to convert.
     @return       Packed numeric representation of ltime.
  */
int64_t TIME_to_longlong_datetime_packed(const MY_TIME *ltime)
{
  int64_t ymd = ((ltime->year * 13 + ltime->month) << 5) | ltime->day;
  int64_t hms = (ltime->hour << 12) | (ltime->minute << 6) | ltime->second;
  int64_t tmp = MY_PACKED_TIME_MAKE(((ymd << 17) | hms), ltime->second_part);
  // assert(!check_datetime_range(ltime)); /* Make sure no overflow */
  return ltime->neg ? -tmp : tmp;
}

/**
     Convert date to packed numeric date representation.
     Numeric packed date format is similar to numeric packed datetime
     representation, with zero hhmmss part.
  
     @param ltime The value to convert.
     @return      Packed numeric representation of ltime.
  */
int64_t TIME_to_longlong_date_packed(const MY_TIME *ltime)
{
  int64_t ymd = ((ltime->year * 13 + ltime->month) << 5) | ltime->day;
  return MY_PACKED_TIME_MAKE_INT(ymd << 17);
}

/**
     Convert time value to numeric packed representation.
  
     @param    ltime   The value to convert.
     @return           Numeric packed representation.
  */
int64_t TIME_to_longlong_time_packed(const MY_TIME *ltime)
{
  /* If month is 0, we mix day with hours: "1 00:10:10" -> "24:00:10" */
  int64_t hms = (((ltime->month ? 0 : ltime->day * 24) + ltime->hour) << 12) |
                (ltime->minute << 6) | ltime->second;
  int64_t tmp = MY_PACKED_TIME_MAKE(hms, ltime->second_part);
  return ltime->neg ? -tmp : tmp;
}

/**
     Convert a temporal value to packed numeric temporal representation,
     depending on its time_type.
     
     @ltime   The value to convert.
     @return  Packed numeric time/date/datetime representation.
  */
int64_t TIME_to_longlong_packed(const MY_TIME *ltime)
{
  switch (ltime->time_type)
  {
  case TIMESTAMP_DATE:
    return TIME_to_longlong_date_packed(ltime);
  case TIMESTAMP_DATETIME:
    return TIME_to_longlong_datetime_packed(ltime);
  case TIMESTAMP_TIME:
    return TIME_to_longlong_time_packed(ltime);
  case TIMESTAMP_NONE:
  case TIMESTAMP_ERROR:
    return 0;
  }
  assert(0);
  return 0;
}

void TIME_from_longlong_datetime_packed(MY_TIME *ltime, int64_t tmp)
{
  int64_t ymd, hms;
  int64_t ymdhms, ym;
  if ((ltime->neg = (tmp < 0)))
    tmp = -tmp;

  ltime->second_part = MY_PACKED_TIME_GET_FRAC_PART(tmp);
  ymdhms = MY_PACKED_TIME_GET_INT_PART(tmp);

  ymd = ymdhms >> 17;
  ym = ymd >> 5;
  hms = ymdhms % (1 << 17);

  ltime->day = ymd % (1 << 5);
  ltime->month = ym % 13;
  ltime->year = (uint32_t)(ym / 13);

  ltime->second = hms % (1 << 6);
  ltime->minute = (hms >> 6) % (1 << 6);
  ltime->hour = (uint32_t)(hms >> 12);

  ltime->time_type = TIMESTAMP_DATETIME;
}

void TIME_from_longlong_date_packed(MY_TIME *ltime, int64_t tmp)
{
  TIME_from_longlong_datetime_packed(ltime, tmp);
  ltime->time_type = TIMESTAMP_DATE;
}

void TIME_from_longlong_time_packed(MY_TIME *ltime, int64_t tmp)
{
  int64_t hms;
  if ((ltime->neg = (tmp < 0)))
    tmp = -tmp;
  hms = MY_PACKED_TIME_GET_INT_PART(tmp);
  ltime->year = (uint32_t)0;
  ltime->month = (uint32_t)0;
  ltime->day = (uint32_t)0;
  ltime->hour = (uint32_t)(hms >> 12) % (1 << 10); /* 10 bits starting at 12th */
  ltime->minute = (uint32_t)(hms >> 6) % (1 << 6); /* 6 bits starting at 6th   */
  ltime->second = (uint32_t)hms % (1 << 6);        /* 6 bits starting at 0th   */
  ltime->second_part = MY_PACKED_TIME_GET_FRAC_PART(tmp);
  ltime->time_type = TIMESTAMP_TIME;
}

/*
  On disk we store as unsigned number with DATETIMEF_INT_OFS offset,
  for HA_KETYPE_BINARY compatibilty purposes.
*/
#define DATETIMEF_INT_OFS 0x8000000000LL

int64_t my_datetime_packed_from_binary(const char *ptr, uint32_t dec)
{
  int64_t intpart = (int64_t)be_uint5korr(ptr) - DATETIMEF_INT_OFS;
  int frac;
  assert(dec <= DATETIME_MAX_DECIMALS);
  switch (dec)
  {
  case 0:
  default:
    return MY_PACKED_TIME_MAKE_INT(intpart);
  case 1:
  case 2:
    frac = ((int)(signed char)ptr[5]) * 10000;
    break;
  case 3:
  case 4:
    frac = be_sint2korr(ptr + 5) * 100;
    break;
  case 5:
  case 6:
    frac = be_sint3korr(ptr + 5);
    break;
  }
  return MY_PACKED_TIME_MAKE(intpart, frac);
}

void my_datetime_packed_to_binary(int64_t nr, char *ptr, uint32_t dec)
{
  assert(dec <= DATETIME_MAX_DECIMALS);
  /* The value being stored must have been properly rounded or truncated */

  be_int5store(ptr, MY_PACKED_TIME_GET_INT_PART(nr) + DATETIMEF_INT_OFS);
  switch (dec)
  {
  case 0:
  default:
    break;
  case 1:
  case 2:
    ptr[5] = (unsigned char)(char)(MY_PACKED_TIME_GET_FRAC_PART(nr) / 10000);
    break;
  case 3:
  case 4:
    be_int2store(ptr + 5, MY_PACKED_TIME_GET_FRAC_PART(nr) / 100);
    break;
  case 5:
  case 6:
    be_int3store(ptr + 5, MY_PACKED_TIME_GET_FRAC_PART(nr));
  }
}

/*
  On disk we convert from signed representation to unsigned
  representation using TIMEF_OFS, so all values become binary comparable.
*/
#define TIMEF_OFS 0x800000000000LL
#define TIMEF_INT_OFS 0x800000LL

void my_time_packed_to_binary(int64_t nr, char *ptr, uint32_t dec)
{
  assert(dec <= DATETIME_MAX_DECIMALS);
  /* Make sure the stored value was previously properly rounded or truncated */
  //assert((MY_PACKED_TIME_GET_FRAC_PART(nr) %
  //	    (int) log_10_int[DATETIME_MAX_DECIMALS - dec]) == 0);

  switch (dec)
  {
  case 0:
  default:
    be_int3store(ptr, TIMEF_INT_OFS + MY_PACKED_TIME_GET_INT_PART(nr));
    break;

  case 1:
  case 2:
    be_int3store(ptr, TIMEF_INT_OFS + MY_PACKED_TIME_GET_INT_PART(nr));
    ptr[3] = (unsigned char)(char)(MY_PACKED_TIME_GET_FRAC_PART(nr) / 10000);
    break;

  case 4:
  case 3:
    be_int3store(ptr, TIMEF_INT_OFS + MY_PACKED_TIME_GET_INT_PART(nr));
    be_int2store(ptr + 3, MY_PACKED_TIME_GET_FRAC_PART(nr) / 100);
    break;

  case 5:
  case 6:
    be_int6store(ptr, nr + TIMEF_OFS);
    break;
  }
}

int64_t my_time_packed_from_binary(const char *ptr, uint32_t dec)
{
  assert(dec <= DATETIME_MAX_DECIMALS);

  switch (dec)
  {
  case 0:
  default:
  {
    int64_t intpart = be_uint3korr(ptr) - TIMEF_INT_OFS;
    return MY_PACKED_TIME_MAKE_INT(intpart);
  }
  case 1:
  case 2:
  {
    int64_t intpart = be_uint3korr(ptr) - TIMEF_INT_OFS;
    int frac = (uint32_t)ptr[3];
    if (intpart < 0 && frac)
    {
      /*
          Negative values are stored with reverse fractional part order,
          for binary sort compatibility.

            Disk value  intpart frac   Time value   Memory value
            800000.00    0      0      00:00:00.00  0000000000.000000
            7FFFFF.FF   -1      255   -00:00:00.01  FFFFFFFFFF.FFD8F0
            7FFFFF.9D   -1      99    -00:00:00.99  FFFFFFFFFF.F0E4D0
            7FFFFF.00   -1      0     -00:00:01.00  FFFFFFFFFF.000000
            7FFFFE.FF   -1      255   -00:00:01.01  FFFFFFFFFE.FFD8F0
            7FFFFE.F6   -2      246   -00:00:01.10  FFFFFFFFFE.FE7960

            Formula to convert fractional part from disk format
            (now stored in "frac" variable) to absolute value: "0x100 - frac".
            To reconstruct in-memory value, we shift
            to the next integer value and then substruct fractional part.
        */
      intpart++;     /* Shift to the next integer value */
      frac -= 0x100; /* -(0x100 - frac) */
    }
    return MY_PACKED_TIME_MAKE(intpart, frac * 10000);
  }

  case 3:
  case 4:
  {
    int64_t intpart = be_uint3korr(ptr) - TIMEF_INT_OFS;
    int frac = be_uint2korr(ptr + 3);
    if (intpart < 0 && frac)
    {
      /*
          Fix reverse fractional part order: "0x10000 - frac".
          See comments for FSP=1 and FSP=2 above.
        */
      intpart++;       /* Shift to the next integer value */
      frac -= 0x10000; /* -(0x10000-frac) */
    }
    return MY_PACKED_TIME_MAKE(intpart, frac * 100);
  }

  case 5:
  case 6:
    return ((int64_t)be_uint6korr(ptr)) - TIMEF_OFS;
  }
}

void my_timestamp_to_binary(const struct timeval *tm, char *ptr, uint32_t dec)
{
  assert(dec <= DATETIME_MAX_DECIMALS);
  /* Stored value must have been previously properly rounded or truncated */
  //assert((tm->tv_usec %
  //	    (int) log_10_int[DATETIME_MAX_DECIMALS - dec]) == 0);
  be_int4store(ptr, tm->tv_sec);
  switch (dec)
  {
  case 0:
  default:
    break;
  case 1:
  case 2:
    ptr[4] = (unsigned char)(char)(tm->tv_usec / 10000);
    break;
  case 3:
  case 4:
    be_int2store(ptr + 4, tm->tv_usec / 100);
    break;
    /* Impossible second precision. Fall through */
  case 5:
  case 6:
    be_int3store(ptr + 4, tm->tv_usec);
  }
}

void my_timestamp_from_binary(struct timeval *tm, const char *ptr, uint32_t dec)
{
  assert(dec <= DATETIME_MAX_DECIMALS);
  tm->tv_sec = be_uint4korr(ptr);
  switch (dec)
  {
  case 0:
  default:
    tm->tv_usec = 0;
    break;
  case 1:
  case 2:
    tm->tv_usec = ((int)ptr[4]) * 10000;
    break;
  case 3:
  case 4:
    tm->tv_usec = be_sint2korr(ptr + 4) * 100;
    break;
  case 5:
  case 6:
    tm->tv_usec = be_sint3korr(ptr + 4);
  }
}

void localtime_to_time(MY_TIME *to, struct tm *from)
{
  to->neg = 0;
  to->second_part = 0;
  to->year = (int)((from->tm_year + 1900) % 10000);
  to->month = (int)from->tm_mon + 1;
  to->day = (int)from->tm_mday;
  to->hour = (int)from->tm_hour;
  to->minute = (int)from->tm_min;
  to->second = (int)from->tm_sec;
}

void calc_time_from_sec(MY_TIME *to, int64_t seconds, int32_t microseconds)
{
  int32_t t_seconds;
  // to->neg is not cleared, it may already be set to a useful value
  to->time_type = TIMESTAMP_TIME;
  to->year = 0;
  to->month = 0;
  to->day = 0;
  assert(seconds < (0xFFFFFFFFLL * 3600LL));
  to->hour = (int32_t)(seconds / 3600L);
  t_seconds = (int32_t)(seconds % 3600L);
  to->minute = t_seconds / 60L;
  to->second = t_seconds % 60L;
  to->second_part = microseconds;
}

void adjust_leap_second(MY_TIME *t)
{
  if (t->second == 60 || t->second == 61)
    t->second = 59;
}

/*
    Calculate difference between two datetime values as seconds + microseconds.

    SYNOPSIS
    calc_time_diff()
      l_time1         - TIME/DATE/DATETIME value
      l_time2         - TIME/DATE/DATETIME value
      l_sign          - 1 absolute values are substracted,
                        -1 absolute values are added.
      seconds_out     - Out parameter where difference between
                        l_time1 and l_time2 in seconds is stored.
      microseconds_out- Out parameter where microsecond part of difference
                        between l_time1 and l_time2 is stored.

    NOTE
    This function calculates difference between l_time1 and l_time2 absolute
    values. So one should set l_sign and correct result if he want to take
    signs into account (i.e. for MY_TIME values).

    RETURN VALUES
      Returns sign of difference.
      1 means negative result
      0 means positive result
  */

bool calc_time_diff(const MY_TIME *l_time1, const MY_TIME *l_time2,
                    int l_sign, int64_t *seconds_out, int32_t *microseconds_out)
{
  int32_t days;
  bool neg;
  int64_t microseconds;

  /*
      We suppose that if first argument is TIMESTAMP_TIME
      the second argument should be TIMESTAMP_TIME also.
      We should check it before calc_time_diff call.
    */
  if (l_time1->time_type == TIMESTAMP_TIME) // Time value
    days = (int32_t)l_time1->day - l_sign * (int32_t)l_time2->day;
  else
  {
    days = calc_daynr((uint32_t)l_time1->year,
                      (uint32_t)l_time1->month,
                      (uint32_t)l_time1->day);
    if (l_time2->time_type == TIMESTAMP_TIME)
      days -= l_sign * (int32_t)l_time2->day;
    else
      days -= l_sign * calc_daynr((uint32_t)l_time2->year,
                                  (uint32_t)l_time2->month,
                                  (uint32_t)l_time2->day);
  }

  microseconds = ((int64_t)days * SECONDS_IN_24H +
                  (int64_t)(l_time1->hour * 3600L +
                            l_time1->minute * 60L +
                            l_time1->second) -
                  l_sign * (int64_t)(l_time2->hour * 3600L +
                                     l_time2->minute * 60L +
                                     l_time2->second)) *
                     1000000LL +
                 (int64_t)l_time1->second_part - l_sign * (int64_t)l_time2->second_part;

  neg = 0;
  if (microseconds < 0)
  {
    microseconds = -microseconds;
    neg = 1;
  }
  *seconds_out = microseconds / 1000000L;
  *microseconds_out = (int32_t)(microseconds % 1000000L);
  return neg;
}

/**
     Mix a date value and a time value.

     @param  IN/OUT  ldate  Date value.
     @param          ltime  Time value.
  */
void mix_date_and_time(MY_TIME *ldate, const MY_TIME *ltime)
{
  assert(ldate->time_type == TIMESTAMP_DATE ||
         ldate->time_type == TIMESTAMP_DATETIME);

  if (!ltime->neg && ltime->hour < 24)
  {
    /*
	Simple case: TIME is within normal 24 hours internal.
	Mix DATE part of ltime2 and TIME part of ltime together.
      */
    ldate->hour = ltime->hour;
    ldate->minute = ltime->minute;
    ldate->second = ltime->second;
    ldate->second_part = ltime->second_part;
  }
  else
  {
    /* Complex case: TIME is negative or outside of 24 hours internal. */
    int64_t seconds;
    int32_t days, useconds;
    int sign = ltime->neg ? 1 : -1;
    ldate->neg = calc_time_diff(ldate, ltime, sign, &seconds, &useconds);
    assert(!ldate->neg);

    /*
	We pass current date to mix_date_and_time. If we want to use
	this function with arbitrary dates, this code will need
	to cover cases when ltime is negative and "ldate < -ltime".
      */
    assert(ldate->year > 0);

    days = (int32_t)(seconds / SECONDS_IN_24H);
    calc_time_from_sec(ldate, seconds % SECONDS_IN_24H, useconds);
    get_date_from_daynr(days, &ldate->year, &ldate->month, &ldate->day);
  }
  ldate->time_type = TIMESTAMP_DATETIME;
}

void time_to_datetime(const MY_TIME *ltime, MY_TIME *ltime2)
{
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  localtime_to_time(ltime2, &t);
  ltime2->time_type = TIMESTAMP_DATETIME;
  adjust_leap_second(ltime2);
  ltime2->hour = ltime2->minute = ltime2->second = ltime2->second_part = 0;
  ltime2->time_type = TIMESTAMP_DATE;
  mix_date_and_time(ltime2, ltime);
}

/*
    Convert a datetime from broken-down MY_TIME representation
    to corresponding TIMESTAMP value.

    @param t               - datetime in broken-down representation,
    @param in_dst_time_gap - pointer to bool which is set to true if t represents
                             value which doesn't exists (falls into the spring time-gap)
                             or to false otherwise.
    @return
    @retval  Number seconds in UTC since start of Unix Epoch corresponding to t.
    @retval  0 - t contains datetime value which is out of TIMESTAMP range.
   */
time_t TIME_to_timestamp(const MY_TIME *t, bool *in_dst_time_gap)
{
  time_t timestamp;
  struct tm tmp_tm;

  *in_dst_time_gap = 0;

  memset(&tmp_tm, 0, sizeof(tmp_tm));
  tmp_tm.tm_year = t->year - 1900;
  tmp_tm.tm_mon = t->month - 1;
  tmp_tm.tm_mday = t->day;
  tmp_tm.tm_hour = t->hour;
  tmp_tm.tm_min = t->minute;
  tmp_tm.tm_sec = t->second;

  timestamp = mktime(&tmp_tm);
  //timestamp = timegm(&tmp_tm);
  //timestamp = timelocal(&tmp_tm);

  if (timestamp != -1)
  {
    return timestamp;
  }

  /* If we are here we have range error. */
  return (0);
}

bool datetime_with_no_zero_in_date_to_timeval(const MY_TIME *ltime,
                                              struct timeval *tm,
                                              int *warnings)
{
  if (!ltime->month)
  {
    if (non_zero_time(ltime))
    {
      *warnings |= MY_TIME_WARN_TRUNCATED;
      return true;
    }
    tm->tv_sec = tm->tv_usec = 0;
    return false;
  }
  bool in_dst_time_gap;
  if (!(tm->tv_sec = TIME_to_timestamp(ltime, &in_dst_time_gap)))
  {
    /*
	Date was outside of the supported timestamp range.
	for example: '3001-01-01 00:00:00' or '1000-01-01 00:00:00'
       */
    *warnings = MY_TIME_WARN_OUT_OF_RANGE;
    return true;
  }
  else if (in_dst_time_gap)
  {
    /*
	date was fine but pointed to winter/summer time switch gap.
	In this case tm is set to the first second after gap.
	eg: '2003-03-30 02:30:00 MSK' -> '2003-03-30 03:00:00 MSK'
       */
    *warnings |= MY_TIME_WARN_INVALID_TIMESTAMP;
  }
  tm->tv_usec = ltime->second_part;
  return false;
}

bool datetime_to_timeval(const MY_TIME *ltime, struct timeval *tm, int *warnings)
{
  return check_date(ltime, non_zero_date(ltime), TIME_NO_ZERO_IN_DATE, warnings) ||
         datetime_with_no_zero_in_date_to_timeval(ltime, tm, warnings);
}

/*
    Converts time from localtime seconds since Epoch (time_t) representation
    to broken-down representation (also in localtime)

    SYNOPISI
    localtime_sec_to_TIME()
      tmp - pointer to MY_TIME structure to fill-in
      t   - time_t value to be converted
   */
void localtime_sec_to_TIME(MY_TIME *tmp, time_t t)
{
  struct tm tmp_tm;
  time_t tmp_t = t;
  localtime_r(&tmp_t, &tmp_tm);
  localtime_to_time(tmp, &tmp_tm);
  tmp->time_type = TIMESTAMP_DATETIME;
  adjust_leap_second(tmp);
}

void localtime_sec_to_TIME(MY_TIME *tmp, struct timeval tv)
{
  localtime_sec_to_TIME(tmp, (time_t)tv.tv_sec);
  tmp->second_part = tv.tv_usec;
}

} // namespace lf
