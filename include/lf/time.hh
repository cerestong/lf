#pragma once

#include <stdint.h>
#include "lf/logger.hh"
#include "lf/slice.hh"

namespace lf
{

extern uint64_t log_10_int[20];

#define DATETIME_MAX_DECIMALS 6

/* Conversion warnings */
#define MY_TIME_WARN_TRUNCATED 1
#define MY_TIME_WARN_OUT_OF_RANGE 2
#define MY_TIME_WARN_INVALID_TIMESTAMP 4
#define MY_TIME_WARN_ZERO_DATE 8
#define MY_TIME_NOTE_TRUNCATED 16
#define MY_TIME_WARN_ZERO_IN_DATE 32

/* two-digit years < this are 20..; >= this are 19.. */
#define YY_PART_YEAR 70

/* Usefull constants */
#define SECONDS_IN_24H 86400L

/* Limits for the TIME data type */
#define TIME_MAX_HOUR 838
#define TIME_MAX_MINUTE 59
#define TIME_MAX_SECOND 59
#define TIME_MAX_VALUE (TIME_MAX_HOUR * 10000 + TIME_MAX_MINUTE * 100 + \
                        TIME_MAX_SECOND)
#define TIME_MAX_VALUE_SECONDS (TIME_MAX_HOUR * 3600L + \
                                TIME_MAX_MINUTE * 60L + TIME_MAX_SECOND)

enum enum_timestamp_type
{
  TIMESTAMP_NONE = -2,
  TIMESTAMP_ERROR = -1,
  TIMESTAMP_DATE = 0,
  TIMESTAMP_DATETIME = 1,
  TIMESTAMP_TIME = 2
};

/* 
     Available interval types used in any statement.

     'interval_type' must be sorted so that simple intervals comes first,
     ie year, quarter, month, week, day, hour, etc. The order based on
     interval size is also important and the intervals should be kept in a
     large to smaller order. (get_interval_value() depends on this)
 
     Note: If you change the order of elements in this enum you should fix 
     order of elements in 'interval_type_to_name' and 'interval_names' 
     arrays 
  
     See also interval_type_to_name, get_interval_value, interval_names
  */

enum interval_type
{
  INTERVAL_YEAR,
  INTERVAL_QUARTER,
  INTERVAL_MONTH,
  INTERVAL_WEEK,
  INTERVAL_DAY,
  INTERVAL_HOUR,
  INTERVAL_MINUTE,
  INTERVAL_SECOND,
  INTERVAL_MICROSECOND,
  INTERVAL_YEAR_MONTH,
  INTERVAL_DAY_HOUR,
  INTERVAL_DAY_MINUTE,
  INTERVAL_DAY_SECOND,
  INTERVAL_HOUR_MINUTE,
  INTERVAL_HOUR_SECOND,
  INTERVAL_MINUTE_SECOND,
  INTERVAL_DAY_MICROSECOND,
  INTERVAL_HOUR_MICROSECOND,
  INTERVAL_MINUTE_MICROSECOND,
  INTERVAL_SECOND_MICROSECOND,
  INTERVAL_LAST
};

/*
    Structure which is used to represent datetime values

    assume that values in this structure are normalized, i.e. year <= 9999,
    month <= 12, day <= 31, hour <= 23, hour <= 59, hour <= 59.
*/
typedef struct st_my_time
{
  uint32_t year, month, day, hour, minute, second;
  uint32_t second_part; /**< microseconds */
  bool neg;
  enum enum_timestamp_type time_type;
} MY_TIME;

/* Flags to str_to_datetime and number_to_datetime */
typedef uint32_t my_time_flags_t;
static const my_time_flags_t TIME_FUZZY_DATE = 1;
static const my_time_flags_t TIME_DATETIME_ONLY = 2;
static const my_time_flags_t TIME_NO_NSEC_ROUNDING = 4;
static const my_time_flags_t TIME_NO_DATE_FRAC_WARN = 8;
static const my_time_flags_t TIME_NO_ZERO_IN_DATE = 16;
static const my_time_flags_t TIME_NO_ZERO_DATE = 32;
static const my_time_flags_t TIME_INVALID_DATES = 64;

/*
    Structure to return status from
    str_to_datetime(), str_to_time(), number_to_datetime(), number_to_time()
  */
typedef struct st_time_status
{
  int32_t warnings;
  uint32_t fractional_digits;
  uint32_t nanoseconds;
} MY_TIME_STATUS;

struct Interval
{
  uint32_t year, month, day, hour;
  uint64_t minute, second, second_part;
  bool neg;
};

void set_zero_time(MY_TIME *tm, enum enum_timestamp_type time_type);

static inline void my_time_status_init(MY_TIME_STATUS *status)
{
  status->warnings = status->fractional_digits = status->nanoseconds = 0;
}

static inline bool non_zero_date(const MY_TIME *ltime)
{
  return ltime->year || ltime->month || ltime->day;
}

static inline bool non_zero_time(const MY_TIME *ltime)
{
  return ltime->hour || ltime->minute || ltime->second || ltime->second_part;
}

/*
    @retval false on success, true on error
   */
bool check_date(const MY_TIME *ltime, bool not_zero_date,
                my_time_flags_t flags, int *was_cut);

/*
    @retval false on success, true on error
   */
bool str_to_datetime(const char *str, size_t length,
                     MY_TIME *ltime,
                     my_time_flags_t flags,
                     MY_TIME_STATUS *status);

static inline bool str_to_datetime(const Slice &str,
                                   MY_TIME *ltime,
                                   my_time_flags_t flags,
                                   MY_TIME_STATUS *status)
{
  return str_to_datetime((const char *)str.data(), str.size(), ltime, flags, status);
}

/*
    @retval false on success, true on error
   */
bool str_to_time(const char *str, size_t length,
                 MY_TIME *l_time,
                 my_time_flags_t flags,
                 MY_TIME_STATUS *status);

static inline bool str_to_time(const Slice &str,
                               MY_TIME *l_time,
                               my_time_flags_t flags,
                               MY_TIME_STATUS *status)
{
  return str_to_time((const char *)str.data(), str.size(), l_time, flags, status);
}

/*
    @return
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
                           my_time_flags_t flags, int *was_cut);

/*
    @retval false on success, true on error
   */
bool number_to_time(int64_t nr, MY_TIME *ltime, int *warnings);

/*
    @retval false on success, true on error
   */
class my_decimal;
bool my_decimal_to_datetime(const my_decimal *decimal,
                            MY_TIME *ltime,
                            my_time_flags_t flags,
                            int *warnings);

bool my_decimal_to_time(const my_decimal *decimal, MY_TIME *ltime, int *warnings);

bool my_double_to_datetime_with_warn(double nr, MY_TIME *ltime, my_time_flags_t flags);

bool my_double_to_time_with_warn(double nr, MY_TIME *ltime);

/*
    @retval false on success, true on error
   */
bool datetime_add_nanoseconds_with_round(MY_TIME *ltime,
                                         uint32_t nanoseconds,
                                         int *warnings);

/*
    @retval false on success, true on error
   */
bool time_add_nanoseconds_with_round(MY_TIME *ltime,
                                     uint32_t nanoseconds,
                                     int *warnings);
/*
    @retval false on success, true on error
   */
bool date_add_interval(MY_TIME *ltime, interval_type int_type,
                       Interval interval);
/*
    @return Days since 0000-00-00
  */
int32_t calc_daynr(uint32_t year, uint32_t month, uint32_t day);

/* 
     Change a daynr to year, month and day 
     Daynr 0 is returned as date 00.00.00 
  */
void get_date_from_daynr(int32_t daynr, uint32_t *ret_year, uint32_t *ret_month,
                         uint32_t *ret_day);

/*
    @return Days since 0000-00-00
  */
bool check_time_mmssff_range(const MY_TIME *ltime);

void adjust_time_range(MY_TIME *my_time, int *warning);

/*
    @return Days since 0000-00-00
  */
bool check_time_range_quick(const MY_TIME *ltime);

void set_max_hhmmss(MY_TIME *tm);

void set_max_time(MY_TIME *tm, bool neg);

void TIME_set_hhmmss(MY_TIME *ltime, uint32_t hhmmss);

/*
    Required buffer length for time_to_str, date_to_str,
    datetime_to_str and TIME_to_string functions. Note, that the
    caller is still responsible to check that given TIME structure
    has values in valid ranges, otherwise size of the buffer could
    be not enough. We also rely on the fact that even wrong values
    sent using binary protocol fit in this buffer.
  */
#define MAX_DATE_STRING_REP_LENGTH 30

int time_to_str(const MY_TIME *l_time, char *to, uint32_t dec);
int date_to_str(const MY_TIME *l_time, char *to);
int datetime_to_str(const MY_TIME *l_time, char *to, uint32_t dec);
int TIME_to_str(const MY_TIME *l_time, char *to, uint32_t dec);

int timeval_to_str(const struct timeval *tm, char *to, uint32_t dec);

bool my_time_round(MY_TIME *ltime, uint32_t dec);
bool my_datetime_round(MY_TIME *ltime, uint32_t dec, int *warnings);

// YYYYMMDDHHMMSS
uint64_t TIME_to_uint64_datetime(const MY_TIME *my_time);
uint64_t TIME_to_uint64_date(const MY_TIME *my_time);
uint64_t TIME_to_uint64_time(const MY_TIME *my_time);
uint64_t TIME_to_uint64_time_round(const MY_TIME *ltime);
uint64_t TIME_to_uint64_datetime_round(const MY_TIME *ltime);

my_decimal *date2my_decimal(const MY_TIME *ltime, my_decimal *dec);
my_decimal *time2my_decimal(const MY_TIME *ltime, my_decimal *dec);
my_decimal *timeval2my_decimal(const struct timeval *tm, my_decimal *dec);

#define MY_PACKED_TIME_GET_INT_PART(x) ((x) >> 24)
#define MY_PACKED_TIME_GET_FRAC_PART(x) ((x) % (1LL << 24))
#define MY_PACKED_TIME_MAKE(i, f) ((((int64_t)(i)) << 24) + (f))
#define MY_PACKED_TIME_MAKE_INT(i) ((((int64_t)(i)) << 24))

int64_t TIME_to_longlong_datetime_packed(const MY_TIME *ltime);
int64_t TIME_to_longlong_date_packed(const MY_TIME *ltime);
int64_t TIME_to_longlong_time_packed(const MY_TIME *ltime);
int64_t TIME_to_longlong_packed(const MY_TIME *ltime);
void TIME_from_longlong_datetime_packed(MY_TIME *ltime, int64_t nr);
void TIME_from_longlong_time_packed(MY_TIME *ltime, int64_t nr);
void TIME_from_longlong_date_packed(MY_TIME *ltime, int64_t nr);

int64_t my_datetime_packed_from_binary(const char *ptr, uint32_t dec);
void my_datetime_packed_to_binary(int64_t nr, char *ptr, uint32_t dec);

void my_time_packed_to_binary(int64_t nr, char *ptr, uint32_t dec);
int64_t my_time_packed_from_binary(const char *ptr, uint32_t dec);

void my_timestamp_to_binary(const struct timeval *tm, char *ptr, uint32_t dec);
void my_timestamp_from_binary(struct timeval *tm, const char *ptr, uint32_t dec);

static inline uint32_t my_time_fraction_remainder(uint32_t nr, uint32_t decimals)
{
  assert(decimals < DATETIME_MAX_DECIMALS);
  return nr % (uint32_t)log_10_int[DATETIME_MAX_DECIMALS - decimals];
}

static inline void my_time_trunc(MY_TIME *ltime, uint32_t decimals)
{
  ltime->second_part -= my_time_fraction_remainder(ltime->second_part, decimals);
}

static inline void datetime_to_time(MY_TIME *ltime)
{
  ltime->year = ltime->month = ltime->day = 0;
  ltime->time_type = TIMESTAMP_TIME;
}

bool calc_time_diff(const MY_TIME *l_time1, const MY_TIME *l_time2,
                    int l_sign, int64_t *seconds_out, int32_t *microseconds_out);
void adjust_leap_second(MY_TIME *t);
void localtime_to_time(MY_TIME *to, struct tm *from);
void calc_time_from_sec(MY_TIME *to, int64_t seconds, int32_t microseconds);
void time_to_datetime(const MY_TIME *ltime, MY_TIME *ltime2);
void mix_date_and_time(MY_TIME *ldate, const MY_TIME *ltime);

time_t TIME_to_timestamp(const MY_TIME *t, bool *in_dst_time_gap);
bool datetime_to_timeval(const MY_TIME *ltime, struct timeval *tm, int *warnings);

void localtime_sec_to_TIME(MY_TIME *tmp, time_t t);
void localtime_sec_to_TIME(MY_TIME *tmp, struct timeval tv);

} // namespace
