/* impl/proc/sig-windows.c — Windows-host signal-wait stubs.
 *
 * Windows has no POSIX sigwaitinfo / sigtimedwait. The wasm guest's
 * libuv-style code paths that probe for these get a clean ENOSYS so
 * they can fall back to non-signal-driven event loops (kqueue on
 * Windows is also stubbed; libuv builds for the FreeBSD-shaped guest
 * never reach into either of these on Windows by design).
 *
 * NO #ifdef in this file — meson selects it only on windows hosts.
 */

#include "yos/types.h"
#include "impl/errno_helpers.h"

#include <errno.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* __builtin_ctz body lives in platform/windows/compat/compat_libc.c. */
extern unsigned __builtin_ctz(unsigned x);

int32_t yos_sigwaitinfo(struct yos_exec_ctx *ctx,
                        uint32_t set_off, uint32_t info_off)
{
    (void)info_off;
    /* sigwaitinfo returns the FreeBSD signal number directly (not
     * 0+sig in an out param like sigwait). Poll ctx->sig_pending for
     * a bit in `set` — same loop as yos_sigwait but with a different
     * return convention. siginfo_t (info_off) is left zero-initialised
     * by the codegen — the wasm guest's siginfo_t is opaque to yos
     * and only the return value is consulted by uv probes. */
    if (!ctx || !ctx->memory) return yos_errno_neg(ctx, EFAULT);
    if (set_off + 16 > ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    /* sigset uses bit = signo-1 (FreeBSD); sig_pending uses bit = signo.
     * See sigmask_apply in sig.c for the full rationale. */
    uint32_t want_fbsd = *(const uint32_t *)(ctx->memory + set_off);
    if (!want_fbsd) return yos_errno_neg(ctx, EINVAL);
    uint32_t want_lo = want_fbsd << 1;
    extern void yos_signal_pump(struct yos_exec_ctx *);
    for (;;) {
        uint32_t pend = __atomic_load_n(&ctx->sig_pending, __ATOMIC_ACQUIRE);
        uint32_t hit = pend & want_lo;
        if (hit) {
            int s = __builtin_ctz(hit);
            uint32_t bit = 1u << s;
            __atomic_and_fetch(&ctx->sig_pending, ~bit, __ATOMIC_ACQ_REL);
            return s;
        }
        if (pend & (1u << 9 /* SIGKILL */)) {
            yos_signal_pump(ctx);
            return yos_errno_neg(ctx, EINTR);
        }
        Sleep(1);
    }
}

int32_t yos_sigtimedwait(struct yos_exec_ctx *ctx,
                         uint32_t set_off, uint32_t info_off,
                         uint32_t timeout_off)
{
    (void)set_off; (void)info_off;
    /* Zero-timeout probe ("any signal queued right now?") is the
     * common libc-coverage / event-loop check. Windows has no signal
     * queue, so the honest answer is "nothing queued" — POSIX maps to
     * -1/EAGAIN. Bridges with a non-zero timeout get ENOSYS, since
     * actually blocking on signals isn't something we can synthesise. */
    if (timeout_off && timeout_off + 8 <= ctx->memory_size) {
        const uint8_t *p = ctx->memory + timeout_off;
        int32_t tv_sec  = *(const int32_t *)(p + 0);
        int32_t tv_nsec = *(const int32_t *)(p + 4);
        if (tv_sec == 0 && tv_nsec == 0)
            return yos_errno_neg(ctx, EAGAIN);
    }
    return yos_errno_neg(ctx, ENOSYS);
}
