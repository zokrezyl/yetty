/* m3_msvc_atomics.h — GCC __atomic_* / __ATOMIC_* shim for MSVC.
 *
 * Force-included into the wasm3 build via /FI. Wasm3's m3_exec.h /
 * m3_atomic.c use GCC's __atomic_* builtins; MSVC doesn't have them
 * but ships the _Interlocked* intrinsics that do the same thing on
 * x86/x64 (every op is full seq-cst — strong enough for everything
 * wasm3 emits since it only ever requests __ATOMIC_RELAXED or
 * __ATOMIC_SEQ_CST).
 *
 * Width dispatch is via sizeof(*(p)) instead of _Generic — MSVC's
 * _Generic requires exact qualifier match, which means a wasm3 site
 * holding a `volatile u32 *` would NOT match an `int *` association.
 * sizeof() doesn't care about qualifiers and gives us a single
 * canonical width path.
 *
 * No #ifdef in consumers — this header is only on the wasm3 build's
 * include path when the host is Windows.
 */
#ifndef YOS_M3_MSVC_ATOMICS_H
#define YOS_M3_MSVC_ATOMICS_H

#include <intrin.h>
#include <stdint.h>
#include <stddef.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef __ATOMIC_RELAXED
#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5
#endif

/* ── load family ───────────────────────────────────────────────────── */

static __forceinline uint8_t  yos_m3_load_u8 (const volatile void *p) {
    _ReadWriteBarrier();
    return *(const volatile uint8_t  *)p;
}
static __forceinline uint16_t yos_m3_load_u16(const volatile void *p) {
    _ReadWriteBarrier();
    return *(const volatile uint16_t *)p;
}
static __forceinline uint32_t yos_m3_load_u32(const volatile void *p) {
    _ReadWriteBarrier();
    return *(const volatile uint32_t *)p;
}
static __forceinline uint64_t yos_m3_load_u64(const volatile void *p) {
    _ReadWriteBarrier();
    return *(const volatile uint64_t *)p;
}

/* The ternary yields a u64 — caller code compares to its own type and
 * the integer-promotion rules deal with the rest. Branches are
 * dead-code-eliminated at any non-zero optimisation level. */
#define __atomic_load_n(p, mo)                                              \
    ( (sizeof(*(p)) == 1) ? (uint64_t)yos_m3_load_u8 ((const volatile void *)(p)) \
    : (sizeof(*(p)) == 2) ? (uint64_t)yos_m3_load_u16((const volatile void *)(p)) \
    : (sizeof(*(p)) == 4) ? (uint64_t)yos_m3_load_u32((const volatile void *)(p)) \
    :                       (uint64_t)yos_m3_load_u64((const volatile void *)(p)) )

#define __atomic_load(p, dst, mo)  ((void)(*(dst) = __atomic_load_n((p), (mo))))

/* ── store family ──────────────────────────────────────────────────── */

#define __atomic_store_n(p, v, mo) do {                                     \
    if      (sizeof(*(p)) == 1) *(volatile uint8_t  *)(p) = (uint8_t )(uint64_t)(v); \
    else if (sizeof(*(p)) == 2) *(volatile uint16_t *)(p) = (uint16_t)(uint64_t)(v); \
    else if (sizeof(*(p)) == 4) *(volatile uint32_t *)(p) = (uint32_t)(uint64_t)(v); \
    else                        *(volatile uint64_t *)(p) = (uint64_t)(v);  \
    if ((mo) == __ATOMIC_SEQ_CST) MemoryBarrier();                          \
} while (0)

#define __atomic_store(p, src, mo) __atomic_store_n((p), *(src), (mo))

/* ── exchange ──────────────────────────────────────────────────────── */

#define __atomic_exchange_n(p, v, mo)                                       \
    ( (sizeof(*(p)) == 1) ? (uint64_t)(uint8_t )_InterlockedExchange8 ((volatile char     *)(p), (char)    (uint64_t)(v)) \
    : (sizeof(*(p)) == 2) ? (uint64_t)(uint16_t)_InterlockedExchange16((volatile short    *)(p), (short)   (uint64_t)(v)) \
    : (sizeof(*(p)) == 4) ? (uint64_t)(uint32_t)_InterlockedExchange  ((volatile long     *)(p), (long)    (uint64_t)(v)) \
    :                       (uint64_t)         _InterlockedExchange64((volatile long long *)(p), (long long)(uint64_t)(v)) )

#define __atomic_exchange(p, vp, ret_p, mo) ((void)(*(ret_p) = __atomic_exchange_n((p), *(vp), (mo))))

/* ── fetch_add / sub / and / or / xor ──────────────────────────────── */

#define __atomic_fetch_add(p, v, mo)                                        \
    ( (sizeof(*(p)) == 1) ? (uint64_t)(uint8_t )_InterlockedExchangeAdd8 ((volatile char     *)(p), (char)    (uint64_t)(v)) \
    : (sizeof(*(p)) == 2) ? (uint64_t)(uint16_t)_InterlockedExchangeAdd16((volatile short    *)(p), (short)   (uint64_t)(v)) \
    : (sizeof(*(p)) == 4) ? (uint64_t)(uint32_t)_InterlockedExchangeAdd  ((volatile long     *)(p), (long)    (uint64_t)(v)) \
    :                       (uint64_t)         _InterlockedExchangeAdd64((volatile long long *)(p), (long long)(uint64_t)(v)) )

#define __atomic_fetch_sub(p, v, mo)  __atomic_fetch_add((p), (uint64_t)0 - (uint64_t)(v), (mo))

#define __atomic_fetch_and(p, v, mo)                                        \
    ( (sizeof(*(p)) == 1) ? (uint64_t)(uint8_t )_InterlockedAnd8 ((volatile char     *)(p), (char)    (uint64_t)(v)) \
    : (sizeof(*(p)) == 2) ? (uint64_t)(uint16_t)_InterlockedAnd16((volatile short    *)(p), (short)   (uint64_t)(v)) \
    : (sizeof(*(p)) == 4) ? (uint64_t)(uint32_t)_InterlockedAnd  ((volatile long     *)(p), (long)    (uint64_t)(v)) \
    :                       (uint64_t)         _InterlockedAnd64((volatile long long *)(p), (long long)(uint64_t)(v)) )

#define __atomic_fetch_or(p, v, mo)                                         \
    ( (sizeof(*(p)) == 1) ? (uint64_t)(uint8_t )_InterlockedOr8 ((volatile char     *)(p), (char)    (uint64_t)(v)) \
    : (sizeof(*(p)) == 2) ? (uint64_t)(uint16_t)_InterlockedOr16((volatile short    *)(p), (short)   (uint64_t)(v)) \
    : (sizeof(*(p)) == 4) ? (uint64_t)(uint32_t)_InterlockedOr  ((volatile long     *)(p), (long)    (uint64_t)(v)) \
    :                       (uint64_t)         _InterlockedOr64((volatile long long *)(p), (long long)(uint64_t)(v)) )

#define __atomic_fetch_xor(p, v, mo)                                        \
    ( (sizeof(*(p)) == 1) ? (uint64_t)(uint8_t )_InterlockedXor8 ((volatile char     *)(p), (char)    (uint64_t)(v)) \
    : (sizeof(*(p)) == 2) ? (uint64_t)(uint16_t)_InterlockedXor16((volatile short    *)(p), (short)   (uint64_t)(v)) \
    : (sizeof(*(p)) == 4) ? (uint64_t)(uint32_t)_InterlockedXor  ((volatile long     *)(p), (long)    (uint64_t)(v)) \
    :                       (uint64_t)         _InterlockedXor64((volatile long long *)(p), (long long)(uint64_t)(v)) )

/* ── compare_exchange ──────────────────────────────────────────────── */
/* GCC signature: bool __atomic_compare_exchange_n(T *p, T *expected,
 *                                                  T desired, bool weak,
 *                                                  int smo, int fmo);
 * On success, p is updated and the call returns 1.
 * On failure, *expected is updated with the observed value, returns 0. */

static __forceinline int yos_m3_cmpxchg_8(volatile void *p, void *exp, uint64_t des) {
    volatile char *pc = (volatile char *)p;
    char *ec = (char *)exp;
    char old = _InterlockedCompareExchange8(pc, (char)des, *ec);
    if (old == *ec) return 1;
    *ec = old;
    return 0;
}
static __forceinline int yos_m3_cmpxchg_16(volatile void *p, void *exp, uint64_t des) {
    volatile short *ps = (volatile short *)p;
    short *es = (short *)exp;
    short old = _InterlockedCompareExchange16(ps, (short)des, *es);
    if (old == *es) return 1;
    *es = old;
    return 0;
}
static __forceinline int yos_m3_cmpxchg_32(volatile void *p, void *exp, uint64_t des) {
    volatile long *pl = (volatile long *)p;
    long *el = (long *)exp;
    long old = _InterlockedCompareExchange(pl, (long)des, *el);
    if (old == *el) return 1;
    *el = old;
    return 0;
}
static __forceinline int yos_m3_cmpxchg_64(volatile void *p, void *exp, uint64_t des) {
    volatile long long *pll = (volatile long long *)p;
    long long *ell = (long long *)exp;
    long long old = _InterlockedCompareExchange64(pll, (long long)des, *ell);
    if (old == *ell) return 1;
    *ell = old;
    return 0;
}

#define __atomic_compare_exchange_n(p, exp_p, des, weak, smo, fmo)          \
    ( (sizeof(*(p)) == 1) ? yos_m3_cmpxchg_8 ((volatile void *)(p), (void *)(exp_p), (uint64_t)(des)) \
    : (sizeof(*(p)) == 2) ? yos_m3_cmpxchg_16((volatile void *)(p), (void *)(exp_p), (uint64_t)(des)) \
    : (sizeof(*(p)) == 4) ? yos_m3_cmpxchg_32((volatile void *)(p), (void *)(exp_p), (uint64_t)(des)) \
    :                       yos_m3_cmpxchg_64((volatile void *)(p), (void *)(exp_p), (uint64_t)(des)) )

#define __atomic_compare_exchange(p, exp_p, des_p, weak, smo, fmo) \
    __atomic_compare_exchange_n((p), (exp_p), *(des_p), (weak), (smo), (fmo))

/* ── _fetch variants (return new value, not old) ───────────────────── */
/* GCC defines both __atomic_fetch_op (returns old) and __atomic_op_fetch
 * (returns new). On x86 we can derive _fetch from fetch_ by combining
 * the returned-old value with the operand. */

#define __atomic_add_fetch(p, v, mo)  ((uint64_t)__atomic_fetch_add((p), (v), (mo)) + (uint64_t)(v))
#define __atomic_sub_fetch(p, v, mo)  ((uint64_t)__atomic_fetch_sub((p), (v), (mo)) - (uint64_t)(v))
#define __atomic_and_fetch(p, v, mo)  ((uint64_t)__atomic_fetch_and((p), (v), (mo)) & (uint64_t)(v))
#define __atomic_or_fetch(p, v, mo)   ((uint64_t)__atomic_fetch_or ((p), (v), (mo)) | (uint64_t)(v))
#define __atomic_xor_fetch(p, v, mo)  ((uint64_t)__atomic_fetch_xor((p), (v), (mo)) ^ (uint64_t)(v))
#define __atomic_nand_fetch(p, v, mo) (~((uint64_t)__atomic_fetch_and((p), (v), (mo)) & (uint64_t)(v)))

/* ── thread_fence / signal_fence ───────────────────────────────────── */

#define __atomic_thread_fence(mo)  MemoryBarrier()
#define __atomic_signal_fence(mo)  _ReadWriteBarrier()

/* ── test_and_set / clear ──────────────────────────────────────────── */

#define __atomic_test_and_set(p, mo) \
    ((_Bool)_InterlockedExchange8((volatile char *)(p), 1))
#define __atomic_clear(p, mo) \
    do { _InterlockedExchange8((volatile char *)(p), 0); (void)(mo); } while (0)

/* ── is_lock_free ──────────────────────────────────────────────────── */

#define __atomic_always_lock_free(sz, p) ((sz) <= 8)
#define __atomic_is_lock_free(sz, p)     ((sz) <= 8)

#endif /* YOS_M3_MSVC_ATOMICS_H */
