/* Soft-float128 imports for wasm32-musl guests.
 *
 * clang's wasm32 ABI uses `long double` = IEEE 754 binary128 (f128).
 * musl's printf, strtod, strtold, etc. exercise that path freely. We
 * don't link compiler-rt's soft-f128 builtins into the guest, so every
 * arithmetic / conversion / comparison gets emitted as a call to an
 * undefined `env.__*tf*` symbol. We provide them here as host imports.
 *
 * Implementation strategy: x86_64 hosts have native __float128 via
 * libquadmath. The wasm guest passes f128 as two i64 halves (low 64
 * + high 64 bits). We pun those halves directly into a __float128
 * via memcpy and use native operators / quadmath conversions.
 *
 * sret functions: when the wasm function returns an f128, the guest
 * reserves stack space and passes its wasm-memory address as the
 * first argument; we write the 16 result bytes there. */

#include <stdint.h>
#include <string.h>
#include "wasm3.h"
#include "m3_env.h"
#include "m3_api_defs.h"
#include "impl/proc/pthread.h"  /* not actually needed but keeps the link surface tidy */

#if !defined(__SIZEOF_FLOAT128__)
/* Hosts that lack __float128 (darwin x86_64 clang, windows MSVC):
 * stub yos_f128_link so the build links. Any guest that actually uses
 * long-double arithmetic will trap at runtime on the missing import,
 * which is preferable to silent precision loss. */
void yos_f128_link (IM3Module mod);
void yos_f128_link (IM3Module mod) { (void)mod; }
#else

typedef __float128 f128;
typedef double     f64;
typedef float      f32;

/* Reconstruct an f128 from the two i64 halves the wasm ABI passes. */
static inline f128 f128_from_halves (uint64_t lo, uint64_t hi)
{
    f128 r;
    uint64_t buf[2] = { lo, hi };
    memcpy (&r, buf, sizeof r);
    return r;
}

/* Write an f128 as two consecutive i64s into wasm memory at `addr`. */
static inline void f128_store_at (uint8_t *mem, uint32_t addr, f128 v)
{
    memcpy (mem + addr, &v, sizeof v);
}

/* Conversions to/from f128 ----------------------------------------------- */

/* __extenddftf2(ret_ptr, double a): a -> f128, stored at *ret_ptr. */
static m3ApiRawFunction (host__extenddftf2)
{
    m3ApiGetArgMem (uint8_t *, ret)
    m3ApiGetArg    (f64,       a)
    /* Cast: native __float128 widening of a double. */
    f128 v = (f128) a;
    memcpy (ret, &v, sizeof v);
    m3ApiSuccess ();
}

/* __extendsftf2(ret_ptr, float a): a -> f128. */
static m3ApiRawFunction (host__extendsftf2)
{
    m3ApiGetArgMem (uint8_t *, ret)
    m3ApiGetArg    (f32,       a)
    f128 v = (f128) a;
    memcpy (ret, &v, sizeof v);
    m3ApiSuccess ();
}

/* __trunctfdf2(a_lo, a_hi) -> double: f128 narrowing to double. */
static m3ApiRawFunction (host__trunctfdf2)
{
    m3ApiReturnType (f64)
    m3ApiGetArg     (uint64_t, lo)
    m3ApiGetArg     (uint64_t, hi)
    f128 v = f128_from_halves (lo, hi);
    m3ApiReturn ((f64) v);
}

/* __floatsitf(ret_ptr, int): convert i32 -> f128. */
static m3ApiRawFunction (host__floatsitf)
{
    m3ApiGetArgMem (uint8_t *, ret)
    m3ApiGetArg    (int32_t,   a)
    f128 v = (f128) a;
    memcpy (ret, &v, sizeof v);
    m3ApiSuccess ();
}

/* __floatunsitf(ret_ptr, uint32): unsigned i32 -> f128. */
static m3ApiRawFunction (host__floatunsitf)
{
    m3ApiGetArgMem (uint8_t *, ret)
    m3ApiGetArg    (uint32_t,  a)
    f128 v = (f128) a;
    memcpy (ret, &v, sizeof v);
    m3ApiSuccess ();
}

/* __floatditf(ret_ptr, int64): i64 -> f128. */
static m3ApiRawFunction (host__floatditf)
{
    m3ApiGetArgMem (uint8_t *, ret)
    m3ApiGetArg    (int64_t,   a)
    f128 v = (f128) a;
    memcpy (ret, &v, sizeof v);
    m3ApiSuccess ();
}

/* __floatunditf(ret_ptr, uint64): u64 -> f128. */
static m3ApiRawFunction (host__floatunditf)
{
    m3ApiGetArgMem (uint8_t *, ret)
    m3ApiGetArg    (uint64_t,  a)
    f128 v = (f128) a;
    memcpy (ret, &v, sizeof v);
    m3ApiSuccess ();
}

/* __fixtfdi(a_lo, a_hi) -> i64: f128 -> i64 (truncates). */
static m3ApiRawFunction (host__fixtfdi)
{
    m3ApiReturnType (int64_t)
    m3ApiGetArg     (uint64_t, lo)
    m3ApiGetArg     (uint64_t, hi)
    f128 v = f128_from_halves (lo, hi);
    m3ApiReturn ((int64_t) v);
}

/* __fixunstfdi(a_lo, a_hi) -> u64. */
static m3ApiRawFunction (host__fixunstfdi)
{
    m3ApiReturnType (uint64_t)
    m3ApiGetArg     (uint64_t, lo)
    m3ApiGetArg     (uint64_t, hi)
    f128 v = f128_from_halves (lo, hi);
    m3ApiReturn ((uint64_t) v);
}

/* __trunctfsf2(a_lo, a_hi) -> float: f128 -> float. */
static m3ApiRawFunction (host__trunctfsf2)
{
    m3ApiReturnType (f32)
    m3ApiGetArg     (uint64_t, lo)
    m3ApiGetArg     (uint64_t, hi)
    f128 v = f128_from_halves (lo, hi);
    m3ApiReturn ((f32) v);
}

/* __fixtfsi(a_lo, a_hi) -> int: convert f128 -> signed i32 (truncates). */
static m3ApiRawFunction (host__fixtfsi)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint64_t, lo)
    m3ApiGetArg     (uint64_t, hi)
    f128 v = f128_from_halves (lo, hi);
    m3ApiReturn ((int32_t) v);
}

/* __fixunstfsi(a_lo, a_hi) -> uint: convert f128 -> unsigned i32. */
static m3ApiRawFunction (host__fixunstfsi)
{
    m3ApiReturnType (uint32_t)
    m3ApiGetArg     (uint64_t, lo)
    m3ApiGetArg     (uint64_t, hi)
    f128 v = f128_from_halves (lo, hi);
    m3ApiReturn ((uint32_t) v);
}

/* Arithmetic ------------------------------------------------------------- */

#define F128_BINOP(name, op)                                                \
    static m3ApiRawFunction (host__##name)                                  \
    {                                                                       \
        m3ApiGetArgMem (uint8_t *, ret)                                     \
        m3ApiGetArg    (uint64_t,  a_lo)                                    \
        m3ApiGetArg    (uint64_t,  a_hi)                                    \
        m3ApiGetArg    (uint64_t,  b_lo)                                    \
        m3ApiGetArg    (uint64_t,  b_hi)                                    \
        f128 a = f128_from_halves (a_lo, a_hi);                             \
        f128 b = f128_from_halves (b_lo, b_hi);                             \
        f128 r = a op b;                                                    \
        memcpy (ret, &r, sizeof r);                                         \
        m3ApiSuccess ();                                                    \
    }

F128_BINOP(addtf3, +)
F128_BINOP(subtf3, -)
F128_BINOP(multf3, *)
F128_BINOP(divtf3, /)

/* Comparisons ------------------------------------------------------------ */

/* The *tf2 comparison ABI: 0 if equal/order, non-zero in the documented
 * sign for ordered relations. We compute the relation in native f128 and
 * map to the result code each callee expects:
 *   __eqtf2 / __netf2 : 0 iff equal (any non-zero means !=).
 *   __letf2 / __getf2 : <=0 iff a<=b ; >0 iff a>b (etc.); +1 for unordered.
 *   __unordtf2        : non-zero iff either operand is NaN. */

static m3ApiRawFunction (host__eqtf2)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint64_t, a_lo)
    m3ApiGetArg     (uint64_t, a_hi)
    m3ApiGetArg     (uint64_t, b_lo)
    m3ApiGetArg     (uint64_t, b_hi)
    f128 a = f128_from_halves (a_lo, a_hi);
    f128 b = f128_from_halves (b_lo, b_hi);
    m3ApiReturn ((int32_t)(a == b ? 0 : 1));
}

static m3ApiRawFunction (host__netf2)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint64_t, a_lo)
    m3ApiGetArg     (uint64_t, a_hi)
    m3ApiGetArg     (uint64_t, b_lo)
    m3ApiGetArg     (uint64_t, b_hi)
    f128 a = f128_from_halves (a_lo, a_hi);
    f128 b = f128_from_halves (b_lo, b_hi);
    m3ApiReturn ((int32_t)(a != b ? 1 : 0));
}

static m3ApiRawFunction (host__letf2)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint64_t, a_lo)
    m3ApiGetArg     (uint64_t, a_hi)
    m3ApiGetArg     (uint64_t, b_lo)
    m3ApiGetArg     (uint64_t, b_hi)
    f128 a = f128_from_halves (a_lo, a_hi);
    f128 b = f128_from_halves (b_lo, b_hi);
    /* Returns: <0 a<b, 0 a==b, >0 a>b, +1 unordered (NaN). */
    if (a == a && b == b) {  /* both ordered */
        m3ApiReturn ((int32_t)(a < b ? -1 : (a > b ? 1 : 0)));
    }
    m3ApiReturn ((int32_t) 1);
}

static m3ApiRawFunction (host__getf2)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint64_t, a_lo)
    m3ApiGetArg     (uint64_t, a_hi)
    m3ApiGetArg     (uint64_t, b_lo)
    m3ApiGetArg     (uint64_t, b_hi)
    f128 a = f128_from_halves (a_lo, a_hi);
    f128 b = f128_from_halves (b_lo, b_hi);
    if (a == a && b == b) {
        m3ApiReturn ((int32_t)(a < b ? -1 : (a > b ? 1 : 0)));
    }
    m3ApiReturn ((int32_t) -1);
}

static m3ApiRawFunction (host__unordtf2)
{
    m3ApiReturnType (int32_t)
    m3ApiGetArg     (uint64_t, a_lo)
    m3ApiGetArg     (uint64_t, a_hi)
    m3ApiGetArg     (uint64_t, b_lo)
    m3ApiGetArg     (uint64_t, b_hi)
    f128 a = f128_from_halves (a_lo, a_hi);
    f128 b = f128_from_halves (b_lo, b_hi);
    m3ApiReturn ((int32_t)(!(a == a) || !(b == b) ? 1 : 0));
}

/* ------------------------------------------------------------------------ */

#define LINK(name, sig, fn) \
    do { (void) m3_LinkRawFunction (mod, "env", name, sig, fn); } while (0)

void yos_f128_link (IM3Module mod);
void yos_f128_link (IM3Module mod)
{
    LINK ("__extenddftf2", "v(iF)",     host__extenddftf2);  /* (ret_ptr, f64) */
    LINK ("__extendsftf2", "v(if)",     host__extendsftf2);  /* (ret_ptr, f32) */
    LINK ("__trunctfdf2",  "F(II)",     host__trunctfdf2);   /* (lo,hi) -> f64 */
    LINK ("__floatsitf",   "v(ii)",     host__floatsitf);
    LINK ("__floatunsitf", "v(ii)",     host__floatunsitf);
    LINK ("__floatditf",   "v(iI)",     host__floatditf);
    LINK ("__floatunditf", "v(iI)",     host__floatunditf);
    LINK ("__fixtfsi",     "i(II)",     host__fixtfsi);
    LINK ("__fixunstfsi",  "i(II)",     host__fixunstfsi);
    LINK ("__fixtfdi",     "I(II)",     host__fixtfdi);
    LINK ("__fixunstfdi",  "I(II)",     host__fixunstfdi);
    LINK ("__trunctfsf2",  "f(II)",     host__trunctfsf2);

    LINK ("__addtf3",      "v(iIIII)",  host__addtf3);
    LINK ("__subtf3",      "v(iIIII)",  host__subtf3);
    LINK ("__multf3",      "v(iIIII)",  host__multf3);
    LINK ("__divtf3",      "v(iIIII)",  host__divtf3);

    LINK ("__eqtf2",       "i(IIII)",   host__eqtf2);
    LINK ("__netf2",       "i(IIII)",   host__netf2);
    LINK ("__letf2",       "i(IIII)",   host__letf2);
    LINK ("__getf2",       "i(IIII)",   host__getf2);
    LINK ("__unordtf2",    "i(IIII)",   host__unordtf2);
}

#undef LINK

#endif /* __SIZEOF_FLOAT128__ */
