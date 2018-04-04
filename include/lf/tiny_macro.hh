#pragma once

#define set_if_smaller(a, b) \
    do                       \
    {                        \
        if ((a) > (b))       \
            (a) = (b);       \
    } while (0)
#define set_if_bigger(a, b) \
    do                      \
    {                       \
        if ((a) < (b))      \
            (a) = (b);      \
    } while (0)
#define swap_variables(t, a, b) \
    {                           \
        t dummy;                \
        dummy = a;              \
        a = b;                  \
        b = dummy;              \
    }
