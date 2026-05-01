#ifndef YETTY_YCORE_MATH_H
#define YETTY_YCORE_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny generic numeric helpers. Prefixed with yetty_ so it is obvious at the
 * call site that these are our own typed inlines (not a libc/<math.h> import).
 *
 * Defined as `static inline` in the header AND marked
 * __attribute__((always_inline)) so the compiler is forced to inline them
 * even at -O0 / debug builds. No link-time symbol; also avoids the
 * double-evaluation pitfalls of MIN()/MAX() macros.
 */

#if defined(__GNUC__) || defined(__clang__)
#define YETTY_ALWAYS_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define YETTY_ALWAYS_INLINE static __forceinline
#else
#define YETTY_ALWAYS_INLINE static inline
#endif

YETTY_ALWAYS_INLINE uint32_t yetty_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

YETTY_ALWAYS_INLINE uint64_t yetty_min_u64(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

YETTY_ALWAYS_INLINE uint32_t yetty_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

YETTY_ALWAYS_INLINE uint64_t yetty_max_u64(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

YETTY_ALWAYS_INLINE float yetty_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_MATH_H */
