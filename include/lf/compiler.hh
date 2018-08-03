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

extern void fail_lf_invariant(const char *file, int line, const char *assertion, const char *message = 0) __attribute__((noreturn));

#define lf_invariant(x, ...) do{ if (!(x)) fail_lf_invariant(__FILE__, __LINE__, #x, ##__VA_ARGS__); } while (0)

extern void fail_lf_precondition(const char *file, int line, const char *assertion, const char *message = 0) __attribute__((noreturn));

#define lf_precondition(x, ...)                                          \
    do                                                                   \
    {                                                                    \
        if (!(x))                                                        \
            fail_lf_precondition(__FILE__, __LINE__, #x, ##__VA_ARGS__); \
    } while (0)

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

static inline int atomic_cas64(uint64_t volatile *a, uint64_t *cmp, uint64_t set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int atomic_cas64_relaxed(uint64_t volatile *a, uint64_t *cmp, uint64_t set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

static inline int atomic_casptr(void *volatile *a, void **cmp, void *set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int atomic_casptr_relaxed(void *volatile *a, void **cmp, void *set)
{
    return __atomic_compare_exchange_n(a, cmp, set, 0,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED);
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

/** @brief Compiler fence that relaxes the processor.

    Use this in spinloops, for example. */
inline void spin_hint()
{
    asm volatile("pause" ::: "memory"); // equivalent to "rep; nop"
}

static inline void compiler_barrier()
{
    asm volatile("" ::: "memory");
}

struct spin_hint_function
{
    void operator()() const
    {
        spin_hint();
    }
};

// stolen from Linux
inline uint64_t ntohq(uint64_t val)
{
#ifdef __i386__
    union {
        struct {
            uint32_t a;
            uint32_t b;
        } s;
        uint64_t u;
    } v;
    v.u = val;
    asm("bswapl %0; bswapl %1; xchgl %0,%1"
        : "+r" (v.s.a), "+r" (v.s.b));
    return v.u;
#else /* __i386__ */
    asm("bswapq %0" : "+r" (val));
    return val;
#endif
}

inline uint64_t htonq(uint64_t val)
{
    return ntohq(val);
}

inline uint64_t net_to_host_order(uint64_t x)
{
    return ntohq(x);
}

inline uint64_t host_to_net_order(uint64_t x)
{
    return htonq(x);
}

inline int ctz(uint64_t x)
{
    return __builtin_ctzll(x);
}

inline int clz(long long x)
{
    return __builtin_clzll(x);
}

inline int clz(uint64_t x)
{
    return __builtin_clzll(x);
}

template <typename T, typename U>
inline T iceil(T x, U y)
{
    U mod = x % y;
    return x + (mod ? y - mod : 0);
}

/* Return the smallest power of 2 greater than or equal to x.
    @pre x != 0
    @pre the result is representable in type T
*/
template <typename T>
inline T iceil_log2(T x)
{
    return T(1) << (sizeof(T) * 8 - clz(x) - !(x & (x - 1)));
}

struct do_nothing
{
    void operator()() const
    {}
    template <typename T>
    void operator()(const T&) const
    {}
    template <typename T, typename U>
    void operator()(const T&, const U&) const
    {}
};

} // namespace lf
