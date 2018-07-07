#pragma once

#include <limits.h>
#include <stdint.h>

namespace lf
{

enum
{
  MYITOA_ERRNO_ERANGE = 1,
  MYITOA_ERRNO_EDOM = 2,
};

class myitoa
{
public:
  /*
      Convert a string to an to unsigned long long integer value
  
      SYNOPSYS
      strtoint64()
        nptr     in       pointer to the string to be converted
        endptr   in/out   pointer to the end of the string/
	                  pointer to the stop character
        error    out      returned error code
 
      DESCRIPTION
      This function takes the decimal representation of integer number
      from string nptr and converts it to an signed or unsigned
      int64_t integer value.
      Space characters and tab are ignored.
      A sign character might precede the digit characters. The number
      may have any number of pre-zero digits.

      The function stops reading the string nptr at the first character
      that is not a decimal digit. If endptr is not NULL then the function
      will not read characters after *endptr.
 
      RETURN VALUES
      Value of string as a signed/unsigned longlong integer

      if no error and endptr != NULL, it will be set to point at the character
      after the number

      The error parameter contains information how things went:
      -1	Number was an ok negative number
      0	 	ok
      ERANGE	If the the value of the converted number exceeded the
	        maximum negative/unsigned long long integer.
		In this case the return value is ~0 if value was
		positive and LLONG_MIN if value was negative.
      EDOM	If the string didn't contain any digits. In this case
    		the return value is 0.

      If endptr is not NULL the function will store the end pointer to
      the stop character here.
    */

  static int64_t strtoint64(const char *nptr, char **endptr, int *error);

  static uint64_t strtouint64(const char *nptr, char **endptr, int *error);
};

} // end namespace pi
