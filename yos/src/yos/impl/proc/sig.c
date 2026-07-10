#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include <stdint.h>
#include <string.h>     /* memset for sigemptyset/sigfillset */
#include <errno.h>
#include <pthread.h>
#include <signal.h>     /* host sigset_t, SIGHUP etc. for pthread_sigmask */
#include <time.h>
#include <unistd.h>     /* usleep */

#include "wasm3.h"
#include "m3_env.h"
#include "impl/errno_helpers.h"  /* yos_remap_errno_h2g for pthread_sigmask */

/* Signal handling. State lives on struct yos_exec_ctx: handler table,
 * blocked mask, and pending mask are all per-process. fork() inherits
 * everything from the parent; execve() preserves the mask and resets
 * the handler table (custom → SIG_DFL, SIG_IGN preserved per POSIX). */
#define YOS_NSIG 32
/* FreeBSD sigset_t is `uint32_t __bits[4]` — 16 bytes total. yos
 * only tracks signums 1..31, so all the relevant bits live in
 * __bits[0]; the other 12 bytes get zeroed on every write. */
#define YOS_FBSD_SIGSET_BYTES 16

static void record_handler(struct yos_exec_ctx *ctx, int signum,
                           uint32_t handler_idx)
{
    if (!ctx || signum <= 0 || signum >= YOS_NSIG) return;
    /* Split the FreeBSD sentinel values out of the table-index slot so
     * a real wasm function pointer at table index 1 can coexist with
     * SIG_IGN (which is also encoded as 1 in the FreeBSD ABI). The
     * caller writes:
     *   0   → SIG_DFL  (clear ignore bit, set idx=0)
     *   1   → SIG_IGN  (set ignore bit, idx unused but reset to 0)
     *   ≥ 2 → real wasm function-table index
     * invoke_signal_handler reads sig_ignore_mask FIRST, then dispatches
     * via sig_handlers[]. */
    if (handler_idx == 0u /* SIG_DFL */) {
        ctx->sig_ignore_mask &= ~(1u << signum);
        ctx->sig_handlers[signum] = 0;
    } else if (handler_idx == 1u /* SIG_IGN */) {
        ctx->sig_ignore_mask |= (1u << signum);
        ctx->sig_handlers[signum] = 0;
    } else {
        ctx->sig_ignore_mask &= ~(1u << signum);
        ctx->sig_handlers[signum] = handler_idx;
    }
}

static inline uint32_t signum_bit(int signum)
{
    return (signum > 0 && signum < YOS_NSIG) ? (1u << signum) : 0u;
}

/* Async signal forwarding from the host.
 *
 * When the user hits Ctrl-C (or any tty-driver signal — SIGINT,
 * SIGQUIT, SIGTSTP, SIGWINCH, …) the KERNEL delivers it to yos's
 * host process, not to any wasm guest directly. Without explicit
 * handling the kernel default kicks in (SIGINT/QUIT terminate; the
 * whole yos process dies, taking every guest with it).
 *
 * We want the signal forwarded to the foreground guest proc's
 * recorded wasm handler instead, then the host runtime stays alive.
 *
 * Async-safety: signal handlers can't call m3_CallV (it locks
 * pthread mutexes and walks runtime state). Two-stage delivery:
 *
 *   1. Host signal handler (installed by yos_install_host_signal_
 *      handlers in main.c) sets a bit in g_host_pending_signals.
 *   2. Any wasm thread, on its way out of a blocking syscall
 *      (yos_read et al.) calls yos_signal_pump(ctx); the pump
 *      drains the pending set and invokes the foreground guest's
 *      wasm-side handler synchronously. */
static volatile uint32_t g_host_pending_signals;  /* atomic bitmask, FreeBSD signums */

void yos_signal_set_pending(int fbsd_signum)
{
    if (fbsd_signum <= 0 || fbsd_signum >= YOS_NSIG) return;
    __atomic_or_fetch(&g_host_pending_signals, 1u << fbsd_signum,
                      __ATOMIC_RELEASE);
}

/* Invoke the wasm-side handler for `signum` (if any). Function table
 * index was recorded by sigaction; we look it up in the same way
 * impl/callback.c does for qsort comparators. Bails silently on
 * any error — the caller is sigsuspend, and we still want to return
 * EINTR so the wait loop sees forward progress. */
/* Special FreeBSD signal(3) handler values. */
#define YOS_SIG_DFL ((uint32_t)0)
#define YOS_SIG_IGN ((uint32_t)1)
#define YOS_SIG_ERR ((uint32_t)0xffffffffu)

/* FreeBSD default action for each signal. "T" = terminate the
 * process; "I" = ignore; "S" = stop; "C" = continue. Indexed by
 * FreeBSD signum (1..31). Only "T" matters for the SIG_DFL path —
 * we already drop ignored and stop/continue signals. Source:
 * sigaction(2) manual page on FreeBSD. */
static int default_action_is_terminate(int signum)
{
    switch (signum) {
        case  1: /* SIGHUP   */
        case  2: /* SIGINT   */
        case  3: /* SIGQUIT  */
        case  4: /* SIGILL   */
        case  5: /* SIGTRAP  */
        case  6: /* SIGABRT  */
        case  7: /* SIGEMT   */
        case  8: /* SIGFPE   */
        case  9: /* SIGKILL  */
        case 10: /* SIGBUS   */
        case 11: /* SIGSEGV  */
        case 12: /* SIGSYS   */
        case 13: /* SIGPIPE  */
        case 14: /* SIGALRM  */
        case 15: /* SIGTERM  */
        case 24: /* SIGXCPU  */
        case 25: /* SIGXFSZ  */
        case 26: /* SIGVTALRM */
        case 27: /* SIGPROF  */
        case 30: /* SIGUSR1  */
        case 31: /* SIGUSR2  */
            return 1;
        default:
            return 0;
    }
}

static void invoke_signal_handler(struct yos_exec_ctx *ctx, int signum)
{
    if (!ctx || signum <= 0 || signum >= YOS_NSIG) return;
    /* SIG_IGN is tracked in a separate bitmask so the FreeBSD ABI
     * value (sa_handler == 1) doesn't collide with wasm function-
     * table index 1 (where small wasm modules place their first
     * indirectly-called function). See types.h::sig_ignore_mask. */
    if (ctx->sig_ignore_mask & (1u << signum)) return;
    uint32_t idx = ctx->sig_handlers[signum];
    ydebug("invoke_signal_handler: sig=%d idx=%u\n", signum, idx);
    /* idx == 0 means SIG_DFL (no handler registered). Wasm function
     * table index 0 is reserved by the wasm ABI as the null sentinel
     * (indirect-call through 0 traps), so this never aliases a real
     * handler.
     *
     * For SIG_DFL on a "terminate" signal, POSIX says the process
     * dies. yos's model: mark the proc as ZOMBIE, encode term_sig
     * so waitpid sees WIFSIGNALED. For the ROOT proc (pid 1) we
     * also _exit() the yos host — pthread_exit alone is not enough
     * on darwin because main-macos.c spawns a daemon Mach exception
     * thread (mach_exc_thread), so killing only the wasm thread
     * leaves the host process alive with no progress, making the
     * guest unkillable via Ctrl-C. For forked-child procs (pid > 1)
     * we keep the pthread_exit-only path so the parent yos host
     * stays up and reaps the child via waitpid. */
    if (idx == YOS_SIG_DFL) {
        if (default_action_is_terminate(signum) && ctx->proc) {
            ctx->proc->state     = YOS_PROC_ZOMBIE;
            ctx->proc->exit_code = 0;
            ctx->proc->term_sig  = signum;
            ctx->proc->exited    = 1;
            pthread_cond_broadcast(&ctx->proc->wait_cond);
            if (ctx->proc->pid <= 1) {
                /* Root guest. Bring the whole host down with the
                 * shell convention 128+signum so a parent shell
                 * reports the signal correctly. */
                _exit(128 + signum);
            }
            /* Same post-exit cleanup as yos_exit: reparent orphans
             * and auto-reap if parent ignores SIGCHLD or is init.
             * Without calling this, kill-by-signal leaves zombies
             * the user's `ps` will keep seeing across sessions. */
            extern void yos_proc_post_exit_cleanup(struct yos_exec_ctx *);
            yos_proc_post_exit_cleanup(ctx);
            pthread_exit(NULL);
        }
        return;
    }
    IM3Runtime rt = (IM3Runtime)ctx->runtime;
    if (!rt) return;
    IM3Module mod = rt->modules;
    if (!mod || idx >= mod->table0Size) {
        ydebug("invoke_signal_handler: idx %u out of table size %u\n",
               idx, mod ? mod->table0Size : 0);
        return;
    }
    IM3Function fn = mod->table0[idx];
    if (!fn) { ydebug("invoke_signal_handler: table[%u] is NULL\n", idx); return; }
    if (!fn->compiled && CompileFunction(fn) != NULL) {
        ydebug("invoke_signal_handler: CompileFunction failed\n");
        return;
    }
    /* sa_handler is `void (*)(int)`; pass the signum. */
    M3Result r = m3_CallV(fn, (uint32_t)signum);
    ydebug("invoke_signal_handler: m3_CallV returned %s\n", r ? r : "OK");
}

/* Drain the host-side pending-signal bitmask, dispatching each set
 * bit to this proc's wasm handler. Called from yos_read (and other
 * places where a guest thread is naturally about to re-enter wasm)
 * to deliver host-originated SIGINT/SIGQUIT/SIGWINCH/SIGTSTP. The
 * foreground-vs-background distinction is intentionally loose here:
 * every wasm thread that reaches the pump drains the same global
 * set, so the FIRST one to drain wins. That's what we want for
 * Ctrl-C at zsh's prompt — zsh is the one blocked in read() and
 * gets the signal. Multi-process delivery to non-foreground procs
 * would need per-proc pending sets keyed on `proc->pgid ==
 * rt->fg_pgid`; ignore for now. */
void yos_signal_pump(struct yos_exec_ctx *ctx)
{
    if (!ctx) return;
    /* Pull anything the host handler latched into the process-wide
     * incoming queue and merge into this ctx's per-process pending
     * set. The host has no easy way to identify a destination ctx —
     * whichever proc pumps first wins, which matches the tty model
     * (foreground proc reads first → it sees Ctrl-C). */
    uint32_t fresh = __atomic_exchange_n(&g_host_pending_signals, 0,
                                          __ATOMIC_ACQ_REL);
    if (fresh) __atomic_or_fetch(&ctx->sig_pending, fresh, __ATOMIC_ACQ_REL);

    /* SIGKILL is uncatchable AND unmaskable per POSIX. Handle it
     * before the normal deliverable-bits loop so the guest can't
     * accidentally (or maliciously) sigprocmask itself unkillable.
     * Terminating here uses pthread_exit instead of pthread_cancel,
     * which leaves wasm3's per-runtime state in a consistent
     * shape and avoids the m3Error-crash-in-next-fork hazard that
     * pthread_cancel caused. */
    uint32_t pend = __atomic_load_n(&ctx->sig_pending, __ATOMIC_ACQUIRE);
    if (pend & (1u << 9)) {
        if (ctx->proc) {
            ctx->proc->state     = YOS_PROC_ZOMBIE;
            ctx->proc->exit_code = 0;
            ctx->proc->term_sig  = 9;
            ctx->proc->exited    = 1;
            pthread_cond_broadcast(&ctx->proc->wait_cond);
            if (ctx->proc->pid <= 1) _exit(128 + 9);
        }
        pthread_exit(NULL);
    }

    /* Deliver every pending signal that isn't blocked. Each delivery
     * clears the pending bit atomically (kill() from another thread
     * may set bits concurrently); blocked bits stay set and get
     * picked up the next time sigprocmask unblocks them or sigsuspend
     * runs with the right mask. */
    for (;;) {
        uint32_t cur = __atomic_load_n(&ctx->sig_pending, __ATOMIC_ACQUIRE);
        uint32_t deliverable = cur & ~ctx->sig_mask;
        if (!deliverable) break;
        int s = __builtin_ctz(deliverable);
        uint32_t bit = 1u << s;
        __atomic_and_fetch(&ctx->sig_pending, ~bit, __ATOMIC_ACQ_REL);
        invoke_signal_handler(ctx, s);
    }
}

int32_t yos_sig_rt_sigaction(struct yos_exec_ctx *ctx, int32_t signum,
                              uint32_t act, uint32_t oldact, uint32_t sigsetsize)
{
    (void)sigsetsize;
    if (!ctx || signum <= 0 || signum >= YOS_NSIG)
        return yos_errno_neg(ctx, EINVAL);
    /* Materialise the FreeBSD-ABI value for the previous handler from
     * yos's split representation. If the signal was SIG_IGN, the
     * FreeBSD ABI expects sa_handler == 1; if it was a registered
     * function, return the wasm-table index; otherwise SIG_DFL == 0. */
    uint32_t prev_abi;
    if (ctx->sig_ignore_mask & (1u << signum)) prev_abi = 1u;       /* SIG_IGN */
    else                                       prev_abi = ctx->sig_handlers[signum];
    if (act && ctx->memory && act + 4 <= ctx->memory_size) {
        /* FreeBSD i386 struct sigaction: sa_handler/sa_sigaction is at
         * offset 0; the value is a wasm function-table index, NOT a
         * host pointer. */
        uint32_t handler = *(uint32_t *)(ctx->memory + act);
        record_handler(ctx, signum, handler);
    }
    /* FreeBSD i386 struct sigaction is exactly 24 bytes:
     *   sa_handler (4) + sa_flags (4) + sa_mask/sigset_t __bits[4] (16).
     * The guest allocates sizeof(struct sigaction) == 24 for oldact;
     * writing 32 here overran the buffer by 8 bytes and corrupted the
     * adjacent allocation (it clobbered the heap free-list header,
     * which silently destroyed the whole guest heap). Write 24. */
    if (oldact && ctx->memory && oldact + 24 <= ctx->memory_size) {
        uint8_t *p = ctx->memory + oldact;
        memset(p, 0, 24);
        *(uint32_t *)p = prev_abi;   /* sa_handler in FreeBSD-ABI encoding */
    }
    return 0;
}

/* Read the FreeBSD-shape sigset_t at `off` into a uint32 holding bits
 * for signums 1..31. Returns 0 if off is 0 / out of bounds.
 *
 * Layout: FreeBSD sigset_t is `uint32_t __bits[4]` — 16 bytes total,
 * little-endian; signum N (1..128) is bit (N-1) of __bits[(N-1)/32].
 * yos only tracks 1..31, so we just read __bits[0] and ignore the
 * upper signums (POSIX realtime sigs aren't delivered anyway). */
static uint32_t read_fbsd_sigset_lo(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (!off || !ctx ||
        (uint64_t)off + (uint64_t)YOS_FBSD_SIGSET_BYTES > (uint64_t)ctx->memory_size)
        return 0;
    return *(uint32_t *)(ctx->memory + off);
}

/* Write a uint32 mask back into a FreeBSD-shape sigset_t at `off`.
 * Zeros the upper signum slots so a sigset_t round-tripped through
 * yos doesn't carry stale bits there. */
static void write_fbsd_sigset_lo(struct yos_exec_ctx *ctx, uint32_t off,
                                 uint32_t lo)
{
    if (!off || !ctx ||
        (uint64_t)off + (uint64_t)YOS_FBSD_SIGSET_BYTES > (uint64_t)ctx->memory_size)
        return;
    uint8_t *p = ctx->memory + off;
    *(uint32_t *)p = lo;
    memset(p + 4, 0, YOS_FBSD_SIGSET_BYTES - 4);
}

/* Apply a FreeBSD-shape sigprocmask op to ctx->sig_mask. After the
 * op, runs the pump so anything that just got unblocked fires.
 *   how = SIG_BLOCK(1)/SIG_UNBLOCK(2)/SIG_SETMASK(3) — FreeBSD numbering.
 * SIGKILL and SIGSTOP can never be blocked (POSIX) — strip those bits
 * silently from any incoming mask. */
#define YOS_FBSD_SIG_BLOCK    1
#define YOS_FBSD_SIG_UNBLOCK  2
#define YOS_FBSD_SIG_SETMASK  3
#define YOS_FBSD_SIGKILL      9
#define YOS_FBSD_SIGSTOP      17

static int32_t sigmask_apply(struct yos_exec_ctx *ctx, int32_t how,
                             uint32_t set_off, uint32_t oset_off)
{
    if (!ctx) return yos_errno_neg(ctx, EINVAL);
    uint32_t prev = ctx->sig_mask;
    if (set_off) {
        /* Wasm sigset_t layout (FreeBSD libc convention): bit position
         * is (signo - 1) — sigaddset(set, 30) sets bit 29. yos's
         * internal sig_mask / sig_pending instead use bit = signo, so
         * sig_handlers[30] holds the SIGUSR1 handler and deliver_to_-
         * proc sets sig_pending |= (1 << 30). Convert by shifting the
         * sigset's low word left by 1 — bit (signo-1) -> bit signo.
         * Bit 31 of the sigset (signo 32) lands outside the uint32 mask
         * width; signal 32 isn't used in our model so it drops cleanly.
         *
         * Without this conversion, blocking SIGUSR1 via pthread_sigmask
         * sets sig_mask bit 29, but a subsequent raise() lands bit 30
         * in sig_pending, signal_pump sees ~mask leaves it deliverable,
         * and SIG_DFL=TERMINATE kills the proc before sigwait can
         * consume the signal. */
        uint32_t fbsd = read_fbsd_sigset_lo(ctx, set_off);
        uint32_t s = fbsd << 1;   /* signo = bit + 1 */
        s &= ~((1u << YOS_FBSD_SIGKILL) | (1u << YOS_FBSD_SIGSTOP));
        switch (how) {
        case YOS_FBSD_SIG_BLOCK:   ctx->sig_mask = prev | s;  break;
        case YOS_FBSD_SIG_UNBLOCK: ctx->sig_mask = prev & ~s; break;
        case YOS_FBSD_SIG_SETMASK: ctx->sig_mask = s;         break;
        default:                   return yos_errno_neg(ctx, EINVAL);
        }
    }
    /* Convert sig_mask (bit=signo) back to FreeBSD sigset_t
     * (bit=signo-1) for the oset out-param, matching the input
     * conversion above. */
    if (oset_off) write_fbsd_sigset_lo(ctx, oset_off, prev >> 1);
    /* Anything we just unblocked may already be pending — fire it now. */
    if (ctx->sig_pending & ~ctx->sig_mask)
        yos_signal_pump(ctx);
    return 0;
}

int32_t yos_sig_rt_sigprocmask(struct yos_exec_ctx *ctx, int32_t how,
                                uint32_t set, uint32_t oset, uint32_t sigsetsize)
{
    (void)sigsetsize;
    return sigmask_apply(ctx, how, set, oset);
}

int32_t yos_sigaction(struct yos_exec_ctx *ctx, int32_t signum,
                           uint32_t act, uint32_t oldact)
{
    return yos_sig_rt_sigaction(ctx, signum, act, oldact, 0);
}

/* ── signal(int, void (*)(int)) → void (*)(int) ──────────────────────
 *
 * BSD-style signal(3): record the wasm-side handler index, return the
 * previously-installed one. Same handler table as sigaction (we don't
 * distinguish signal() vs sigaction() callers — both end up in the same
 * yos_sigsuspend-synthesised SIGCHLD dispatch path).
 *
 * The handler arg + return value are function-table indices in wasm
 * land. The caller passes SIG_DFL=0 / SIG_IGN=1 as raw small ints;
 * those flow through unchanged and invoke_signal_handler treats them
 * as "skip" (see the SIG_DFL/SIG_IGN guard there).
 *
 * Signum is validated against YOS_NSIG (32); out-of-range returns
 * SIG_ERR=0xFFFFFFFF, matching what the FreeBSD libc-level shim would
 * expect for an EINVAL response. */
uint32_t yos_signal(struct yos_exec_ctx *ctx, int32_t signum, uint32_t handler)
{
    if (!ctx || signum <= 0 || signum >= YOS_NSIG) return YOS_SIG_ERR;
    uint32_t old;
    if (ctx->sig_ignore_mask & (1u << signum)) old = YOS_SIG_IGN;
    else                                       old = ctx->sig_handlers[signum];
    record_handler(ctx, signum, handler);
    return old;
}

int32_t yos_sigprocmask(struct yos_exec_ctx *ctx, int32_t how,
                             uint32_t set, uint32_t oset)
{
    return sigmask_apply(ctx, how, set, oset);
}

/* sigsuspend — block "until a signal arrives". yos doesn't actually
 * deliver SIGCHLD on child exit (children are sibling pthreads; their
 * completion broadcasts a per-proc cond_var instead). zsh's standard
 * fork+wait pattern is:
 *
 *     block(SIGCHLD); sigsuspend(empty); waitpid(-1, ...);
 *
 * If sigsuspend doesn't block at all (the previous stub returned
 * ENOSYS), zsh interprets the unblock as a spurious wake, calls
 * sigsuspend again, and busy-loops forever — every external command
 * hangs the shell.
 *
 * Minimum-viable fix: poll the proc table for any child of ours that
 * has reached ZOMBIE state, sleeping briefly between checks. When one
 * appears (or after a short timeout — important for shells doing
 * `wait` with no children) return -1 with errno=EINTR so the caller's
 * loop falls through to waitpid. This is the same poll-with-usleep
 * shape yos_waitpid already uses (see impl/proc.c) — staying
 * consistent with existing yos process-table mechanics. */
/* sigsuspend — atomically install temp mask, wait for an unblocked
 * signal, restore mask, return -1 + EINTR. We poll the proc table for
 * ZOMBIE children of this proc and synthesise SIGCHLD when one
 * appears (yos's only signal-delivery driver besides host-forwarded
 * SIGINT/SIGWINCH which already enter via signal_pump). The temp
 * mask is enforced via signal_pump's blocked-bit filter; any
 * unblocked pending bit set during the wait (from kill() or the
 * host signal handler) gets delivered. */
int32_t yos_sigsuspend(struct yos_exec_ctx *ctx, uint32_t mask_off)
{
    if (!ctx || !ctx->proc || !ctx->rt)
        return yos_errno_neg(ctx, EINVAL);

    uint32_t saved_mask = ctx->sig_mask;
    if (mask_off) {
        uint32_t m = read_fbsd_sigset_lo(ctx, mask_off);
        m &= ~((1u << YOS_FBSD_SIGKILL) | (1u << YOS_FBSD_SIGSTOP));
        ctx->sig_mask = m;
    }

    /* Drain anything already pending under the new mask before we
     * start waiting; matches the POSIX "deliver one immediately if
     * one was pending" semantic. */
    yos_signal_pump(ctx);

    /* Poll for SIGCHLD-worthy events. Each iteration also runs the
     * pump in case kill()/host signal landed something pending. We
     * bail as soon as we either delivered a signal (pending cleared)
     * or saw a zombie child. */
    struct yos_runtime *rt = ctx->rt;
    int32_t my_pid = ctx->proc->pid;
    for (int iter = 0; iter < 200; iter++) {
        pthread_mutex_lock(&rt->proc_lock);
        int has_zombie = 0;
        for (int i = 0; i < YOS_MAX_PROCS; i++) {
            struct yos_proc *p = &rt->procs[i];
            if (p->state == YOS_PROC_ZOMBIE && p->ppid == my_pid) {
                has_zombie = 1; break;
            }
        }
        pthread_mutex_unlock(&rt->proc_lock);
        if (has_zombie) {
            ctx->sig_pending |= (1u << 20);   /* FreeBSD SIGCHLD */
            yos_signal_pump(ctx);
            break;
        }
        if (ctx->sig_pending & ~ctx->sig_mask) break;
        usleep(5000);
    }

    /* POSIX: restore the original mask, regardless of whether a
     * signal fired. Any signal that arrived during the suspend that
     * we couldn't deliver (blocked under the original mask too) stays
     * in sig_pending for later. */
    ctx->sig_mask = saved_mask;
    return yos_errno_neg(ctx, EINTR);
}

/* ── sigemptyset / sigfillset / sigaddset / sigdelset / sigismember
 *
 * FreeBSD `sigset_t` is `__uint32_t __bits[4]` — 16 bytes total,
 * holding 128 bits (signal numbers 1..128). These are pure userspace
 * bitmap manipulators: zero/fill/set-bit/clear-bit/test-bit on the
 * 128-bit blob the wasm guest hands us. No host call, no host
 * sigset_t involved (Linux's sigset_t is 128 bytes, totally different
 * layout — we MUST do the manipulation on the wasm-side bytes
 * directly, otherwise we'd corrupt 7 sigset_ts past the boundary).
 *
 * Auto-bridge stubs these because of the size mismatch — at the
 * declaration level the bridge generator can't tell that the
 * intended manipulation is purely on the FreeBSD layout.
 */

/* sig-linux.c / sig-darwin.c use this; declared in sig-internal.h.
 * 64-bit math so a near-end offset + sigset size can't wrap into a
 * pass on 32-bit guests where uint32_t addition silently overflows. */
int yos_sigset_bound(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (off == 0) return 0;
    return (uint64_t)off + (uint64_t)YOS_FBSD_SIGSET_BYTES
           <= (uint64_t)ctx->memory_size;
}

/* POSIX sig{empty,fill,add,del,ismember}set: return 0 / -1 with
 * errno set, NOT -errno. Use yos_errno_neg so the per-ctx errno slot
 * gets the FreeBSD-shape value and any `if (sigfillset(&s) == -1)`
 * in the guest catches the failure. (pthread_sigmask is the odd one
 * out: it returns the errno as a positive value.) */
int32_t yos_sigemptyset(struct yos_exec_ctx *ctx, uint32_t set_off)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    memset(ctx->memory + set_off, 0, YOS_FBSD_SIGSET_BYTES);
    return 0;
}

int32_t yos_sigfillset(struct yos_exec_ctx *ctx, uint32_t set_off)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    memset(ctx->memory + set_off, 0xff, YOS_FBSD_SIGSET_BYTES);
    return 0;
}

/* FreeBSD sigaddset/sigdelset/sigismember:
 *   bit (signo-1) within __bits[(signo-1)/32], bit (signo-1)%32.
 * Signals are 1..128. Out-of-range -> EINVAL. */
static int yos_sigset_signo_ok(int32_t signo)
{
    return (signo >= 1 && signo <= 128);
}

int32_t yos_sigaddset(struct yos_exec_ctx *ctx, uint32_t set_off, int32_t signo)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    if (!yos_sigset_signo_ok(signo))     return yos_errno_neg(ctx, EINVAL);
    uint32_t b = (uint32_t)(signo - 1);
    uint32_t *bits = (uint32_t *)(ctx->memory + set_off);
    bits[b / 32] |= 1u << (b % 32);
    return 0;
}

int32_t yos_sigdelset(struct yos_exec_ctx *ctx, uint32_t set_off, int32_t signo)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    if (!yos_sigset_signo_ok(signo))     return yos_errno_neg(ctx, EINVAL);
    uint32_t b = (uint32_t)(signo - 1);
    uint32_t *bits = (uint32_t *)(ctx->memory + set_off);
    bits[b / 32] &= ~(1u << (b % 32));
    return 0;
}

int32_t yos_sigismember(struct yos_exec_ctx *ctx, uint32_t set_off, int32_t signo)
{
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);
    if (!yos_sigset_signo_ok(signo))     return yos_errno_neg(ctx, EINVAL);
    uint32_t b = (uint32_t)(signo - 1);
    const uint32_t *bits = (const uint32_t *)(ctx->memory + set_off);
    return (bits[b / 32] >> (b % 32)) & 1u;
}

/* ── FreeBSD ↔ host signal-number / sigset_t translation ────────────
 *
 * The guest's sigset_t is FreeBSD-shape (16 bytes, bit (signo-1) for
 * signo in 1..128). The host's sigset_t is Linux-shape (128 bytes,
 * sigemptyset/sigaddset opaque API). Many POSIX signals share the
 * same number on both kernels (HUP=1, INT=2, ...), but the BSD-vs-
 * Linux additions diverge:
 *
 *     name   FreeBSD  Linux
 *     BUS    10       7
 *     SYS    12       31
 *     URG    16       23
 *     STOP   17       19
 *     TSTP   18       20
 *     CONT   19       18
 *     CHLD   20       17
 *     IO     23       29   (Linux: SIGPOLL == SIGIO)
 *     USR1   30       10
 *     USR2   31       12
 *     EMT     7       –    (Linux has no equivalent)
 *     INFO   29       –    (Linux has no equivalent)
 *
 * The fbsd→host bit-rewrite below walks the FreeBSD bitmap and sets
 * the corresponding host bit through sigaddset(). The reverse path
 * does the same in the other direction. Signals without a host
 * equivalent are silently dropped — caller still gets EINVAL from
 * sigaddset/sigaction etc. if it tries to actually use them. */
int fbsd_to_host_signo(int fb)
{
    /* Indexed 1..32; out-of-range returns 0 (no host equivalent). */
    static const int tbl[33] = {
        [1]  = SIGHUP,    [2]  = SIGINT,    [3]  = SIGQUIT,  [4]  = SIGILL,
        [5]  = SIGTRAP,   [6]  = SIGABRT,   [7]  = 0 /*EMT*/, [8]  = SIGFPE,
        [9]  = SIGKILL,   [10] = SIGBUS,    [11] = SIGSEGV,  [12] = SIGSYS,
        [13] = SIGPIPE,   [14] = SIGALRM,   [15] = SIGTERM,  [16] = SIGURG,
        [17] = SIGSTOP,   [18] = SIGTSTP,   [19] = SIGCONT,  [20] = SIGCHLD,
        [21] = SIGTTIN,   [22] = SIGTTOU,   [23] = SIGIO,    [24] = SIGXCPU,
        [25] = SIGXFSZ,   [26] = SIGVTALRM, [27] = SIGPROF,  [28] = SIGWINCH,
        [29] = 0 /*INFO*/,[30] = SIGUSR1,   [31] = SIGUSR2,  [32] = 0 /*THR*/,
    };
    return (fb >= 1 && fb <= 32) ? tbl[fb] : 0;
}

int host_to_fbsd_signo(int h)
{
    for (int fb = 1; fb <= 32; fb++)
        if (fbsd_to_host_signo(fb) == h) return fb;
    return 0;
}

void fbsd_sigset_to_host(const uint8_t *fb, sigset_t *host)
{
    sigemptyset(host);
    for (int fbsig = 1; fbsig <= 32; fbsig++) {
        int b = fbsig - 1;
        if (fb[b >> 3] & (1u << (b & 7))) {
            int h = fbsd_to_host_signo(fbsig);
            if (h > 0) sigaddset(host, h);
        }
    }
}

void host_sigset_to_fbsd(const sigset_t *host, uint8_t *fb)
{
    memset(fb, 0, YOS_FBSD_SIGSET_BYTES);
    for (int h = 1; h < NSIG; h++) {
        if (sigismember(host, h) > 0) {
            int fbsig = host_to_fbsd_signo(h);
            if (fbsig > 0) {
                int b = fbsig - 1;
                fb[b >> 3] |= 1u << (b & 7);
            }
        }
    }
}

/* ── pthread_sigmask ─────────────────────────────────────────────────
 *
 * Layout: 16B FreeBSD sigset_t ↔ 128B host sigset_t via the bit-
 * rewrite above. `how` numbering diverges: FreeBSD SIG_BLOCK=1
 * UNBLOCK=2 SETMASK=3; Linux SIG_BLOCK=0 UNBLOCK=1 SETMASK=2.
 *
 * Return convention: POSIX pthread_sigmask returns 0 / errno, NOT
 * -1+errno. We mirror that — bridge return value IS the errno-or-
 * zero the guest's pthread_sigmask declaration expects. */
int32_t yos_pthread_sigmask(struct yos_exec_ctx *ctx, int32_t how,
                            uint32_t set_off, uint32_t oset_off)
{
    if (set_off  && !yos_sigset_bound(ctx, set_off))  return EFAULT;
    if (oset_off && !yos_sigset_bound(ctx, oset_off)) return EFAULT;

    /* yos models per-process signals (one thread per yos_proc), so the
     * per-thread vs per-process distinction collapses — pthread_sigmask
     * routes through the same per-ctx sig_mask sigprocmask uses. This
     * also matters for hosts whose pthread_sigmask is a no-op (Windows
     * has no per-thread signal mask): without updating ctx->sig_mask
     * the wasm guest's SIG_BLOCK request is silently dropped, and a
     * subsequent raise()+sigwait() flow delivers the signal anyway,
     * tripping SIG_DFL=TERMINATE and killing the wasm program before
     * sigwait can consume it. */
    int32_t rc = sigmask_apply(ctx, how, set_off, oset_off);
    if (rc < 0) {
        /* sigmask_apply returns -1+errno on EFAULT/EINVAL via the
         * yos_errno_neg convention. pthread_sigmask, however, returns
         * an errno-code (POSIX). Translate. */
        return (ctx->memory && ctx->errno_off)
             ? *(int *)(ctx->memory + ctx->errno_off)
             : EINVAL;
    }
    /* Best-effort host call — keeps the host's view of the mask
     * roughly in sync for paths that DO check it (POSIX hosts), and
     * is a harmless no-op on Windows. Errors aren't propagated: the
     * authoritative state already lives in ctx->sig_mask. */
    sigset_t host_set, host_old;
    sigset_t *set_p  = NULL;
    sigset_t *oset_p = oset_off ? &host_old : NULL;
    int hhow = 0;
    if (set_off) {
        fbsd_sigset_to_host(ctx->memory + set_off, &host_set);
        set_p = &host_set;
        switch (how) {
            case 1: hhow = SIG_BLOCK;   break;
            case 2: hhow = SIG_UNBLOCK; break;
            case 3: hhow = SIG_SETMASK; break;
            default: hhow = SIG_SETMASK; break;
        }
    }
    (void)pthread_sigmask(hhow, set_p, oset_p);
    return 0;
}

/* ── sigaltstack(const stack_t *ss, stack_t *oss) ────────────────────
 *
 * FreeBSD wasm32 stack_t is { void *ss_sp; size_t ss_size; int ss_flags; }
 * laid out 4+4+4=12 bytes (wasm pointers + i386 size_t are both 32-bit).
 *
 * yos can't honour a guest-supplied alt stack: synthesised signal
 * delivery (invoke_signal_handler) runs the wasm-side handler via
 * m3_CallV on the *host's* regular thread stack, not on any wasm-
 * linear-memory region. The guest's ss_sp value names a wasm offset
 * — handing that to host sigaltstack would point the host kernel at
 * a host-VA inside our linear-memory mmap, almost certainly faulting
 * the next time a signal landed.
 *
 * Behaviour: validate bounds; report SS_DISABLE in oss so callers
 * (libuv probes, runtime libs) see "no alt stack installed" and pick
 * the regular-stack code path. Input ss is bounds-checked then
 * ignored. Returns success — failing here would make libuv refuse to
 * start.
 */
#ifndef YOS_SS_DISABLE
#define YOS_SS_DISABLE 4   /* SS_DISABLE on both FreeBSD and Linux */
#endif
#define YOS_FBSD_STACK_T_BYTES 12

int32_t yos_sigaltstack(struct yos_exec_ctx *ctx,
                         uint32_t ss_off, uint32_t oss_off)
{
    if (ss_off) {
        if ((uint64_t)ss_off + (uint64_t)YOS_FBSD_STACK_T_BYTES
            > (uint64_t)ctx->memory_size)
            return yos_errno_neg(ctx, EFAULT);
        /* Honestly we'd want to remember these for the oss query on a
         * later call; nothing in tree consumes that yet. Drop the
         * values silently for now — invokers that read back through
         * oss only care whether the kernel accepted the call. */
    }
    if (oss_off) {
        if ((uint64_t)oss_off + (uint64_t)YOS_FBSD_STACK_T_BYTES
            > (uint64_t)ctx->memory_size)
            return yos_errno_neg(ctx, EFAULT);
        uint8_t *p = ctx->memory + oss_off;
        memset(p, 0, YOS_FBSD_STACK_T_BYTES);
        /* ss_flags at byte offset 8 (after the 4-byte ss_sp and the
         * 4-byte ss_size). */
        *(uint32_t *)(p + 8) = YOS_SS_DISABLE;
    }
    return 0;
}

/* ── sigwait(const sigset_t *set, int *sig) ─────────────────────────
 *
 * Blocks the calling thread until one of the signals in `set` is
 * delivered; writes the signal number (FreeBSD numbering) to `*sig`.
 * POSIX return: 0 on success, errno on error (NOT -1 + errno).
 *
 * Host call uses the converted host sigset; the host returns a host
 * signum that we map back to FreeBSD numbering before writing it out. */
int32_t yos_sigwait(struct yos_exec_ctx *ctx,
                    uint32_t set_off, uint32_t sig_out_off)
{
    if (!yos_sigset_bound(ctx, set_off))             return EFAULT;
    if (!sig_out_off ||
        (uint64_t)sig_out_off + 4ULL > (uint64_t)ctx->memory_size)
        return EFAULT;

    /* yos models signals in ctx->sig_pending (set by yos_kill/raise);
     * popping a bit from there is the authoritative wait. Host sigwait
     * doesn't see those — it watches the host kernel's per-thread
     * signal queue, which is empty for raise()s that went through the
     * yos surface. Block briefly on the per-proc signal cond until
     * something in `set` shows up.
     *
     * Convention conversion (see sigmask_apply for the full story):
     * the wasm sigset uses bit = signo-1 (FreeBSD libc layout), the
     * internal sig_pending uses bit = signo. Shift left 1 to align.
     *
     * On Windows there is no host sigwait at all (compat returns
     * ENOSYS); this loop is the only mechanism by which the wasm
     * guest's pthread_sigmask(BLOCK)+raise()+sigwait() sequence ever
     * completes without SIG_DFL=TERMINATE killing the proc. */
    uint32_t want_fbsd = read_fbsd_sigset_lo(ctx, set_off);
    if (!want_fbsd) return EINVAL;
    uint32_t want_lo = want_fbsd << 1;

    /* Spin-poll the pending mask. sleep is short so a signal arriving
     * via deliver_to_proc on another thread is picked up promptly. The
     * loop exits on a match or on SIGKILL (uncatchable; pump handles). */
    for (;;) {
        uint32_t pend = __atomic_load_n(&ctx->sig_pending, __ATOMIC_ACQUIRE);
        uint32_t hit = pend & want_lo;
        if (hit) {
            int s = __builtin_ctz(hit);   /* bit-position == signo */
            uint32_t bit = 1u << s;
            __atomic_and_fetch(&ctx->sig_pending, ~bit, __ATOMIC_ACQ_REL);
            *(int32_t *)(ctx->memory + sig_out_off) = s;
            return 0;
        }
        /* SIGKILL bypasses sigwait — fall through so the next pump
         * call sees it (the pump in turn pthread_exit's the proc). */
        if (pend & (1u << YOS_FBSD_SIGKILL)) {
            yos_signal_pump(ctx);
            return EINTR;
        }
        /* Brief sleep avoids burning a core in a tight loop while
         * waiting for another thread (or this thread on a later
         * bridge) to set a bit we care about. */
        struct timespec ts = { 0, 1000000 };  /* 1 ms */
        nanosleep(&ts, NULL);
    }
}

/* ── sigwaitinfo / sigtimedwait ─────────────────────────────────────
 *
 * Bodies live in sig-linux.c (native sigwaitinfo + sigtimedwait) and
 * sig-darwin.c (sigwait-based emulation; sigtimedwait ENOSYS — darwin
 * has neither). Both reach into this file for the bound check + sigset
 * layout converter + fbsd<->host signo mapping via sig-internal.h.
 */

/* ── sigpending(sigset_t *set) ─────────────────────────────────────────
 *
 * Returns the set of signals pending delivery to (or blocked + queued
 * for) the calling process. Real shells (zsh's job control) check this
 * to see whether SIGCHLD is queued — when sigsuspend is unreliable they
 * fall back to: sigprocmask(BLOCK,SIGCHLD); sigpending; if pending then
 * waitpid; else sigsuspend.
 *
 * Host call + sigset_t out-conversion to FreeBSD layout. Bounds-check
 * the wasm offset; on bound failure return -1+EFAULT POSIX-style. */
int32_t yos_sigpending(struct yos_exec_ctx *ctx, uint32_t set_off)
{
    if (!set_off) return yos_errno_neg(ctx, EFAULT);
    if (!yos_sigset_bound(ctx, set_off)) return yos_errno_neg(ctx, EFAULT);

    sigset_t host;
    if (sigpending(&host) != 0)
        return yos_errno_neg(ctx, errno);

    host_sigset_to_fbsd(&host, ctx->memory + set_off);
    return 0;
}
