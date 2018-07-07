#pragma once

#include <stdint.h>

#undef PLATFORM_IS_LITTLE_ENDIAN
#include <endian.h>
#include <string.h>

#ifndef PLATFORM_IS_LITTLE_ENDIAN
#define PLATFORM_IS_LITTLE_ENDIAN (__BYTE_ORDER == __LITTLE_ENDIAN)
#endif

namespace lf
{

static const bool lfLittleEndian = PLATFORM_IS_LITTLE_ENDIAN;
#undef PLATFORM_IS_LITTLE_ENDIAN

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define LIKELY(x) (__builtin_expect(!!(x), 1))
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))

static inline void compiler_barrier()
{
    asm volatile("" ::: "memory");
}

static inline int32_t atomic_load(int32_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_SEQ_CST);
}

static inline int32_t atomic_load_relaxed(int32_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_RELAXED);
}

static inline int32_t atomic_load_acquire(int32_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_ACQUIRE);
}

static inline int64_t atomic_load(int64_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_load_relaxed(int64_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_RELAXED);
}

static inline int64_t atomic_load_acquire(int64_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_ACQUIRE);
}

static inline uint32_t atomic_load(uint32_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_load_relaxed(uint32_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_RELAXED);
}

static inline uint32_t atomic_load_acquire(uint32_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_ACQUIRE);
}

static inline uint64_t atomic_load(uint64_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_load_relaxed(uint64_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_RELAXED);
}

static inline int64_t atomic_load_acquire(uint64_t volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_ACQUIRE);
}

static inline void *atomic_loadptr(void *volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_SEQ_CST);
}

static inline void *atomic_loadptr_relaxed(void *volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_RELAXED);
}

static inline void *atomic_load_acquire(void *volatile *a)
{
    return __atomic_load_n(a, __ATOMIC_ACQUIRE);
}

static inline void atomic_store(int64_t volatile *a, int64_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_relaxed(int64_t volatile *a, int64_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELAXED);
}

static inline void atomic_store_release(int64_t volatile *a, int64_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELEASE);
}

static inline void atomic_store(uint64_t volatile *a, uint64_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_relaxed(uint64_t volatile *a, uint64_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELAXED);
}

static inline void atomic_store_release(uint64_t volatile *a, uint64_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELEASE);
}

static inline void atomic_store(int32_t volatile *a, int32_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_relaxed(int32_t volatile *a, int32_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELAXED);
}

static inline void atomic_store_release(int32_t volatile *a, int32_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELEASE);
}

static inline void atomic_store(uint32_t volatile *a, uint32_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_SEQ_CST);
}

static inline void atomic_store_relaxed(uint32_t volatile *a, uint32_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELAXED);
}

static inline void atomic_store_release(uint32_t volatile *a, uint32_t v)
{
    return __atomic_store_n(a, v, __ATOMIC_RELEASE);
}

static inline void atomic_storeptr(void *volatile *a, void *v)
{
    __atomic_store_n(a, v, __ATOMIC_SEQ_CST);
}

static inline void atomic_storeptr_relaxed(void *volatile *a, void *v)
{
    __atomic_store_n(a, v, __ATOMIC_RELAXED);
}

static inline void atomic_storeptr_release(void *volatile *a, void *v)
{
    __atomic_store_n(a, v, __ATOMIC_RELEASE);
}

static inline int atomic_cas32(int32_t volatile *a, int32_t *cmp, int32_t set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int atomic_cas64(int64_t volatile *a, int64_t *cmp, int64_t set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int atomic_casptr(void *volatile *a, void **cmp, void *set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int32_t atomic_add32(int32_t volatile *a, int32_t v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_SEQ_CST);
}

static inline int32_t atomic_add32_relaxed(int32_t volatile *a, int32_t v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
}

static inline int64_t atomic_add64(int64_t volatile *a, int64_t v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_add64_relaxed(int64_t volatile *a, int64_t v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
}

static inline uint64_t atomic_add64(uint64_t volatile *a, uint64_t v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_add64_relaxed(uint64_t volatile *a, uint64_t v)
{
    return __atomic_fetch_add(a, v, __ATOMIC_RELAXED);
}

static inline void memory_fence()
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline void acquire_fence()
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

static inline void release_fence()
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static inline void relax_fence()
{
    __atomic_thread_fence(__ATOMIC_RELAXED);
}

} // end namespace
