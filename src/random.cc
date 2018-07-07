#include "lf/random.hh"
#include "lf/compiler.hh"
#include <string.h>
#include <thread>
#include <utility>

#define STORAGE_DECL static __thread

namespace lf
{

Random *Random::get_tls_instance()
{
  STORAGE_DECL Random *tls_instance;
  STORAGE_DECL std::aligned_storage<sizeof(Random)>::type tls_instance_bytes;

  auto rv = tls_instance;
  if (unlikely(rv == nullptr))
  {
    size_t seed = std::hash<std::thread::id>()(std::this_thread::get_id());
    rv = new (&tls_instance_bytes) Random((uint32_t)seed);
    tls_instance = rv;
  }
  return rv;
}

} // end namespace lf
