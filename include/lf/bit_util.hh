#pragma once

#include <stdint.h>

namespace lf
{

extern const uint8_t _bits_reverse_table[256];

static inline uint32_t reverse_bits(uint32_t key)
{
  return (_bits_reverse_table[key & 255] << 24) |
         (_bits_reverse_table[(key >> 8) & 255] << 16) |
         (_bits_reverse_table[(key >> 16) & 255] << 8) |
         _bits_reverse_table[(key >> 24)];
}

static inline uint32_t clear_highest_bit(uint32_t v)
{
  uint32_t w = v >> 1;
  w |= w >> 1;
  w |= w >> 2;
  w |= w >> 4;
  w |= w >> 8;
  w |= w >> 16;
  return v & w;
}

} // end namespace
