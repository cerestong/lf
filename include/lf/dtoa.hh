#pragma once

#include <string.h>
#include "lf/my_decimal.hh"

namespace lf
{

enum gcvt_arg_type
{
  GCVT_ARG_FLOAT,
  GCVT_ARG_DOUBLE
};

enum
{
  //NOT_FIXED_DEC = 31,
  /*
      The longest string fcvt can return is 311 + "precision" bytes.
      Here we assume that we never cal fcvt() with precision >= NOT_FIXED_DEC
      (+ 1 byte for the terminating '\0').
    */
  FLOATING_POINT_BUFFER = (311 + NOT_FIXED_DEC),
};

class mydtoa
{
public:
  /**
       @brief
       Converts a given floating point number to a zero-terminated string
       representation using the 'f' format.

       @details
       This function is a wrapper around dtoa() to do the same as
       sprintf(to, "%-.*f", precision, x), though the conversion is usually more
       precise. The only difference is in handling [-,+]infinity and nan values,
       in which case we print '0\0' to the output string and indicate an overflow.

       @param x           the input floating point number.
       @param precision   the number of digits after the decimal point.
                          All properties of sprintf() apply:
                          - if the number of significant digits after the decimal
			   point is less than precision, the resulting string is
			   right-padded with zeros
			  - if the precision is 0, no decimal point appears
			  - if a decimal point appears, at least one digit appears
			   before it
       @param to          pointer to the output buffer. The longest string which
                          fcvt() can return is FLOATING_POINT_BUFFER bytes
			  (including the terminating '\0').
       @param error       if not NULL, points to a location where the status of
                          conversion is stored upon return.
                          FALSE  successful conversion
			  TRUE   the input number is [-,+]infinity or nan.
                             The output string in this case is always '0'.
       @return            number of written characters (excluding terminating '\0')
    */

  static size_t fcvt(double x, int precision, char *to, bool *error);

  /**
       @brief
       Converts a given floating point number to a zero-terminated string
       representation with a given field width using the 'e' format
       (aka scientific notation) or the 'f' one.

       @details
       The format is chosen automatically to provide the most number of significant
       digits (and thus, precision) with a given field width. In many cases, the
       result is similar to that of sprintf(to, "%g", x) with a few notable
       differences:
       - the conversion is usually more precise than C library functions.
       - there is no 'precision' argument. instead, we specify the number of
       characters available for conversion (i.e. a field width).
       - the result never exceeds the specified field width. If the field is too
       short to contain even a rounded decimal representation, gcvt()
       indicates overflow and truncates the output string to the specified width.
       - float-type arguments are handled differently than double ones. For a
       float input number (i.e. when the 'type' argument is GCVT_ARG_FLOAT)
       we deliberately limit the precision of conversion by FLT_DIG digits to
       avoid garbage past the significant digits.
       - unlike sprintf(), in cases where the 'e' format is preferred,  we don't
       zero-pad the exponent to save space for significant digits. The '+' sign
       for a positive exponent does not appear for the same reason.

       @param x           the input floating point number.
       @param type        is either GCVT_ARG_FLOAT or GCVT_ARG_DOUBLE.
                          Specifies the type of the input number (see notes above).
       @param width       field width in characters. The minimal field width to
                          hold any number representation (albeit rounded) is 7
			  characters ("-Ne-NNN").
       @param to          pointer to the output buffer. The result is always
                          zero-terminated, and the longest returned string is thus
                          'width + 1' bytes.
       @param error       if not NULL, points to a location where the status of
                          conversion is stored upon return.
                          FALSE  successful conversion
                          TRUE   the input number is [-,+]infinity or nan.
                             The output string in this case is always '0'.
       @return            number of written characters (excluding terminating '\0')

       @todo
       Check if it is possible and  makes sense to do our own rounding on top of
       dtoa() instead of calling dtoa() twice in (rare) cases when the resulting
       string representation does not fit in the specified field width and we want
       to re-round the input number with fewer significant digits. Examples:

       gcvt(-9e-3, ..., 4, ...);
       gcvt(-9e-3, ..., 2, ...);
       gcvt(1.87e-3, ..., 4, ...);
       gcvt(55, ..., 1, ...);

       We do our best to minimize such cases by:
   
       - passing to dtoa() the field width as the number of significant digits
   
       - removing the sign of the number early (and decreasing the width before
       passing it to dtoa())
   
       - choosing the proper format to preserve the most number of significant
       digits.
    */
  static size_t gcvt(double x, gcvt_arg_type type, int width, char *to, bool *error);

  /**
       @brief
       Converts string to double (string does not have to be zero-terminated)

       @details
       This is a wrapper around dtoa's version of strtod().

       @param str     input string
       @param end     address of a pointer to the first character after the input
                      string. Upon return the pointer is set to point to the first
                      rejected character.
       @param error   Upon return is set to EOVERFLOW in case of underflow or
                      overflow.
   
       @return        The resulting double value. In case of underflow, 0.0 is
                      returned. In case overflow, signed DBL_MAX is returned.
    */

  static double strtod(const char *str, char **end, int *error);

  static double atof(const char *nptr);
};

} // end namespace 
