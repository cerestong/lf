#include <string.h>
#include "lf/hash.hh"
#include "lf/compiler.hh"

namespace lf
{

inline uint32_t DecodeFixed32(const char *ptr)
{
  if (lfLittleEndian)
  {
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result)); // gcc optimizes this to a plain load
    return result;
  }
  else
  {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
}

uint32_t hash(const char *data, size_t n, uint32_t seed)
{
  // Similar to murmur hash
  const uint32_t m = 0xc6a4a793;
  const uint32_t r = 24;
  const char *limit = data + n;
  uint32_t h = static_cast<uint32_t>(seed ^ (n * m));

  // Pick up four bytes at a time
  while (data + 4 <= limit)
  {
    uint32_t w = DecodeFixed32(data);
    data += 4;
    h += w;
    h *= m;
    h ^= (h >> 16);
  }

  // Pick up remaining bytes
  switch (limit - data)
  {
  // Note: The original hash implementation used data[i] << shift, which
  // promotes the char to int and then performs the shift. If the char is
  // negative, the shift is undefined behavior in C++. The hash algorithm is
  // part of the format definition, so we cannot change it; to obtain the same
  // behavior in a legal way we just cast to uint32_t, which will do
  // sign-extension. To guarantee compatibility with architectures where chars
  // are unsigned we first cast the char to int8_t.
  case 3:
    h += static_cast<uint32_t>(static_cast<int8_t>(data[2])) << 16;
  // fall through
  case 2:
    h += static_cast<uint32_t>(static_cast<int8_t>(data[1])) << 8;
  // fall through
  case 1:
    h += static_cast<uint32_t>(static_cast<int8_t>(data[0]));
    h *= m;
    h ^= (h >> r);
    break;
  }
  return h;
}

} // end namespace
