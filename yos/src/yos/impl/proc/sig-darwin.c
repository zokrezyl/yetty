/* impl/proc/sig-darwin.c — darwin-shared signal-wait bridges
 * (macOS, iOS, tvOS, iOS Simulator — anything with libSystem).
 *
 * darwin has no sigwaitinfo (POSIX-2008 left it Linux-only by note)
 * and no sigtimedwait at all. Emulate sigwaitinfo via sigwait — same
 * blocking semantics, just no siginfo_t fill-out, which is fine since
 * we zero the wasm siginfo struct anyway. Return ENOSYS for the
 * bounded variant; nothing in the active wasm guest set uses it yet.
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

int32_t yos_sigwaitinfo(struct yos_exec_ctx *ctx,
                        uint32_t set_off, uint32_t info_off)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    /* FreeBSD-i386 siginfo_t is 64 bytes. 64-bit math so a near-end
     * info_off can't wrap into a false pass. */
    if (info_off &&
        (uint64_t)info_off + 64ULL > (uint64_t)ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);

    sigset_t host;
    fbsd_sigset_to_host(ctx->memory + set_off, &host);

    int sig = 0;
    int e = sigwait(&host, &sig);
    if (e != 0) return yos_errno_neg(ctx, e);

    /* Zero the wasm siginfo_t if provided so callers don't read stale
     * memory. */
    if (info_off)
        memset(ctx->memory + info_off, 0, 64);

    int fbsig = host_to_fbsd_signo(sig);
    return (fbsig > 0) ? fbsig : sig;
}

int32_t yos_sigtimedwait(struct yos_exec_ctx *ctx,
                         uint32_t set_off, uint32_t info_off,
                         uint32_t timeout_off)
{
    (void)ctx; (void)set_off; (void)info_off; (void)timeout_off;
    return yos_errno_neg(ctx, ENOSYS);
}
