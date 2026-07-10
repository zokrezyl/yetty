/* impl/strto.c — string-to-number family with safe endptr handling.
 *
 * The auto-generated bridges for strtol / strtoll / strtoul / strtoull /
 * strtoimax / strtoumax / strtod / strtof / strtold all mishandle the
 * `char **endptr` arg. The bridge body does:
 *
 *     char ** ep_h = (char **)(ctx->memory + ep_off);
 *     long _r = strtol(s, ep_h, base);
 *
 * Host strtol writes an 8-byte host pointer through ep_h; the wasm
 * guest's `char *endptr` slot is 4 bytes. Result: 4 bytes of adjacent
 * stack get clobbered — typically the next local or the stack canary
 * — and __stack_chk_fail trips a few frames later (nvim hit this in
 * its version-parsing path during startup).
 *
 * These shims call the host fn with a host-side scratch endptr, then
 * convert that host pointer back to a wasm offset and store the
 * 4-byte value through the guest's slot.
 *
 * Listed in hooks.yaml runtime_owned so bridge.py emits no body;
 * main.c calls yos_strto_link() to bind us.
 */

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"

extern int yos_remap_errno_h2g(int);

/* Convert a host pointer that lives inside the guest's linear memory
 * back to a wasm offset. Returns 0 if NULL or out-of-range. */
static inline uint32_t host_to_wasm_off(struct yos_exec_ctx *ctx, const void *p)
{
    if (!p) return 0;
    uintptr_t base = (uintptr_t)ctx->memory;
    uintptr_t hp   = (uintptr_t)p;
    if (hp < base || hp - base >= ctx->memory_size) return 0;
    return (uint32_t)(hp - base);
}

static inline void write_errno(struct yos_exec_ctx *ctx)
{
    if (errno && ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(errno);
    }
}

#define DEFINE_STRTO_INT(name, host_fn, ret_type, m3_ret_macro)             \
static m3ApiRawFunction(m3_yos_##name)                                       \
{                                                                            \
    m3_ret_macro                                                             \
    m3ApiGetArg(uint32_t, str_off);                                          \
    m3ApiGetArg(uint32_t, endp_off);                                         \
    m3ApiGetArg(int32_t,  base);                                             \
    struct yos_exec_ctx *ctx =                                               \
        (struct yos_exec_ctx *)m3_GetUserData(runtime);                      \
    uint32_t mem_size = 0;                                                   \
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);                       \
    ctx->memory_size = mem_size;                                             \
    if (!str_off || str_off >= mem_size) m3ApiReturn(0);                     \
    const char *s = (const char *)(ctx->memory + str_off);                   \
    char *host_end = NULL;                                                   \
    errno = 0;                                                               \
    ret_type _r = host_fn(s, endp_off ? &host_end : NULL, (int)base);        \
    write_errno(ctx);                                                        \
    if (endp_off && endp_off + 4 <= mem_size) {                              \
        *(uint32_t *)(ctx->memory + endp_off) =                              \
            host_to_wasm_off(ctx, host_end);                                 \
    }                                                                        \
    m3ApiReturn(_r);                                                         \
}

DEFINE_STRTO_INT(strtol,    strtol,    int32_t,  m3ApiReturnType(int32_t))
DEFINE_STRTO_INT(strtoll,   strtoll,   int64_t,  m3ApiReturnType(int64_t))
DEFINE_STRTO_INT(strtoul,   strtoul,   uint32_t, m3ApiReturnType(uint32_t))
DEFINE_STRTO_INT(strtoull,  strtoull,  uint64_t, m3ApiReturnType(uint64_t))
DEFINE_STRTO_INT(strtoimax, strtoimax, int64_t,  m3ApiReturnType(int64_t))
DEFINE_STRTO_INT(strtoumax, strtoumax, uint64_t, m3ApiReturnType(uint64_t))
DEFINE_STRTO_INT(strtoq,    strtoll,   int64_t,  m3ApiReturnType(int64_t))
DEFINE_STRTO_INT(strtouq,   strtoull,  uint64_t, m3ApiReturnType(uint64_t))

/* strtod/f/ld take (str, endptr) — no base. */
#define DEFINE_STRTO_FP(name, host_fn, ret_type, m3_ret_macro, m3_get_ret)  \
static m3ApiRawFunction(m3_yos_##name)                                       \
{                                                                            \
    m3_ret_macro                                                             \
    m3ApiGetArg(uint32_t, str_off);                                          \
    m3ApiGetArg(uint32_t, endp_off);                                         \
    struct yos_exec_ctx *ctx =                                               \
        (struct yos_exec_ctx *)m3_GetUserData(runtime);                      \
    uint32_t mem_size = 0;                                                   \
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);                       \
    ctx->memory_size = mem_size;                                             \
    if (!str_off || str_off >= mem_size) m3ApiReturn((ret_type)0);           \
    const char *s = (const char *)(ctx->memory + str_off);                   \
    char *host_end = NULL;                                                   \
    errno = 0;                                                               \
    ret_type _r = host_fn(s, endp_off ? &host_end : NULL);                   \
    write_errno(ctx);                                                        \
    if (endp_off && endp_off + 4 <= mem_size) {                              \
        *(uint32_t *)(ctx->memory + endp_off) =                              \
            host_to_wasm_off(ctx, host_end);                                 \
    }                                                                        \
    m3ApiReturn(_r);                                                         \
}

DEFINE_STRTO_FP(strtod, strtod, double, m3ApiReturnType(double), )
DEFINE_STRTO_FP(strtof, strtof, float,  m3ApiReturnType(float),  )

void yos_strto_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "strtol",    "i(iii)", m3_yos_strtol);
    m3_LinkRawFunction(mod, "env", "strtoll",   "I(iii)", m3_yos_strtoll);
    m3_LinkRawFunction(mod, "env", "strtoul",   "i(iii)", m3_yos_strtoul);
    m3_LinkRawFunction(mod, "env", "strtoull",  "I(iii)", m3_yos_strtoull);
    m3_LinkRawFunction(mod, "env", "strtoimax", "I(iii)", m3_yos_strtoimax);
    m3_LinkRawFunction(mod, "env", "strtoumax", "I(iii)", m3_yos_strtoumax);
    m3_LinkRawFunction(mod, "env", "strtoq",    "I(iii)", m3_yos_strtoq);
    m3_LinkRawFunction(mod, "env", "strtouq",   "I(iii)", m3_yos_strtouq);
    m3_LinkRawFunction(mod, "env", "strtod",    "F(ii)",  m3_yos_strtod);
    m3_LinkRawFunction(mod, "env", "strtof",    "f(ii)",  m3_yos_strtof);
}
