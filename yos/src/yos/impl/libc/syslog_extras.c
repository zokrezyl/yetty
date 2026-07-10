/* impl/syslog_extras.c — bridge surface for syslog + libutil bits
 * that aren't in our extractor's header walk.
 *
 * The codegen walks `api_top_headers` in src/yos/codegen/meson.build.
 * Neither <syslog.h> nor FreeBSD's <libutil.h> is on that list (they
 * pulled in machine-specific decls that broke extraction under the
 * wasm32 target), so the auto-bridge surface is missing every
 * function from those two headers. telnetd in particular needs:
 *
 *   openlog   — set syslog identity (no-op on yos; we log to stderr
 *               directly via syslog() below).
 *   closelog  — same — no-op.
 *   syslog    — variadic; format + write to stderr with a "[syslog]"
 *               prefix so it's easy to spot but won't get lost. Test
 *               rig only — a real production setup would route this
 *               through host syslog.
 *   login_tty — acquire a controlling TTY on the given fd. Maps
 *               1:1 to host login_tty (in <util.h> on glibc, in
 *               <libutil.h> on FreeBSD/darwin), which does
 *               setsid + ioctl(TIOCSCTTY) + dup2-to-0/1/2 + close.
 *   realhostname_sa — FreeBSD libutil reverse-DNS-with-fallback.
 *               Stubbed: write "unknown" + return 0. Telnetd uses
 *               this only to fill the audit hostname slot, so a
 *               literal "unknown" doesn't break anything functional.
 *
 * Linked at startup by yos_libc_extras_link_imports below, called
 * from src/yos/main.c right after the codegen's import-bind pass.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     /* dup, close — darwin's <util.h> doesn't pull these in */
#include <sys/types.h>
#include <sys/socket.h>

/* login_tty lives in different headers per host. */
#if defined(__linux__)
#  include <pty.h>      /* glibc puts it here */
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <util.h>     /* libutil */
#endif

#include "wasm3.h"
#include "m3_api_defs.h"
#include "m3_env.h"

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/errno_helpers.h"

extern int yos_fd_get(struct yos_exec_ctx *, int);

/* ── env.openlog(const char *ident, int option, int facility) ─────
 * No-op: we don't maintain syslog state; syslog() below writes
 * straight to stderr. The wasm-side `ident` pointer is intentionally
 * ignored. */
static m3ApiRawFunction(m3_openlog)
{
    m3ApiGetArgMem(const char *, ident);
    m3ApiGetArg   (int32_t,      option);
    m3ApiGetArg   (int32_t,      facility);
    (void)ident; (void)option; (void)facility;
    (void)_ctx; (void)_mem;
    m3ApiSuccess();
}

static m3ApiRawFunction(m3_closelog)
{
    (void)_ctx; (void)_mem; (void)_sp;
    m3ApiSuccess();
}

/* ── env.syslog(int priority, const char *fmt, ...) ───────────────
 *
 * Variadic. Same shape as our printf bridge: the wasm caller
 * stages varargs in a va_list block reachable via the trailing
 * `va_ptr` arg the wasm32 ABI tacks on. We don't try to format the
 * varargs ourselves (would need to recreate format_one logic from
 * impl/printf.c here) — for telnetd's audit-trail use the priority
 * + format string + a "[syslog: …]" prefix is enough to see WHAT
 * the daemon is logging. If we ever need real %s/%d expansion we
 * can wire this through yos_vfprintf later. */
static m3ApiRawFunction(m3_syslog)
{
    m3ApiGetArg   (int32_t,      priority);
    m3ApiGetArgMem(const char *, fmt);
    /* Variadic trail: clang's wasm32 ABI passes a va_list_ptr as
     * the last argument. We don't decode it. */
    (void)priority;
    fprintf(stderr, "[syslog prio=%d] %s\n",
            (int)priority, fmt ? fmt : "(null)");
    (void)_ctx;
    m3ApiSuccess();
}

/* ── env.login_tty(int fd) ────────────────────────────────────────
 *
 * Hand-roll the four-step BSD recipe. Calling host login_tty is
 * NOT safe under yos's pthread-per-process model:
 *   - setsid() is per-HOST-process, not per-host-thread; the
 *     first guest "process" already put the host into some
 *     session, and a second thread's setsid() either fails or
 *     yanks the controlling tty out from under unrelated guest
 *     threads.
 *   - ioctl(fd, TIOCSCTTY, 0) needs the calling process to be
 *     session leader without a controlling tty; the host process
 *     usually already has one (the terminal we were launched
 *     from), so this returns EPERM and telnetd's getptyslave
 *     prints "telnetd: : Operation not permitted." and exits.
 *
 * The pieces that DO need to happen for telnetd to read/write
 * its PTY are the dup2-to-0/1/2 + close, since those redirect
 * the wasm guest's stdio table (the per-ctx fd_map). The
 * controlling-tty bit is meaningful only at the kernel level
 * for job control / signal delivery; the wasm guest doesn't
 * observe it directly. So: skip setsid + TIOCSCTTY, do the
 * fd swap, return 0. */
static m3ApiRawFunction(m3_login_tty)
{
    m3ApiReturnType (int32_t);
    m3ApiGetArg     (int32_t, wfd);

    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) {
        m3ApiReturn(yos_errno_neg(ctx, EBADF));
    }
    /* Repoint guest fds 0/1/2 at the slave PTY's host fd. Each
     * slot in ctx->fd_map[] is INDEPENDENT host fd ownership;
     * dup2 host-side and assign. */
    extern int yos_fd_alloc(struct yos_exec_ctx *, int);
    extern void yos_fd_close(struct yos_exec_ctx *, int);
    for (int slot = 0; slot < 3; slot++) {
        int dup_fd = dup(hfd);
        if (dup_fd < 0) {
            ydebug("login_tty(wfd=%d hfd=%d) dup failed slot=%d errno=%d\n",
                   wfd, hfd, slot, errno);
            m3ApiReturn(yos_errno_neg(ctx, errno));
        }
        /* Close the old host fd held by ctx->fd_map[slot], if any,
         * then install the new dup. yos_fd_close handles the
         * host-side close + slot clear. */
        if (ctx->fd_map[slot] >= 0) {
            close(ctx->fd_map[slot]);
        }
        ctx->fd_map[slot] = dup_fd;
    }
    /* Release the original PTY slave slot — guest no longer needs
     * the high-numbered fd once 0/1/2 point at the same kernel
     * object via dup. */
    if (wfd > 2) {
        close(hfd);
        ctx->fd_map[wfd] = -1;
    }
    ydebug("login_tty(wfd=%d hfd=%d) -> dup2'd to fd_map[0..2]\n",
           wfd, hfd);
    m3ApiReturn(0);
    m3ApiSuccess();
}

/* ── env.forkpty(int *amaster, char *name, termios *termp, winsize *winp)
 *
 * openpty + fork + login_tty composed over the asyncify fork, exactly
 * like the browser engine's bridge (yos_proc.mjs). nvim's :terminal is
 * the consumer: it forkpty()s and execs $SHELL in the child.
 *
 * First entry (asyncify NORMAL): open a REAL host pty pair, apply the
 * caller's winsize, publish both ends in the guest fd table, write
 * *amaster, stash the guest fd numbers on the ctx, then unwind through
 * yos_fork. BOTH sides re-execute this bridge while REWINDING (the
 * wasm replays down to the call); yos_fork returns which side we are:
 * the child runs the login_tty recipe on the slave (dup onto fds
 * 0/1/2, drop the high slots AND the master), the parent drops the
 * slave and keeps only the master. The stash reaches the child ctx via
 * the fork pump (types.h forkpty_* fields); the host fds themselves
 * travel through the ordinary parent_fd_map dup path.
 *
 * termp is intentionally ignored: the host kernel hands a fresh pty
 * sane cooked defaults, which is what nvim's :terminal asks for. */
static m3ApiRawFunction(m3_forkpty)
{
    m3ApiReturnType (int32_t);
    m3ApiGetArg     (uint32_t, amaster_w);
    m3ApiGetArg     (uint32_t, name_w);
    m3ApiGetArg     (uint32_t, termp_w);
    m3ApiGetArg     (uint32_t, winp_w);
    (void)name_w; (void)termp_w;
    /* Guest NULL must stay NULL — a raw m3ApiGetArgMem would alias the
     * linear-memory base for offset 0. */
    int32_t  *amaster = amaster_w ? (int32_t *)m3ApiOffsetToPtr(amaster_w) : NULL;
    uint16_t *winp    = winp_w    ? (uint16_t *)m3ApiOffsetToPtr(winp_w)   : NULL;

    struct yos_exec_ctx *ctx =
        (struct yos_exec_ctx *)m3_GetUserData(runtime);
#if defined(_WIN32)
    (void)amaster; (void)winp; (void)ctx;
    m3ApiReturn(-ENOSYS);
#else
    extern int32_t yos_fork(struct yos_exec_ctx *);
    extern int yos_fork_rewinding(struct yos_exec_ctx *);
    extern int yos_fd_alloc(struct yos_exec_ctx *, int);

    const int rewinding = yos_fork_rewinding(ctx);
    if (!rewinding) {
        int master_hfd = -1, slave_hfd = -1;
        struct winsize wsz = { 0 };
        struct winsize *wszp = NULL;
        if (winp) {
            wsz.ws_row = winp[0];
            wsz.ws_col = winp[1];
            wszp = &wsz;
        }
        if (openpty(&master_hfd, &slave_hfd, NULL, NULL, wszp) < 0) {
            ydebug("forkpty: openpty failed errno=%d\n", errno);
            m3ApiReturn(yos_errno_neg(ctx, errno));
        }
        ctx->forkpty_master_wfd = yos_fd_alloc(ctx, master_hfd);
        ctx->forkpty_slave_wfd  = yos_fd_alloc(ctx, slave_hfd);
        if (ctx->forkpty_master_wfd < 0 || ctx->forkpty_slave_wfd < 0) {
            close(master_hfd);
            close(slave_hfd);
            m3ApiReturn(yos_errno_neg(ctx, EMFILE));
        }
        if (amaster)
            *amaster = ctx->forkpty_master_wfd;
        ctx->forkpty_pending = 1;
        ydebug("forkpty: pty master wfd=%d slave wfd=%d, unwinding\n",
               ctx->forkpty_master_wfd, ctx->forkpty_slave_wfd);
    }

    int32_t side = yos_fork(ctx);
    if (!rewinding) {
        /* Unwinding (value ignored) — or the fork itself failed. */
        if (side < 0) {
            int mh = yos_fd_get(ctx, ctx->forkpty_master_wfd);
            int sh = yos_fd_get(ctx, ctx->forkpty_slave_wfd);
            if (mh >= 0) close(mh);
            if (sh >= 0) close(sh);
            ctx->fd_map[ctx->forkpty_master_wfd] = -1;
            ctx->fd_map[ctx->forkpty_slave_wfd]  = -1;
            ctx->forkpty_pending = 0;
        }
        m3ApiReturn(side);
    }

    if (ctx->forkpty_pending) {
        ctx->forkpty_pending = 0;
        const int32_t master_wfd = ctx->forkpty_master_wfd;
        const int32_t slave_wfd  = ctx->forkpty_slave_wfd;
        if (side == 0) {
            /* child: slave becomes stdio (login_tty recipe), master and
             * the high slave slot are dropped. */
            int slave_hfd = yos_fd_get(ctx, slave_wfd);
            for (int slot = 0; slot < 3 && slave_hfd >= 0; slot++) {
                int dup_fd = dup(slave_hfd);
                if (dup_fd < 0) break;
                if (ctx->fd_map[slot] >= 0)
                    close(ctx->fd_map[slot]);
                ctx->fd_map[slot] = dup_fd;
            }
            if (slave_hfd >= 0) {
                close(slave_hfd);
                ctx->fd_map[slave_wfd] = -1;
            }
            int master_hfd = yos_fd_get(ctx, master_wfd);
            if (master_hfd >= 0) {
                close(master_hfd);
                ctx->fd_map[master_wfd] = -1;
            }
            ctx->is_forkpty_child = 1;
            ydebug("forkpty child: slave on fds 0/1/2\n");
        } else if (side > 0) {
            /* parent: keep only the master. */
            int slave_hfd = yos_fd_get(ctx, slave_wfd);
            if (slave_hfd >= 0) {
                close(slave_hfd);
                ctx->fd_map[slave_wfd] = -1;
            }
            ydebug("forkpty parent: child pid=%d, master wfd=%d\n",
                   side, master_wfd);
        }
    }
    m3ApiReturn(side);
#endif
    m3ApiSuccess();
}

/* ── env.realhostname_sa(char *host, size_t hsize,
 *                        const struct sockaddr *sa, int salen)
 *
 * FreeBSD-libutil reverse-DNS with a numeric fallback. Telnetd uses
 * it to fill the connecting peer's hostname for audit / utmp slots.
 * For the test-rig we don't care about DNS — write "unknown" into
 * the guest buffer and return 0. */
static m3ApiRawFunction(m3_realhostname_sa)
{
    m3ApiReturnType (int32_t);
    m3ApiGetArgMem  (char *,                       host);
    m3ApiGetArg     (uint32_t,                     hsize);
    m3ApiGetArgMem  (const struct sockaddr *,      sa);
    m3ApiGetArg     (int32_t,                      salen);
    (void)sa; (void)salen; (void)_ctx;
    if (host && hsize >= 8) {
        memcpy(host, "unknown", 8);  /* incl. NUL */
    } else if (host && hsize > 0) {
        host[0] = '\0';
    }
    m3ApiReturn(0);
    m3ApiSuccess();
}

/* Called from src/yos/main.c after the codegen binds the auto-
 * generated env imports. These five aren't in the codegen surface
 * because <syslog.h> / <libutil.h> aren't in the extractor's
 * api_top_headers — adding them there would drag in machine-
 * specific decls that break the wasm32 extraction. Hand-binding
 * here is the targeted fix. */
void yos_syslog_extras_link_imports(IM3Module mod)
{
    M3Result r;
    r = m3_LinkRawFunction(mod, "env", "openlog",  "v(iii)",  m3_openlog);
    if (r && r != m3Err_functionLookupFailed)
        fprintf(stderr, "yos: link openlog: %s\n", r);
    r = m3_LinkRawFunction(mod, "env", "closelog", "v()",     m3_closelog);
    if (r && r != m3Err_functionLookupFailed)
        fprintf(stderr, "yos: link closelog: %s\n", r);
    r = m3_LinkRawFunction(mod, "env", "syslog",   "v(iii)",  m3_syslog);
    if (r && r != m3Err_functionLookupFailed)
        fprintf(stderr, "yos: link syslog: %s\n", r);
    r = m3_LinkRawFunction(mod, "env", "login_tty", "i(i)",   m3_login_tty);
    if (r && r != m3Err_functionLookupFailed)
        fprintf(stderr, "yos: link login_tty: %s\n", r);
    r = m3_LinkRawFunction(mod, "env", "forkpty",  "i(iiii)", m3_forkpty);
    if (r && r != m3Err_functionLookupFailed)
        fprintf(stderr, "yos: link forkpty: %s\n", r);
    r = m3_LinkRawFunction(mod, "env", "realhostname_sa", "i(iiii)",
                           m3_realhostname_sa);
    if (r && r != m3Err_functionLookupFailed)
        fprintf(stderr, "yos: link realhostname_sa: %s\n", r);
}
