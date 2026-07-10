/* impl/proc/sig-linux.c — Linux-only signal-wait bridges.
 *
 * sigwaitinfo + sigtimedwait both exist natively on Linux (and FreeBSD,
 * but we ship a Linux-host slice only for now — FreeBSD will get its
 * own sig-freebsd.c when we run on it).
 *
 * Helpers (yos_sigset_bound, fbsd_sigset_to_host, host_to_fbsd_signo)
 * live in sig.c and are declared in sig-internal.h. No #ifdef in here.
 */

#include "yos/types.h"
#include "impl/errno_helpers.h"
#include "impl/proc/sig-internal.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* FreeBSD-i386 siginfo_t is 64 bytes; timespec is 8. 64-bit math so a
 * near-end info_off / timeout_off can't wrap into a false pass. */
#define YOS_FBSD_SIGINFO_BYTES 64
#define YOS_FBSD_TIMESPEC_BYTES 8

int32_t yos_sigwaitinfo(struct yos_exec_ctx *ctx,
                        uint32_t set_off, uint32_t info_off)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    if (info_off &&
        (uint64_t)info_off + (uint64_t)YOS_FBSD_SIGINFO_BYTES
        > (uint64_t)ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);

    sigset_t host;
    fbsd_sigset_to_host(ctx->memory + set_off, &host);

    int rc = sigwaitinfo(&host, NULL);
    if (rc < 0) return yos_errno_neg(ctx, errno);

    /* Zero the wasm siginfo_t if provided so callers don't read stale
     * memory. */
    if (info_off)
        memset(ctx->memory + info_off, 0, YOS_FBSD_SIGINFO_BYTES);

    int fbsig = host_to_fbsd_signo(rc);
    return (fbsig > 0) ? fbsig : rc;
}

int32_t yos_sigtimedwait(struct yos_exec_ctx *ctx,
                         uint32_t set_off, uint32_t info_off,
                         uint32_t timeout_off)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    if (info_off &&
        (uint64_t)info_off + (uint64_t)YOS_FBSD_SIGINFO_BYTES
        > (uint64_t)ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);

    sigset_t host;
    fbsd_sigset_to_host(ctx->memory + set_off, &host);

    struct timespec ts, *tsp = NULL;
    if (timeout_off) {
        if ((uint64_t)timeout_off + (uint64_t)YOS_FBSD_TIMESPEC_BYTES
            > (uint64_t)ctx->memory_size)
            return yos_errno_neg(ctx, EFAULT);
        uint32_t s, n;
        memcpy(&s, ctx->memory + timeout_off,     4);
        memcpy(&n, ctx->memory + timeout_off + 4, 4);
        ts.tv_sec  = (time_t)(int32_t)s;
        ts.tv_nsec = (long)(int32_t)n;
        tsp = &ts;
    }

    int rc = sigtimedwait(&host, NULL, tsp);
    if (rc < 0) return yos_errno_neg(ctx, errno);

    if (info_off)
        memset(ctx->memory + info_off, 0, YOS_FBSD_SIGINFO_BYTES);

    int fbsig = host_to_fbsd_signo(rc);
    return (fbsig > 0) ? fbsig : rc;
}
