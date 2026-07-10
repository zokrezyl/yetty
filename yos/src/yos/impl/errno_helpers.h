/* impl/errno_helpers.h — small inline helpers for POSIX-conformant
 * error reporting from custom_<area> impls.
 *
 * The auto-generated bridges already do this dance after every host
 * libc call. Hand-written subsystem impls (impl/io/io.c, impl/proc.c,
 * impl/mem.c, …) used to return `-errno` directly, which broke
 * caller checks like `if (rc == -1) ...` (every POSIX wrapper does
 * this) and meant the FreeBSD `errno` macro never reflected the
 * actual error. These helpers let an impl say
 *
 *     return unlink(s) < 0 ? yos_errno_neg(ctx, errno) : 0;
 *
 * which is one expression, sets the per-ctx errno slot, and returns
 * -1 (POSIX). For getters that return uint or pointer values, see
 * yos_errno_zero() / yos_errno_null().
 */
#ifndef YOS_IMPL_ERRNO_HELPERS_H
#define YOS_IMPL_ERRNO_HELPERS_H

#include <stdint.h>
#include <string.h>  /* strerror */
#include <errno.h>   /* errno is thread-local — must come from glibc */

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

extern int yos_remap_errno_h2g(int host_errno);

/* Log every bridge error return at a level chosen by errno class.
 * Issue #10 — without this, every error-return is silent and a
 * misbehaving wasm guest takes a debugger to diagnose. Heuristic:
 *   EFAULT, ENOSYS, EMFILE → yerror (yos-internal cause)
 *   everything else        → ywarn (host call returned the errno)
 * Caller's __func__ name shows up via the wrapper macros below. */
static inline int yos_errno_is_internal(int e)
{
    return e == EFAULT || e == ENOSYS || e == EMFILE;
}

static inline void yos__errno_log(const char *fn, int e)
{
    if (yos_errno_is_internal(e))
        yerror("%s: errno=%d (%s)", fn, e, strerror(e));
    else
        ywarn ("%s: errno=%d (%s)", fn, e, strerror(e));
}

static inline int32_t yos__errno_neg(struct yos_exec_ctx *ctx, int host_errno,
                                     const char *fn)
{
    yos__errno_log(fn, host_errno);
    if (ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) =
            yos_remap_errno_h2g(host_errno);
    }
    return -1;
}

static inline uint32_t yos__errno_null(struct yos_exec_ctx *ctx, int host_errno,
                                       const char *fn)
{
    yos__errno_log(fn, host_errno);
    if (ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) =
            yos_remap_errno_h2g(host_errno);
    }
    return 0;
}

static inline int32_t yos__errno_check(struct yos_exec_ctx *ctx, int rc, int e,
                                       const char *fn)
{
    if (rc < 0) return yos__errno_neg(ctx, e, fn);
    return (int32_t)rc;
}

/* Write the host errno to the per-ctx slot (so the FreeBSD `errno`
 * macro reads correctly), return -1. Use for int-returning fns. Macro
 * wrappers capture __func__ so the log line names the actual bridge,
 * not the helper. */
#define yos_errno_neg(ctx, e)   yos__errno_neg((ctx), (e), __func__)
#define yos_errno_null(ctx, e)  yos__errno_null((ctx), (e), __func__)
/* `rc` is typically the host libc call itself (e.g. connect(...)), which
 * sets `errno` as a side effect. Function-argument evaluation order is
 * unspecified in C, so `yos__errno_check((ctx), (rc), errno, ...)` may read
 * `errno` BEFORE evaluating `rc` — capturing a stale errno (0) instead of
 * the one the call just set. Sequence `rc` first via a temporary so `errno`
 * is read only after the call has run. */
#define yos_errno_check(ctx, rc) \
    __extension__({ int32_t yos_errno_rc_ = (rc); \
                    yos__errno_check((ctx), yos_errno_rc_, errno, __func__); })

#endif /* YOS_IMPL_ERRNO_HELPERS_H */
