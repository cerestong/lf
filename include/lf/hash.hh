#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lf
{
extern uint32_t hash(const char *data, size_t n, uint32_t seed);

inline uint32_t slice_hash(const char *data, size_t n)
{
    return hash(data, n, 397);
}
} // end namespace
