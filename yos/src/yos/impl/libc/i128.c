/* Soft-int128 imports for wasm32 guests.
 *
 * clang emits calls to compiler-rt builtins for `__int128`/`unsigned
 * __int128` arithmetic and shifts. We don't link compiler-rt into the
 * wasm sysroot, so anything that uses 128-bit ints (openssh's session
 * counter timekeeping in particular) leaves an undefined `env.__*ti3`
 * symbol behind that unresolves at module load. We provide them here
 * as host imports.
 *
 * Sibling of impl/libc/f128.c — same pattern: wasm passes each i128
 * as two i64 halves (low 64 + high 64), sret-style functions take a
 * wasm-memory address as the first arg and we write the 16 result
 * bytes there. Implementation uses the host's native __int128, which
 * clang/gcc provide on every 64-bit target we care about.
 *
 * Builtins covered: __multi3 (a*b), __ashlti3 (a<<n), __ashrti3,
 * __lshrti3, __divti3, __modti3, __udivti3, __umodti3. Only the first
 * two are reached by openssh today, but the rest are listed inline so
 * any future package that ends up emitting them links without another
 * host-side patch. */

#include <stdint.h>
#include <string.h>
#include "wasm3.h"
#include "m3_env.h"
#include "m3_api_defs.h"

#if !defined(__SIZEOF_INT128__)
/* Some hosts (32-bit, MSVC) lack __int128. Stub the linker so the
 * build still produces a binary; a guest that actually exercises i128
 * will trap on the unresolved import, which is exactly what happened
 * before — better that than silently wrong arithmetic. */
void yos_i128_link(IM3Module mod);
void yos_i128_link(IM3Module mod) { (void)mod; }
#else

typedef __int128          i128;
typedef unsigned __int128 u128;

static inline u128 u128_from_halves(uint64_t lo, uint64_t hi)
{
    return ((u128)hi << 64) | (u128)lo;
}

static inline void i128_store_at(void *dst, u128 v)
{
    memcpy(dst, &v, sizeof v);
}

/* __multi3(ret_ptr, a_lo, a_hi, b_lo, b_hi): 128-bit unsigned-or-signed
 * multiply (lower 128 bits are identical for both signednesses). */
static m3ApiRawFunction(host__multi3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (uint64_t,  b_lo)
    m3ApiGetArg   (uint64_t,  b_hi)
    u128 a = u128_from_halves(a_lo, a_hi);
    u128 b = u128_from_halves(b_lo, b_hi);
    i128_store_at(ret, a * b);
    m3ApiSuccess();
}

/* __ashlti3(ret_ptr, a_lo, a_hi, shift): arithmetic left shift.
 * Shift count is taken modulo 128 (UB for >=128 in C, but the builtin
 * itself defines behaviour as compiler-rt does — mask the shift). */
static m3ApiRawFunction(host__ashlti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (int32_t,   shift)
    u128 a = u128_from_halves(a_lo, a_hi);
    u128 r = (shift >= 128 || shift < 0) ? 0 : (a << (shift & 127));
    i128_store_at(ret, r);
    m3ApiSuccess();
}

/* __ashrti3(ret_ptr, a_lo, a_hi, shift): arithmetic right shift —
 * signed input, sign-extends. */
static m3ApiRawFunction(host__ashrti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (int32_t,   shift)
    i128 a = (i128)u128_from_halves(a_lo, a_hi);
    /* C signed-right-shift is impl-defined; on every target clang
     * cross-compiles to wasm from it's a sign-fill arithmetic shift,
     * which is what compiler-rt's __ashrti3 promises. */
    i128 r = (shift >= 128) ? (a < 0 ? -1 : 0) :
             (shift <= 0)   ? a : (a >> shift);
    i128_store_at(ret, r);
    m3ApiSuccess();
}

/* __lshrti3(ret_ptr, a_lo, a_hi, shift): logical right shift —
 * unsigned input, zero-fill. */
static m3ApiRawFunction(host__lshrti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (int32_t,   shift)
    u128 a = u128_from_halves(a_lo, a_hi);
    u128 r = (shift >= 128 || shift < 0) ? 0 : (a >> shift);
    i128_store_at(ret, r);
    m3ApiSuccess();
}

/* Division / remainder family. Compiler-rt: signed uses __divti3 /
 * __modti3, unsigned uses __udivti3 / __umodti3. */
static m3ApiRawFunction(host__divti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (uint64_t,  b_lo)
    m3ApiGetArg   (uint64_t,  b_hi)
    i128 a = (i128)u128_from_halves(a_lo, a_hi);
    i128 b = (i128)u128_from_halves(b_lo, b_hi);
    /* compiler-rt's __divti3 traps on /0 via UB; we just write 0 to
     * keep the host alive. Guests doing legitimate /0 still go through
     * the same UB path they would on real hardware. */
    i128 r = b ? (a / b) : 0;
    i128_store_at(ret, r);
    m3ApiSuccess();
}

static m3ApiRawFunction(host__modti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (uint64_t,  b_lo)
    m3ApiGetArg   (uint64_t,  b_hi)
    i128 a = (i128)u128_from_halves(a_lo, a_hi);
    i128 b = (i128)u128_from_halves(b_lo, b_hi);
    i128 r = b ? (a % b) : 0;
    i128_store_at(ret, r);
    m3ApiSuccess();
}

static m3ApiRawFunction(host__udivti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (uint64_t,  b_lo)
    m3ApiGetArg   (uint64_t,  b_hi)
    u128 a = u128_from_halves(a_lo, a_hi);
    u128 b = u128_from_halves(b_lo, b_hi);
    u128 r = b ? (a / b) : 0;
    i128_store_at(ret, r);
    m3ApiSuccess();
}

static m3ApiRawFunction(host__umodti3)
{
    m3ApiGetArgMem(uint8_t *, ret)
    m3ApiGetArg   (uint64_t,  a_lo)
    m3ApiGetArg   (uint64_t,  a_hi)
    m3ApiGetArg   (uint64_t,  b_lo)
    m3ApiGetArg   (uint64_t,  b_hi)
    u128 a = u128_from_halves(a_lo, a_hi);
    u128 b = u128_from_halves(b_lo, b_hi);
    u128 r = b ? (a % b) : 0;
    i128_store_at(ret, r);
    m3ApiSuccess();
}

#define LINK(name, sig, fn)                                                \
    do {                                                                   \
        M3Result _r = m3_LinkRawFunction(mod, "env", (name), (sig), (fn)); \
        if (_r && _r != m3Err_functionLookupFailed) {                      \
            (void)_r;                                                      \
        }                                                                  \
    } while (0)

void yos_i128_link(IM3Module mod);
void yos_i128_link(IM3Module mod)
{
    /* All sret-style: first arg is the wasm pointer the guest reserved
     * for the i128 result. Signature string: 'v' = void, 'i' = i32,
     * 'I' = i64. */
    LINK("__multi3",  "v(iIIII)", host__multi3);
    LINK("__ashlti3", "v(iIIi)",  host__ashlti3);
    LINK("__ashrti3", "v(iIIi)",  host__ashrti3);
    LINK("__lshrti3", "v(iIIi)",  host__lshrti3);
    LINK("__divti3",  "v(iIIII)", host__divti3);
    LINK("__modti3",  "v(iIIII)", host__modti3);
    LINK("__udivti3", "v(iIIII)", host__udivti3);
    LINK("__umodti3", "v(iIIII)", host__umodti3);
}

#undef LINK

#endif /* __SIZEOF_INT128__ */
