#pragma once

#include <stddef.h>
#include <stdint.h>

/* result is big endian*/

#define be_int1store(T, A) *((uint8_t *)(T)) = (uint8_t)(A)

#define be_int2store(T, A)                              \
    {                                                   \
        uint16_t def_temp = (uint16_t)(A);              \
        ((uint8_t *)(T))[1] = (uint8_t)(def_temp);      \
        ((uint8_t *)(T))[0] = (uint8_t)(def_temp >> 8); \
    }
#define be_int3store(T, A)                               \
    {                                                    \
        uint32_t def_temp = (uint32_t)(A);               \
        ((uint8_t *)(T))[2] = (uint8_t)(def_temp);       \
        ((uint8_t *)(T))[1] = (uint8_t)(def_temp >> 8);  \
        ((uint8_t *)(T))[0] = (uint8_t)(def_temp >> 16); \
    }
#define be_int4store(T, A)                               \
    {                                                    \
        uint32_t def_temp = (uint32_t)(A);               \
        ((uint8_t *)(T))[3] = (uint8_t)(def_temp);       \
        ((uint8_t *)(T))[2] = (uint8_t)(def_temp >> 8);  \
        ((uint8_t *)(T))[1] = (uint8_t)(def_temp >> 16); \
        ((uint8_t *)(T))[0] = (uint8_t)(def_temp >> 24); \
    }
#define be_int5store(T, A)                               \
    {                                                    \
        uint32_t def_temp = (uint32_t)(A),               \
                 def_temp2 = (uint32_t)((A) >> 32);      \
        ((uint8_t *)(T))[4] = (uint8_t)(def_temp);       \
        ((uint8_t *)(T))[3] = (uint8_t)(def_temp >> 8);  \
        ((uint8_t *)(T))[2] = (uint8_t)(def_temp >> 16); \
        ((uint8_t *)(T))[1] = (uint8_t)(def_temp >> 24); \
        ((uint8_t *)(T))[0] = (uint8_t)(def_temp2);      \
    }
#define be_int6store(T, A)                               \
    {                                                    \
        uint32_t def_temp = (uint32_t)(A),               \
                 def_temp2 = (uint32_t)((A) >> 32);      \
        ((uint8_t *)(T))[5] = (uint8_t)(def_temp);       \
        ((uint8_t *)(T))[4] = (uint8_t)(def_temp >> 8);  \
        ((uint8_t *)(T))[3] = (uint8_t)(def_temp >> 16); \
        ((uint8_t *)(T))[2] = (uint8_t)(def_temp >> 24); \
        ((uint8_t *)(T))[1] = (uint8_t)(def_temp2);      \
        ((uint8_t *)(T))[0] = (uint8_t)(def_temp2 >> 8); \
    }
#define be_int7store(T, A)                                \
    {                                                     \
        uint32_t def_temp = (uint32_t)(A),                \
                 def_temp2 = (uint32_t)((A) >> 32);       \
        ((uint8_t *)(T))[6] = (uint8_t)(def_temp);        \
        ((uint8_t *)(T))[5] = (uint8_t)(def_temp >> 8);   \
        ((uint8_t *)(T))[4] = (uint8_t)(def_temp >> 16);  \
        ((uint8_t *)(T))[3] = (uint8_t)(def_temp >> 24);  \
        ((uint8_t *)(T))[2] = (uint8_t)(def_temp2);       \
        ((uint8_t *)(T))[1] = (uint8_t)(def_temp2 >> 8);  \
        ((uint8_t *)(T))[0] = (uint8_t)(def_temp2 >> 16); \
    }
#define be_int8store(T, A)                           \
    {                                                \
        uint32_t def_temp3 = (uint32_t)(A),          \
                 def_temp4 = (uint32_t)((A) >> 32);  \
        be_int4store((uint8_t *)(T) + 0, def_temp4); \
        be_int4store((uint8_t *)(T) + 4, def_temp3); \
    }

#define be_sint1korr(A) ((int8_t)(*A))
#define be_uint1korr(A) ((uint8_t)(*A))

#define be_sint2korr(A) ((int16_t)(((int16_t)(((uint8_t *)(A))[1])) + \
                                   ((int16_t)((int16_t)((char *)(A))[0]) << 8)))
#define be_sint3korr(A) ((int32_t)(((((uint8_t *)(A))[0]) & 128) ? (((uint32_t)255L << 24) |                    \
                                                                    (((uint32_t)((uint8_t *)(A))[0]) << 16) |   \
                                                                    (((uint32_t)((uint8_t *)(A))[1]) << 8) |    \
                                                                    ((uint32_t)((uint8_t *)(A))[2]))            \
                                                                 : (((uint32_t)((uint8_t *)(A))[0]) << 16) |    \
                                                                       (((uint32_t)((uint8_t *)(A))[1]) << 8) | \
                                                                       ((uint32_t)((uint8_t *)(A))[2])))
#define be_sint4korr(A) ((int32_t)(((int32_t)(((uint8_t *)(A))[3])) +       \
                                   ((int32_t)(((uint8_t *)(A))[2]) << 8) +  \
                                   ((int32_t)(((uint8_t *)(A))[1]) << 16) + \
                                   ((int32_t)((int16_t)((char *)(A))[0]) << 24)))
#define be_sint8korr(A) ((int64_t)be_uint8korr(A))
#define be_uint2korr(A) ((uint16_t)(((uint16_t)(((uint8_t *)(A))[1])) + \
                                    ((uint16_t)(((uint8_t *)(A))[0]) << 8)))
#define be_uint3korr(A) ((uint32_t)(((uint32_t)(((uint8_t *)(A))[2])) +        \
                                    (((uint32_t)(((uint8_t *)(A))[1])) << 8) + \
                                    (((uint32_t)(((uint8_t *)(A))[0])) << 16)))
#define be_uint4korr(A) ((uint32_t)(((uint32_t)(((uint8_t *)(A))[3])) +         \
                                    (((uint32_t)(((uint8_t *)(A))[2])) << 8) +  \
                                    (((uint32_t)(((uint8_t *)(A))[1])) << 16) + \
                                    (((uint32_t)(((uint8_t *)(A))[0])) << 24)))
#define be_uint5korr(A) ((uint64_t)(((uint32_t)(((uint8_t *)(A))[4])) +          \
                                    (((uint32_t)(((uint8_t *)(A))[3])) << 8) +   \
                                    (((uint32_t)(((uint8_t *)(A))[2])) << 16) +  \
                                    (((uint32_t)(((uint8_t *)(A))[1])) << 24)) + \
                         (((uint64_t)(((uint8_t *)(A))[0])) << 32))
#define be_uint6korr(A) ((uint64_t)(((uint32_t)(((uint8_t *)(A))[5])) +          \
                                    (((uint32_t)(((uint8_t *)(A))[4])) << 8) +   \
                                    (((uint32_t)(((uint8_t *)(A))[3])) << 16) +  \
                                    (((uint32_t)(((uint8_t *)(A))[2])) << 24)) + \
                         (((uint64_t)(((uint32_t)(((uint8_t *)(A))[1])) +        \
                                      (((uint32_t)(((uint8_t *)(A))[0]) << 8)))) \
                          << 32))
#define be_uint7korr(A) ((uint64_t)(((uint32_t)(((uint8_t *)(A))[6])) +           \
                                    (((uint32_t)(((uint8_t *)(A))[5])) << 8) +    \
                                    (((uint32_t)(((uint8_t *)(A))[4])) << 16) +   \
                                    (((uint32_t)(((uint8_t *)(A))[3])) << 24)) +  \
                         (((uint64_t)(((uint32_t)(((uint8_t *)(A))[2])) +         \
                                      (((uint32_t)(((uint8_t *)(A))[1])) << 8) +  \
                                      (((uint32_t)(((uint8_t *)(A))[0])) << 16))) \
                          << 32))
#define be_uint8korr(A) ((uint64_t)(((uint32_t)(((uint8_t *)(A))[7])) +           \
                                    (((uint32_t)(((uint8_t *)(A))[6])) << 8) +    \
                                    (((uint32_t)(((uint8_t *)(A))[5])) << 16) +   \
                                    (((uint32_t)(((uint8_t *)(A))[4])) << 24)) +  \
                         (((uint64_t)(((uint32_t)(((uint8_t *)(A))[3])) +         \
                                      (((uint32_t)(((uint8_t *)(A))[2])) << 8) +  \
                                      (((uint32_t)(((uint8_t *)(A))[1])) << 16) + \
                                      (((uint32_t)(((uint8_t *)(A))[0])) << 24))) \
                          << 32))
