#define _GNU_SOURCE
#define _DARWIN_C_SOURCE  /* darwin libc gates mknodat/etc. behind this */
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "platform.h"
#include "impl/errno_helpers.h"
#include "impl/io/io-internal.h"
#include "vfs/mount.h"
#include "vfs/file.h"
#include "vfs/procfs.h"
#include "wasm32_structs.h"
#include "host64_structs.h"
#include "struct_convert.h"
#include <stdint.h>
#include <stdlib.h>             /* getenv */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <pthread.h>     /* pthread_self() — used by the ioctl tid trace */
#include <sys/ioctl.h>
#include <dirent.h>
#include <stdio.h>

/* wptr / wstr / host_fd are inlined from impl/io/io-internal.h so the
 * platform slice files (io-linux.c, io-darwin.c) get the same defs
 * without duplication. */

/* Resolve a guest path against ctx->cwd into an absolute host path.
 *
 * yos's fork model is "pthread of the same host process", so the host
 * cwd is SHARED across every yos guest. That means `chdir()` in one
 * guest (e.g. runsv child) silently changes another guest's (e.g.
 * runsvdir parent) cwd, and subsequent relative-path ops resolve
 * against the wrong base. The whole runit + runsvdir pattern (and any
 * super-server that does fork-then-chdir-then-exec relative) used to
 * race fatally on this.
 *
 * Fix: yos_chdir below NEVER calls host chdir — it only updates
 * ctx->cwd. Every path-taking bridge (yos_open, yos_opendir,
 * yos_execve, stat/lstat/access/mkdir/unlink/…) routes its path
 * argument through this helper to get the host-absolute path.
 *
 * Returns: pointer to a thread-local buffer holding the resolved
 * path. Valid until the next yos_path_resolve() call on the same
 * thread (caller MUST copy if it needs to outlive that). If the
 * input is already absolute, returns the input pointer unchanged
 * (no copy needed — same lifetime as the caller's data).
 *
 * The TLS buffer is per-host-pthread which (under yos's pthread-per-
 * forked-process model) is per-guest-process, so concurrent guests
 * don't trample each other. */
const char *yos_path_resolve(struct yos_exec_ctx *ctx, const char *p)
{
    if (!p) return NULL;
    /* POSIX: the EMPTY path is ENOENT, never the cwd. Joining "" onto
     * ctx->cwd produced "<cwd>/", so stat("")/access("") reported the
     * current directory — netrw's `isdirectory(expand("<amatch>"))` on
     * nvim's unnamed startup buffer then browsed the cwd instead of
     * leaving the buffer alone (same bug fixed in the browser engine's
     * stat/open/access). Pass "" through untouched: every host syscall
     * already returns ENOENT for it. */
    if (p[0] == 0) return p;
    /* Treat as already-absolute. POSIX hosts accept only "/foo";
     * Windows additionally accepts drive-letter and UNC forms. On
     * POSIX the relaxed pattern would match legitimate relative
     * filenames like "c:foo" and skip the cwd join. */
    int is_abs = (p[0] == '/');
#ifdef _WIN32
    is_abs = is_abs
          || (p[0] == '\\' && p[1] == '\\')
          || (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))
              && p[1] == ':');
#endif
    const char *abs = p;
    if (is_abs) {
        /* absolute — passthrough */
    } else if (!ctx || ctx->cwd[0] == 0) {
        /* no cwd yet — let host decide */
    } else {
        static _Thread_local char buf[PATH_MAX];
        size_t cwd_n = strlen(ctx->cwd);
        if (cwd_n == 0 || cwd_n >= sizeof buf) return p;
        memcpy(buf, ctx->cwd, cwd_n);
        size_t pos = cwd_n;
        if (buf[pos - 1] != '/' && buf[pos - 1] != '\\' && pos + 1 < sizeof buf) buf[pos++] = '/';
        size_t pn = strlen(p);
        if (pos + pn + 1 > sizeof buf) return p;   /* too long */
        memcpy(buf + pos, p, pn + 1);
        abs = buf;
    }
    /* Map POSIX-shape devices/temps to host equivalents. POSIX hosts
     * pass through unchanged; Windows substitutes /dev/null → NUL,
     * /tmp/<x> → %TEMP%\<x>, etc. */
    return yos_plat_translate_path(abs);
}

/* For *at()-family bridges: choose the right path-resolution policy
 * for a given (host_dfd, path) pair.
 *
 *   absolute path    → passthrough (kernel won't consult any dirfd)
 *   host_dfd == AT_FDCWD → resolve against ctx->cwd (the kernel's
 *                      AT_FDCWD is the *host process* cwd, not ours)
 *   real translated dfd  → leave the path alone; host_dfd points at
 *                      the same directory the guest's wfd does, so
 *                      the host kernel resolves the relative path
 *                      against that directory correctly.
 *
 * Like yos_path_resolve, the cwd-relative branch returns a TLS buffer
 * — callers that pair two resolve calls (renameat, linkat) must
 * snapshot the first result to the stack before the second call. */
const char *yos_path_resolve_at(struct yos_exec_ctx *ctx, int host_dfd, const char *p)
{
    if (!p) return NULL;
    if (host_dfd == AT_FDCWD) return yos_path_resolve(ctx, p);
    return p;
}

/* ============================================================================
 * Per-runtime fd table.
 * ============================================================================ */

void yos_fd_table_init(struct yos_exec_ctx *ctx)
{
    /* Idempotent. main.c::load_wasm_module runs on BOTH initial
     * process load AND every execve (after m3_FreeRuntime tears
     * down the old wasm module). Re-running the dup loop on execve
     * wipes the parent shell's fd setup: zsh's dup2(some_pipe, 2)
     * gets replaced by a fresh F_DUPFD of the host's literal stderr,
     * the new host fd lands at some kernel-picked slot (typically 8),
     * and the subsequent CLOEXEC walk in execve closes it. The
     * child's first write to stderr then EBADFs.
     *
     * Use ctx->fd_table_inited as the "already done" flag — checking
     * fd_map[0] alone won't work because ctx is calloc'd to zeros and
     * a fresh ctx has fd_map[0] == 0, not -1. */
    if (ctx->fd_table_inited) return;
    ctx->fd_table_inited = 1;
    for (int i = 0; i < YOS_FD_MAX; i++) ctx->fd_map[i] = -1;
    /* Inherit yos's stdio at the conventional positions — but as DUPS
     * of the host's 0/1/2, not the originals.
     *
     * Why: if the guest's wasm code does `close(1)` (zsh interactive
     * does this routinely when restructuring fds around fork+exec),
     * yos_fd_close calls `yos_plat_close(host_fd)`. When host_fd is the host's
     * literal stdout fd 1, that wipes out yos's own stdout for the
     * rest of its lifetime. The kernel can then reuse fd 1 for the
     * next allocation (a pipe from yos_fork_pump's F_DUPFD loop, for
     * instance), so a later write to wfd 1 → host fd 1 lands in the
     * pipe instead of on the user's terminal. The "command not found"
     * message vanishes; subsequent writes hit EBADF when the pipe
     * closes; the shell prompt comes back broken.
     *
     * Duplicating up-front guarantees the host's original 0/1/2 are
     * never reachable through any wasm fd, so no guest action can
     * close them. fcntl(F_DUPFD, 3) picks the smallest free fd >= 3,
     * so we never collide with the originals. Fall back to the raw
     * fds if the dup fails (e.g. fd already closed at exec time —
     * yos as a daemon with stdio redirected to /dev/null), preserving
     * the prior behaviour for that edge case. */
    for (int i = 0; i < 3; i++) {
        int dup_fd = fcntl(i, F_DUPFD, 3);
        ctx->fd_map[i] = (dup_fd >= 0) ? dup_fd : i;
    }
}

/* FreeBSD's AT_FDCWD = -100 (also Linux). Darwin's AT_FDCWD = -2. The
 * wasm guest is FreeBSD-shaped so it always passes -100; translate to
 * the host value here. We accept either spelling on input so callers
 * that already worked under the host value keep working. */
#define YOS_FBSD_AT_FDCWD (-100)

int32_t yos_fd_get(struct yos_exec_ctx *ctx, int32_t wfd)
{
    if (wfd == AT_FDCWD || wfd == YOS_FBSD_AT_FDCWD) return AT_FDCWD;
    if (wfd < 0 || wfd >= YOS_FD_MAX) return yos_errno_neg(ctx, EBADF);
    int hfd = ctx->fd_map[wfd];
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    return hfd;
}

/* Translate a wasm dirfd to the host's representation. Used by every
 * *at() bridge — see codegen/bridge.py's _AT_FAMILY list. Returns
 * the host AT_FDCWD when the guest passed AT_FDCWD (either platform's
 * value), the mapped host fd for a regular wasm fd, or -1 to make the
 * host call fail with EBADF for invalid input. */
int yos_xlate_dfd(struct yos_exec_ctx *ctx, int32_t wfd)
{
    /* yos_fd_get returns:
     *   - host AT_FDCWD when wfd is AT_FDCWD-like (darwin = -2,
     *     Linux/FreeBSD = -100). Pass through; do NOT filter as
     *     "negative ⇒ error" — AT_FDCWD itself is negative.
     *   - positive host fd for valid wasm fds.
     *   - -EBADF (-9) for invalid wasm fds, which the host call
     *     will reject as a bad fd. */
    return yos_fd_get(ctx, wfd);
}

/* Ownership contract: yos_fd_alloc takes ownership of `host_fd`.
 *   - On success the host fd is recorded in fd_map; the caller now
 *     refers to it via the returned wfd. Release with yos_fd_close,
 *     which closes the underlying host fd.
 *   - On EMFILE (table full) yos_fd_alloc CLOSES host_fd internally
 *     and returns -EMFILE. Callers MUST NOT close the host fd
 *     themselves on this path — that would double-close (or close
 *     an unrelated descriptor the kernel has already recycled).
 *   - If host_fd is negative on entry (a propagated error from an
 *     upstream open/socket/dup), the same negative is returned
 *     unchanged; no fd is touched.
 *
 * Pipe2 and similar two-fd routines depend on this contract: they
 * close ONLY the partner end on the first-alloc failure path. See
 * impl/io/io-darwin.c::yos_pipe2 / io-linux.c::yos_pipe2. */
/* Slot sentinel: "this slot was released by fclose() of an fdopen'd
 * stream — refuse close() (return EBADF) and don't reuse for open()".
 * Without this, the test_issue15_fdopen_lifetime scenario fails:
 * fdopen(fd_a)+fclose() releases fd_a's slot to -1, the next open()
 * gets fd_a back from yos_fd_alloc, and the caller's defensive
 * close(fd_a) (which it expects to EBADF on a stale fd) instead
 * closes the brand-new open's host fd. */
#define YOS_FD_FCLOSE_TOMB (-2)

int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd)
{
    if (host_fd < 0) return host_fd;
    for (int i = 0; i < YOS_FD_MAX; i++) {
        /* Strict -1 match: skip TOMB slots so post-fclose defensive
         * close(stale_fd) reads EBADF rather than clobbering a
         * recycled fd. */
        if (ctx->fd_map[i] == -1) {
            ctx->fd_map[i] = host_fd;
            free(ctx->fd_paths[i]);
            ctx->fd_paths[i] = NULL;
            return i;
        }
    }
    yos_plat_close(host_fd);
    return -EMFILE;
}

/* Lexically canonicalize an absolute path in place: collapse `//` →
 * `/`, drop `./` segments, and resolve `../` against the preceding
 * segment. Pure string transformation — does NOT touch the filesystem,
 * does NOT resolve symlinks. We need this because fts(3) walks via
 * fchdir(open("..")) and similar dance: openat with ".." records as
 * "<base>/.." which, if appended literally, balloons the cwd into a
 * megabyte of "../foo/../bar/../baz" stew and quickly overflows
 * PATH_MAX, corrupting the stored path so later lookups against it
 * fail with ENOENT. Canonicalizing on each store keeps fd_paths
 * sized to the real depth of the tree, not to fts's traversal log.
 *
 * NOT a substitute for realpath(3): preserves symlinks (a/symlink/..
 * canonicalizes to "a", not to wherever symlink pointed). For yos's
 * use (tracking the path the guest believes it opened) lexical is
 * correct — the guest opened "a/symlink/.." and that's what it
 * thinks its cwd is. */
static void canon_abs_path(char *p)
{
    if (!p || p[0] != '/') return;
    /* Write to a separate buffer to keep the read pointer (in) strictly
     * ahead of the write pointer. An in-place version trampled the
     * input's '\0' on the first segment-with-trailing-'/' emission,
     * so the outer `while (*in)` then walked into uninitialised stack,
     * which appended random byte sequences (debug-output residue) to
     * the canonicalised path — exactly the "/path/A/anager_Mac.app/..."
     * doubling we observed under fts(3). */
    char tmp[PATH_MAX];
    char *out = tmp;
    *out++ = '/';
    const char *in = p + 1;
    while (*in) {
        while (*in == '/') in++;
        if (!*in) break;
        const char *seg_end = in;
        while (*seg_end && *seg_end != '/') seg_end++;
        size_t seg_len = seg_end - in;

        if (seg_len == 1 && in[0] == '.') {
            /* "." — drop. */
        } else if (seg_len == 2 && in[0] == '.' && in[1] == '.') {
            if (out > tmp + 1) {
                out--;
                while (out > tmp + 1 && out[-1] != '/') out--;
            }
        } else {
            if (out + seg_len + 1 >= tmp + sizeof tmp) break;
            memcpy(out, in, seg_len);
            out += seg_len;
            *out++ = '/';
        }
        in = seg_end;
    }
    if (out > tmp + 1 && out[-1] == '/') out--;
    *out = '\0';
    size_t n = out - tmp;
    memcpy(p, tmp, n + 1);
}

/* Same as yos_fd_alloc but also records the absolute path of the file
 * the fd was opened on. Called by path-taking opens so yos_fchdir can
 * read the path back without consulting the host kernel. `path` may
 * be NULL or relative — we resolve relative→absolute via the current
 * ctx->cwd, then lexically canonicalize. */
int32_t yos_fd_alloc_with_path(struct yos_exec_ctx *ctx, int host_fd,
                               const char *path)
{
    int32_t wfd = yos_fd_alloc(ctx, host_fd);
    if (wfd < 0 || !path) return wfd;
    char abs[PATH_MAX];
    if (path[0] == '/') {
        snprintf(abs, sizeof abs, "%s", path);
    } else if (ctx->cwd[0]) {
        size_t cwd_n = strlen(ctx->cwd);
        int need_slash = (cwd_n > 0 && ctx->cwd[cwd_n - 1] != '/');
        snprintf(abs, sizeof abs, "%s%s%s", ctx->cwd,
                 need_slash ? "/" : "", path);
    } else {
        snprintf(abs, sizeof abs, "%s", path);
    }
    canon_abs_path(abs);
    ctx->fd_paths[wfd] = strdup(abs);
    return wfd;
}

int32_t yos_fd_assign(struct yos_exec_ctx *ctx, int32_t newfd, int host_fd)
{
    if (host_fd < 0) return host_fd;
    if (newfd < 0 || newfd >= YOS_FD_MAX) {
        yos_plat_close(host_fd);
        return yos_errno_neg(ctx, EBADF);
    }
    int old = ctx->fd_map[newfd];
    if (old >= 0 && old != host_fd) {
        ydebug("fd_assign: wfd %d evict host_fd=%d (replaced by %d)\n",
               newfd, old, host_fd);
        yos_plat_close(old);
    }
    ctx->fd_map[newfd] = host_fd;
    free(ctx->fd_paths[newfd]);
    ctx->fd_paths[newfd] = NULL;
    return newfd;
}

int32_t yos_fd_close(struct yos_exec_ctx *ctx, int32_t wfd)
{
    if (wfd < 0 || wfd >= YOS_FD_MAX) return yos_errno_neg(ctx, EBADF);
    int hfd = ctx->fd_map[wfd];
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int r = yos_plat_close(hfd);
    ctx->fd_map[wfd] = -1;
    free(ctx->fd_paths[wfd]);
    ctx->fd_paths[wfd] = NULL;
    return yos_errno_check(ctx, (int32_t)r);
}

/* Release a wasm-fd slot WITHOUT calling yos_plat_close(host_fd). Used by
 * impl/io/file.c::free_handle after fclose(host_FILE) has already
 * closed the underlying host fd — calling close on a stale fd would
 * EBADF (or worse, close someone else's freshly-opened fd that got
 * the recycled number). The slot is tombstoned (YOS_FD_FCLOSE_TOMB)
 * so yos_fd_alloc won't recycle it and yos_fd_close on the same wfd
 * cleanly EBADFs. yos_fd_assign (dup2) clears the tombstone — the
 * caller is explicitly claiming the slot. */
void yos_fd_release_slot(struct yos_exec_ctx *ctx, int32_t wfd)
{
    if (!ctx || wfd < 0 || wfd >= YOS_FD_MAX) return;
    ctx->fd_map[wfd] = YOS_FD_FCLOSE_TOMB;
    free(ctx->fd_paths[wfd]);
    ctx->fd_paths[wfd] = NULL;
}

void yos_fd_fork_dup(struct yos_exec_ctx *child, struct yos_exec_ctx *parent)
{
    for (int i = 0; i < YOS_FD_MAX; i++) {
        int phfd = parent->fd_map[i];
        if (phfd < 0) {
            child->fd_map[i] = -1;
            child->fd_paths[i] = NULL;
            continue;
        }
        /* POSIX fork preserves FD_CLOEXEC; F_DUPFD strips it. Use
         * F_DUPFD_CLOEXEC when the source has it set so subsequent
         * exec()s correctly close the descriptor — without this,
         * libuv's spawn signal pipe (CLOEXEC) survives our pseudo-
         * exec, the post-execvp failure path in the child writes
         * errno into it, and the parent treats the spawn as failed. */
    int flags = fcntl(phfd, F_GETFD);
    int dupcmd = (flags >= 0 && (flags & FD_CLOEXEC)) ? F_DUPFD_CLOEXEC
                                                          : F_DUPFD;
        int chfd = fcntl(phfd, dupcmd, 0);
        child->fd_map[i] = (chfd >= 0) ? chfd : -1;
        child->fd_paths[i] = parent->fd_paths[i]
                             ? strdup(parent->fd_paths[i]) : NULL;
    }
}

/* Back-compat shim: code paths still calling yos_fd_translate / host_fd
 * route through the new table. */
int32_t yos_fd_translate(struct yos_exec_ctx *ctx, int32_t fd)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    return hfd < 0 ? fd : hfd;
}

int32_t yos_read(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count)
{
    /* Validate the FULL [buf, buf+count) range — the host kernel writes
     * up to `count` bytes into the buffer, and `wptr` alone only checks
     * the first byte. A buffer near memory_size with a huge count would
     * otherwise let read() stomp past wasm memory. */
    void *p = wptr_range(ctx, buf, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);

    /* Check if virtual fd */
    if (yos_is_virtual_fd(fd)) {
        struct yos_file_table *ft = (struct yos_file_table *)ctx->procfs_fds;
        if (!ft) return yos_errno_neg(ctx, EBADF);
        struct yos_file *file = yos_file_get(ft, fd);
        if (!file) return yos_errno_neg(ctx, EBADF);
        if (file->ops && file->ops->read)
            return file->ops->read(ctx, file, p, count);
        return yos_errno_neg(ctx, EBADF);
    }

    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    /* Pump host-side pending signals (Ctrl-C → SIGINT, terminal
     * resize → SIGWINCH, …) before we sleep in read(). For zsh at
     * the prompt, that calls its recorded wasm SIGINT handler
     * which line-discards. For nvim, SIGWINCH triggers a redraw. */
    extern void yos_signal_pump(struct yos_exec_ctx *);
    yos_signal_pump(ctx);
    ssize_t r;
    /* Fake-PTY master in TIOCPKT mode: prepend a 0x00 status byte so
     * telnetd doesn't misinterpret the first data byte as packet
     * flags. -2 means "not a packet-mode master", fall through. */
    {
        extern ssize_t yos_pty_packet_read(int hfd, void *buf, size_t cap);
        ssize_t pr = yos_pty_packet_read(hfd, p, count);
        if (pr != -2) {
            r = pr;
            goto read_done;
        }
    }
    for (;;) {
        r = yos_plat_read(hfd, p, count);
        if (r >= 0 || errno != EINTR) break;
        /* Signal arrived while we were blocked. Drain the pending
         * bitmask (delivers to the wasm handler) and retry the
         * read — zsh has had its chance to react. If the guest
         * actually wants EINTR to propagate it'll see it via the
         * second-stage return from its wasm handler. */
        yos_signal_pump(ctx);
    }
    /* PTY master EOF normalisation. On Linux, a PTY master read after
     * the slave is fully closed returns -1 / EIO; on FreeBSD/macOS it
     * returns 0 (EOF). yos's wasm guests are FreeBSD-shaped — telnetd's
     * main loop interprets EIO as a transient error and retries in a
     * tight loop, burning a CPU and never releasing the connection.
     * Translate EIO on a PTY master fd to EOF so the loop terminates.
     * isatty() is the cheap discriminator: the master IS a tty fd
     * (`/dev/ptmx`), regular files / sockets aren't, so we don't risk
     * swallowing a legitimate EIO from a disk read. */
    if (r < 0 && errno == EIO && hfd >= 0 && isatty(hfd) == 1) {
        ydebug("read(wfd=%d hfd=%d): PTY-master EIO → 0 (EOF) "
               "for FreeBSD-shape compatibility\n", fd, hfd);
        r = 0;
    }
read_done:;
    if (ytrace_default_enabled()) {
        pid_t tid = yos_plat_gettid();
        char hex[3 * 16 + 1] = {0};
        if (r > 0) {
            const uint8_t *bp = (const uint8_t *)p;
            int n = r > 16 ? 16 : (int)r;
            for (int i = 0; i < n; i++)
                snprintf(hex + i * 3, 4, "%02x ", bp[i]);
        }
        ydebug("read(tid=%d wfd=%d hfd=%d count=%u) = %zd%s%s\n",
               (int)tid, fd, hfd, count, r,
               r > 0 ? " hex=" : "",
               r > 0 ? hex : "");
    }
    /* YOS_DUMP_SOCK=<fd>: dump every byte read/written on that wasm fd to
     * /tmp/yos-sock-{r,w}.bin. Diagnostic for MAC failures — lets us see
     * if the bytes ssh's crypto consumes match what the server sent. */
    if (r > 0) {
        const char *dump = getenv("YOS_DUMP_SOCK");
        if (dump && (int)strtol(dump, NULL, 10) == fd) {
            FILE *f = fopen("/tmp/yos-sock-r.bin", "ab");
            if (f) { fwrite(p, 1, (size_t)r, f); fclose(f); }
        }
    }
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_write(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count)
{
    /* Validate the FULL [buf, buf+count) range so the host kernel can't
     * read past wasm memory when count is guest-controlled. */
    void *p = wptr_range(ctx, buf, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (fd == 2 && count > 0) ctx->stderr_written_since_exec = 1;
    /* DEBUG: dump nvim's vim._init_packages module bytes (linear memory
     * 0x6aa70) on every write so we see corruption timeline. */
    if (getenv("YOS_DUMP_INIT")) {
        const uint8_t *m = ctx->memory + 0x6aa70;
        fprintf(stderr, "yos: dump@0x6aa70: %02x %02x %02x %02x %02x %02x \"%.20s\"\n",
                m[0], m[1], m[2], m[3], m[4], m[5], (const char *)m);
    }
    /* DIAG: catch 0xff garbage — log the FULL hex of any write that
     * starts with 0xff or is short and contains 0xff. Helps locate
     * which guest call path is dribbling poisoned bytes onto the tty
     * (zsh ZLE refresh emits these before `\b \b` and they look like
     * "backspace inserts a space" on the user's screen). */
    if (getenv("YOS_TRACE_0XFF") && count <= 64) {
        const uint8_t *bp = (const uint8_t *)p;
        int has_ff = 0;
        for (uint32_t i = 0; i < count; i++) if (bp[i] == 0xff) { has_ff = 1; break; }
        if (has_ff) {
            fprintf(stderr, "yos_write fd=%d count=%u hex=", fd, count);
            for (uint32_t i = 0; i < count; i++) fprintf(stderr, "%02x ", bp[i]);
            fprintf(stderr, "  buf_off=0x%x\n", buf);
        }
    }

    /* Drop post-fork all-0xff tty garbage. This is an asyncify-fork
     * compatibility shim, not generic write semantics — the gate and
     * full rationale live in yos_compat_drop_fork_tty_garbage()
     * (impl/io/io-internal.h). file.c::yos_fwrite uses the same shim
     * for the stdio path. */
    if (yos_compat_drop_fork_tty_garbage(hfd, p, count))
        return (int32_t)count;
    ssize_t r = yos_plat_write(hfd, p, count);
    int saved_errno = (r < 0) ? errno : 0;
    /* YOS_DUMP_SOCK=<fd>: dump every byte written on that wasm fd —
     * sibling of the read-side dump above for MAC-failure diagnosis. */
    if (r > 0) {
        const char *dump = getenv("YOS_DUMP_SOCK");
        if (dump && (int)strtol(dump, NULL, 10) == fd) {
            FILE *f = fopen("/tmp/yos-sock-w.bin", "ab");
            if (f) { fwrite(p, 1, (size_t)r, f); fclose(f); }
        }
    }
    if (ytrace_default_enabled() && fd != 4 && fd != 5) {
        pid_t tid = yos_plat_gettid();
        char hex[3 * 32 + 1] = {0};
        if (r > 0) {
            const uint8_t *bp = (const uint8_t *)p;
            int n = r > 32 ? 32 : (int)r;
            for (int i = 0; i < n; i++)
                snprintf(hex + i * 3, 4, "%02x ", bp[i]);
        }
        ydebug("write(tid=%d wfd=%d hfd=%d count=%u) = %zd%s%s%s%s\n",
               (int)tid, fd, hfd, count, r,
               r < 0 ? " errno=" : "",
               r < 0 ? strerror(saved_errno) : "",
               r > 0 ? " hex=" : "",
               r > 0 ? hex : "");
    }
    /* POSIX contract: write returns -1 + errno on failure, NOT -errno.
     * The wasm guest checks `if (write(...) == -1)` and reads errno
     * via __error(); pre-fix, the bridge returned the raw -errno
     * value (e.g. -32 for EPIPE), so the guest's compare-with-(-1)
     * never matched and error handling silently fell through. */
    if (r < 0) return yos_errno_neg(ctx, saved_errno);
    return (int32_t)r;
}

/* Forward decls — definitions are further down with the fcntl
 * cmd/oflags translation tables. */
static int oflags_fb_to_lx(int f);
static int oflags_lx_to_fb(int f);

int32_t yos_open(struct yos_exec_ctx *ctx, uint32_t path, int32_t flags, int32_t mode)
{
    const char *raw = wstr_check(ctx, path);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);

    /* Check if path is in a virtual filesystem */
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (mt) {
        const char *remaining;
        const struct yos_file_operations *ops = yos_mount_resolve(mt, s, &remaining);
        if (ops && ops->open) {
            return ops->open(ctx, remaining, flags, mode);
        }
    }

    /* Fake-FIFO lookup. mkfifo(2) is forbidden on tvOS/iOS sandboxes,
     * so yos_mkfifo registers (path → host pipe fds) and the matching
     * open() returns a dup of the right end. See impl/fifo.c. Helper
     * sets errno=0 + returns -1 on "not a FIFO" so we fall through
     * to the regular path. */
    {
        extern int yos_fifo_try_open(struct yos_exec_ctx *ctx, const char *path, int flags);
        int hfifo = yos_fifo_try_open(ctx, s, flags);
        if (hfifo >= 0) return yos_fd_alloc(ctx, hfifo);
        if (errno != 0) return yos_errno_neg(ctx, errno);
    }

    /* Fake-PTY slave lookup. posix_openpt on sandboxed darwin (tvOS,
     * iOS) returns EPERM; impl/pty.c falls back to socketpair and
     * synthesises "/dev/yos-pts/N". The matching open() of that path
     * needs to hand back a dup of the slave end. */
    {
        extern int yos_pty_try_open(const char *path, int flags);
        int hpty = yos_pty_try_open(s, flags);
        if (hpty >= 0) return yos_fd_alloc(ctx, hpty);
        if (errno != 0) return yos_errno_neg(ctx, errno);
    }

    int hflags = oflags_fb_to_lx(flags);
    /* open() is `int open(const char *path, int flags, ...)` in
     * FreeBSD headers — variadic. clang's wasm32 ABI passes the
     * variadic mode arg via a va_list pointer in the shadow stack,
     * NOT as a direct i32. The `mode` parameter we receive is a wasm
     * offset to a small struct containing the mode int. Pulling
     * the literal `mode` value used to set garbage permission bits
     * (the wasm stack address looked like mode_t≈0x100000), which
     * surfaced as e.g. shada files created `--w-rw---T` and then
     * unreadable on the next nvim run ("permission denied"). */
    int real_mode = mode;
    if (hflags & O_CREAT) {
        if (mode && (uint32_t)mode + 4 <= ctx->memory_size)
            real_mode = *(int32_t *)(ctx->memory + (uint32_t)mode);
        /* Apply per-ctx umask in software. Host umask is forced to 0
         * at startup (main.c) so each ctx's umask doesn't fight over
         * the shared host-process value. */
        real_mode &= ~ctx->umask;
    } else {
        /* Without O_CREAT mode is ignored; don't deref a stack address
         * that may be 0 / past memory. */
        real_mode = 0;
    }
    int r = yos_plat_open(s, hflags, real_mode);
    if (ytrace_default_enabled())
        ydebug("open(\"%s\" flags=0x%x->0x%x mode_off=%d real_mode=0%o) = %d%s\n",
               s, flags, hflags, mode, real_mode, r,
               r < 0 ? strerror(errno) : "");
    if (r < 0) return yos_errno_neg(ctx, errno);
    return yos_fd_alloc_with_path(ctx, r, s);
}

int32_t yos_close(struct yos_exec_ctx *ctx, int32_t fd)
{
    /* Check if virtual fd */
    if (yos_is_virtual_fd(fd)) {
        struct yos_file_table *ft = (struct yos_file_table *)ctx->procfs_fds;
        if (!ft) return yos_errno_neg(ctx, EBADF);
        struct yos_file *file = yos_file_get(ft, fd);
        if (!file) return yos_errno_neg(ctx, EBADF);
        int32_t ret = 0;
        if (file->ops && file->ops->close)
            ret = file->ops->close(ctx, file);
        yos_file_free(ft, fd);
        return ret;
    }

    return yos_fd_close(ctx, fd);
}

void yos_closefrom(struct yos_exec_ctx *ctx, int32_t lowfd)
{
    if (lowfd < 0) lowfd = 0;
    for (int i = lowfd; i < YOS_FD_MAX; i++) {
        int hfd = ctx->fd_map[i];
        if (hfd < 0) continue;
        yos_plat_close(hfd);
        ctx->fd_map[i] = -1;
        free(ctx->fd_paths[i]);
        ctx->fd_paths[i] = NULL;
    }
}

int32_t yos_close_range(struct yos_exec_ctx *ctx, uint32_t lowfd,
                        uint32_t maxfd, int32_t flags)
{
    (void)flags;
    if (lowfd >= YOS_FD_MAX) return 0;
    uint32_t hi = maxfd >= YOS_FD_MAX ? YOS_FD_MAX - 1 : maxfd;
    for (uint32_t i = lowfd; i <= hi; i++) {
        int hfd = ctx->fd_map[i];
        if (hfd < 0) continue;
        yos_plat_close(hfd);
        ctx->fd_map[i] = -1;
        free(ctx->fd_paths[i]);
        ctx->fd_paths[i] = NULL;
    }
    return 0;
}

int32_t yos_creat(struct yos_exec_ctx *ctx, uint32_t pathname, int32_t mode)
{
    const char *s = wstr_check(ctx, pathname);
    if (!s) return yos_errno_neg(ctx, EFAULT);
    const char *resolved = yos_path_resolve(ctx, s);
    int r = creat(resolved, mode & ~ctx->umask);
    if (r < 0) return yos_errno_neg(ctx, errno);
    return yos_fd_alloc_with_path(ctx, r, resolved);
}

int32_t yos_link(struct yos_exec_ctx *ctx, uint32_t oldname, uint32_t newname)
{
    const char *o_in = wstr_check(ctx, oldname);
    const char *n_in = wstr_check(ctx, newname);
    if (!o_in || !n_in) return yos_errno_neg(ctx, EFAULT);
    /* yos_path_resolve returns a TLS buffer; the second call would
     * clobber the first when both args are relative. Snapshot to the
     * stack between calls. */
    char o_buf[PATH_MAX];
    const char *o = yos_path_resolve(ctx, o_in);
    snprintf(o_buf, sizeof o_buf, "%s", o);
    const char *n = yos_path_resolve(ctx, n_in);
    return yos_errno_check(ctx, link(o_buf, n));
}

int32_t yos_unlink(struct yos_exec_ctx *ctx, uint32_t pathname)
{
    const char *raw = wstr_check(ctx, pathname);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    /* If this was a fake FIFO, drop the registry entry first so the
     * held pipe fds get freed; then unlink the placeholder file. */
    {
        extern int yos_fifo_drop(struct yos_exec_ctx *ctx, const char *path);
        (void)yos_fifo_drop(ctx, s);
    }
    return yos_errno_check(ctx, unlink(s));
}

int32_t yos_chdir(struct yos_exec_ctx *ctx, uint32_t filename)
{
    const char *s = wstr_check(ctx, filename);
    if (!s) return yos_errno_neg(ctx, EFAULT);

    /* DO NOT call host chdir. yos's pthread-per-process model shares
     * host cwd across every guest, so a real chdir here would let
     * one guest's chdir silently move every other guest's relative
     * paths. Instead, resolve the target against the current ctx->cwd
     * + verify it exists + update ctx->cwd. Every subsequent path-
     * taking bridge will route its path through yos_path_resolve()
     * which prepends ctx->cwd for relative paths. */
    const char *resolved = yos_path_resolve(ctx, s);
    struct stat st;
    if (stat(resolved, &st) < 0)
        return yos_errno_neg(ctx, errno);
    if (!S_ISDIR(st.st_mode))
        return yos_errno_neg(ctx, ENOTDIR);

    /* Update ctx->cwd. If `s` is already absolute, use it as-is.
     * Else compose ctx->cwd + '/' + s. TODO: canonicalize . / .. */
    if (s[0] == '/') {
        strncpy(ctx->cwd, s, PATH_MAX - 1);
        ctx->cwd[PATH_MAX - 1] = '\0';
    } else {
        size_t cwdlen = strlen(ctx->cwd);
        if (cwdlen > 0 && ctx->cwd[cwdlen - 1] != '/')
            strncat(ctx->cwd, "/", PATH_MAX - cwdlen - 1);
        strncat(ctx->cwd, s, PATH_MAX - strlen(ctx->cwd) - 1);
    }
    ydebug("chdir(\"%s\") → ctx->cwd=\"%s\"\n", s, ctx->cwd);

    return 0;
}

int32_t yos_chmod(struct yos_exec_ctx *ctx, uint32_t filename, int32_t mode)
{
    const char *raw = wstr_check(ctx, filename);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    return yos_errno_check(ctx, chmod(s, mode));
}

int32_t yos_lchown(struct yos_exec_ctx *ctx, uint32_t filename, int32_t user, int32_t group)
{
    const char *raw = wstr_check(ctx, filename);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    return yos_errno_check(ctx, lchown(s, user, group));
}

int32_t yos_lseek(struct yos_exec_ctx *ctx, int32_t fd, int32_t offset, int32_t whence)
{
    (void)ctx;
    off_t r = lseek(host_fd(ctx, fd), offset, whence);
    return yos_errno_check(ctx, (int32_t)r);
}

/* _llseek: 64-bit seek for 32-bit systems
 * Combines offset_high/offset_low into 64-bit offset, writes result to *result_ptr */
int32_t yos_vfs__llseek(struct yos_exec_ctx *ctx, int32_t fd, uint32_t offset_high,
                        uint32_t offset_low, uint32_t result_ptr, int32_t whence)
{
    int64_t *result = wptr(ctx, result_ptr);
    if (!result) return yos_errno_neg(ctx, EFAULT);

    off_t offset = ((off_t)offset_high << 32) | offset_low;
    off_t r = lseek(host_fd(ctx, fd), offset, whence);
    if (r < 0) {
        return yos_errno_neg(ctx, errno);
    }
    *result = r;
    return 0;
}

/* A file yos can actually exec: a wasm module ("\0asm") or a shebang
 * script ("#!"). yos loads these regardless of the host execute bit, so
 * access(X_OK) on them must succeed even though nix-store modules are
 * 0444. Without this, anything that gates exec on access(X_OK) — tmux's
 * checkshell(), a shell's command lookup — rejects every yos program. */
static int yos_file_is_loadable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char head[4] = {0};
    size_t n = fread(head, 1, 4, f);
    fclose(f);
    if (n >= 4 && head[0] == 0x00 && head[1] == 0x61 &&
        head[2] == 0x73 && head[3] == 0x6d)
        return 1;                                   /* wasm */
    if (n >= 2 && head[0] == '#' && head[1] == '!')
        return 1;                                   /* shebang script */
    return 0;
}

int32_t yos_access(struct yos_exec_ctx *ctx, uint32_t filename, int32_t mode)
{
    const char *raw = wstr_check(ctx, filename);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    int rc = yos_plat_access(s, mode);
    if (rc == 0) return 0;
    int saved = errno;
    /* Treat X_OK as yos-executability for the files yos can load. The
     * non-exec parts of `mode` (R_OK/W_OK) must still hold for real. */
    if ((mode & X_OK) && yos_file_is_loadable(s)) {
        int without_x = mode & ~X_OK;
        if (without_x == 0 || yos_plat_access(s, without_x) == 0)
            return 0;
    }
    return yos_errno_neg(ctx, saved);
}

int32_t yos_rename(struct yos_exec_ctx *ctx, uint32_t oldname, uint32_t newname)
{
    const char *o = wstr_check(ctx, oldname);
    const char *n = wstr_check(ctx, newname);
    if (!o || !n) return yos_errno_neg(ctx, EFAULT);
    /* Each path needs its own resolved copy — yos_path_resolve uses
     * a single TLS buffer, so the second call would clobber the first. */
    char abs_o[PATH_MAX]; char abs_n[PATH_MAX];
    const char *ro = yos_path_resolve(ctx, o);
    strncpy(abs_o, ro, sizeof abs_o - 1); abs_o[sizeof abs_o - 1] = 0;
    const char *rn = yos_path_resolve(ctx, n);
    strncpy(abs_n, rn, sizeof abs_n - 1); abs_n[sizeof abs_n - 1] = 0;
    return yos_errno_check(ctx, rename(abs_o, abs_n));
}

int32_t yos_mkdir(struct yos_exec_ctx *ctx, uint32_t pathname, int32_t mode)
{
    const char *raw = wstr_check(ctx, pathname);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    return yos_errno_check(ctx, mkdir(s, mode & ~ctx->umask));
}

int32_t yos_rmdir(struct yos_exec_ctx *ctx, uint32_t pathname)
{
    const char *raw = wstr_check(ctx, pathname);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    return yos_errno_check(ctx, rmdir(s));
}

int32_t yos_pipe(struct yos_exec_ctx *ctx, uint32_t fildes)
{
    int *p = wptr(ctx, fildes);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    int hfds[2];
    if (pipe(hfds) < 0) return yos_errno_neg(ctx, errno);
    int32_t r = yos_fd_alloc(ctx, hfds[0]);
    if (r < 0) { yos_plat_close(hfds[1]); return r; }
    int32_t w = yos_fd_alloc(ctx, hfds[1]);
    if (w < 0) { yos_fd_close(ctx, r); return w; }
    p[0] = r;
    p[1] = w;
    ydebug("pipe -> wfd_r=%d hfd_r=%d wfd_w=%d hfd_w=%d\n",
           r, hfds[0], w, hfds[1]);
    return 0;
}

/* FreeBSD ioctl request numbers (from sys/ttycom.h, sys/filio.h) that
 * nvim/libuv reach for. Linux's request numbers for the equivalent
 * operations are completely different — same operation, different
 * encoding scheme — so the wasm guest's `ioctl(tty_fd, FB_TIOCGWINSZ)`
 * passed straight to host glibc returns ENOTTY because Linux doesn't
 * recognise the request. Translate request → request, then call host
 * ioctl. The arg buffer (struct winsize / int) is layout-compatible
 * for the ones we need so no struct conversion is required. */
#define FB_TIOCGWINSZ   0x40087468u   /* _IOR('t', 0x68, struct winsize) */
#define FB_TIOCSWINSZ   0x80087467u
#define FB_TIOCGPGRP    0x40047477u   /* _IOR('t', 0x77, int) */
#define FB_TIOCSPGRP    0x80047476u
#define FB_TIOCSCTTY    0x20007461u
#define FB_TIOCNOTTY    0x20007471u
#define FB_FIONREAD     0x4004667fu
#define FB_FIONBIO      0x8004667eu
#define FB_FIOCLEX      0x20006601u
#define FB_FIONCLEX     0x20006602u
#define FB_FIOASYNC     0x8004667du
/* TIOCPKT: PTY-master packet mode. telnetd enables this so each
 * read returns a leading status byte (TIOCPKT_FLUSHWRITE etc.) plus
 * the data — used to interleave terminal-state changes with the data
 * stream. The "(void) ioctl(p, TIOCPKT, &on)" call ignores failure;
 * if the host returns ENOTTY, telnetd still goes on to strip byte[0]
 * of every read as "the status byte", eating the leading ESC of every
 * escape sequence — visible as zsh's prompt repaint showing `[K` /
 * `[?2004h` literal in the client (no ESC byte → terminal doesn't
 * interpret) and ZLE behaving as if Enter fired after each char. */
#define FB_TIOCPKT      0x80047470u   /* _IOW('t', 112, int) */

#define LX_TIOCGWINSZ   0x5413u
#define LX_TIOCSWINSZ   0x5414u
#define LX_TIOCGPGRP    0x540Fu
#define LX_TIOCSPGRP    0x5410u
#define LX_TIOCSCTTY    0x540Eu
#define LX_TIOCNOTTY    0x5422u
#define LX_FIONREAD     0x541Bu
#define LX_FIONBIO      0x5421u
#define LX_FIOCLEX      0x5451u
#define LX_FIONCLEX     0x5450u
#define LX_FIOASYNC     0x5452u
#define LX_TIOCPKT      0x5420u


int32_t yos_ioctl(struct yos_exec_ctx *ctx, int32_t fd, uint32_t cmd, uint32_t arg)
{
    uint32_t lcmd = ioctl_cmd_fb_to_lx(cmd);

    /* ioctl is variadic in C; clang's wasm32 ABI passes the third
     * argument via a va_list pointer on the shadow stack — `arg` is
     * the offset of that pack, NOT the offset of the user buffer. The
     * first slot of the pack is the user's actual wasm pointer (or
     * the integer value, for value-arg ioctls). Without this
     * dereference, the host kernel writes/reads the pack location
     * instead of the user's struct, and TIOCGWINSZ silently leaves
     * the caller's `struct winsize` zero — nvim's tui_guess_size
     * then falls through to terminfo defaults (80×24).
     *
     * For value-arg ioctls (TIOCSCTTY, FIONBIO with int*, ...) the
     * dereferenced word is either the int value (passed directly)
     * or another wasm pointer; treating it as a pointer and passing
     * the host translation works for both cases — the host kernel
     * interprets the third arg per the request encoding. */
    uint32_t user_arg = arg;
    if (arg && (uint32_t)arg + 4 <= ctx->memory_size) {
        user_arg = *(uint32_t *)(ctx->memory + arg);
    }

    ydebug("ioctl(tid=%d fd=%d, cmd=0x%x->0x%x, va_pack=0x%x user_arg=0x%x)\n",
           (int)(uintptr_t)pthread_self(), fd, cmd, lcmd, arg, user_arg);

    int hfd = host_fd(ctx, fd);
    void *argp = user_arg ? wptr(ctx, user_arg) : NULL;

    /* TIOCPKT on a fake-PTY master: remember the flag so subsequent
     * read()s on the master prepend the synthesised status byte.
     * Without this every output char gets eaten by telnetd as a
     * packet flag and FLUSHWRITE bits emit spurious IAC DM. Match
     * EITHER encoding since ioctl_cmd_fb_to_lx is a no-op on darwin
     * (lcmd stays as FB_TIOCPKT) and a translation on Linux. */
    if ((lcmd == LX_TIOCPKT || lcmd == FB_TIOCPKT || cmd == FB_TIOCPKT)
        && hfd >= 0) {
        extern int yos_pty_set_packet_mode(int hfd, int on);
        int on_val = (argp && user_arg) ? *(int *)argp : 0;
        if (yos_pty_set_packet_mode(hfd, on_val)) return 0;
    }

    /* Virtualize controlling-tty foreground pgrp queries/sets so the
     * pgid the wasm caller stores/reads belongs to the *guest* pid
     * namespace (matching getpgrp(), setpgid()) rather than the host
     * shell's pgrp the kernel would report. Only intercept on tty fds
     * — non-tty TIOCGPGRP/TIOCSPGRP would just fail with ENOTTY which
     * is the correct kernel behavior. */
    if ((lcmd == LX_TIOCGPGRP || lcmd == LX_TIOCSPGRP)
        && hfd >= 0 && yos_plat_isatty(hfd)) {
        if (!argp) return yos_errno_neg(ctx, EFAULT);
        if (lcmd == LX_TIOCGPGRP) {
            *(int32_t *)argp = ctx->rt->fg_pgid;
            ydebug("ioctl TIOCGPGRP(virt) = %d\n", ctx->rt->fg_pgid);
            return 0;
        } else {
            int32_t pgid = *(int32_t *)argp;
            if (pgid <= 0) return yos_errno_neg(ctx, EINVAL);
            ctx->rt->fg_pgid = pgid;
            ydebug("ioctl TIOCSPGRP(virt) <- %d\n", pgid);
            return 0;
        }
    }
    /* TIOCSCTTY: caller wants this tty as its controlling terminal.
     * The kernel's bookkeeping is per-host-process and doesn't fit
     * one-pthread-per-guest-proc; just accept and update the
     * virtualized fg pgrp to the caller's pgrp. */
    if (lcmd == LX_TIOCSCTTY && hfd >= 0 && yos_plat_isatty(hfd)) {
        if (ctx->proc) ctx->rt->fg_pgid = ctx->proc->pgid;
        ydebug("ioctl TIOCSCTTY(virt) fg_pgid <- %d\n", ctx->rt->fg_pgid);
        return 0;
    }

    int r = ioctl(hfd, lcmd, argp);
    ydebug("ioctl = %d (errno=%d)\n", r, r < 0 ? errno : 0);
    if (lcmd == LX_TIOCGWINSZ && r == 0 && argp) {
        unsigned short *ws = (unsigned short *)argp;
        ydebug("  winsize: row=%u col=%u xpix=%u ypix=%u\n",
               ws[0], ws[1], ws[2], ws[3]);
    }
    return yos_errno_check(ctx, (int32_t)r);
}

/* fcntl command numbers diverge between FreeBSD and Linux past the
 * common 0..4 range. Most importantly nvim's `fcntl(fd, F_DUPFD_CLOEXEC,
 * 3)` (in channel_from_stdio for the embedded server) passes the
 * FreeBSD value (17); host Linux glibc expects 1030. Without this
 * translation the call returns -1, nvim asserts in stream_init, and
 * the embedded server crashes — leaving the TUI parent's RPC writes
 * to EPIPE. Add the translation table and the matching arg/flag
 * remap for F_GETFL/F_SETFL (O_* values also differ). */
#define FB_F_DUPFD              0
#define FB_F_GETFD              1
#define FB_F_SETFD              2
#define FB_F_GETFL              3
#define FB_F_SETFL              4
#define FB_F_GETOWN             5
#define FB_F_SETOWN             6
#define FB_F_GETLK              11
#define FB_F_SETLK              12
#define FB_F_SETLKW             13
#define FB_F_DUPFD_CLOEXEC      17
#define FB_F_DUP2FD_CLOEXEC     18

static int fcntl_cmd_fb_to_lx(int cmd)
{
    switch (cmd) {
    case FB_F_DUPFD:           return F_DUPFD;
    case FB_F_GETFD:           return F_GETFD;
    case FB_F_SETFD:           return F_SETFD;
    case FB_F_GETFL:           return F_GETFL;
    case FB_F_SETFL:           return F_SETFL;
    case FB_F_GETOWN:          return F_GETOWN;
    case FB_F_SETOWN:          return F_SETOWN;
    case FB_F_GETLK:           return F_GETLK;
    case FB_F_SETLK:           return F_SETLK;
    case FB_F_SETLKW:          return F_SETLKW;
    case FB_F_DUPFD_CLOEXEC:   return F_DUPFD_CLOEXEC;
    default:                   return cmd;  /* pass through, may EINVAL */
    }
}

/* O_* flag bits — FreeBSD vs Linux. Used by open/openat/fcntl(F_*FL).
 * Bottom 2 bits (RDONLY/WRONLY/RDWR) match. The rest is per-flag
 * remap: most differ in BIT POSITION. Without translation, opens with
 * `O_CREAT | O_EXCL` (FreeBSD: 0x200|0x800) get sent to host glibc as
 * O_NOCTTY|O_NDELAY which neither creates nor enforces exclusivity —
 * nvim's swap-file mkstemp loop fails through every variant name
 * and surfaces as E326/E303. */
#define FB_O_NONBLOCK   0x00000004
#define FB_O_APPEND     0x00000008
#define FB_O_SHLOCK     0x00000010
#define FB_O_EXLOCK     0x00000020
#define FB_O_ASYNC      0x00000040
#define FB_O_SYNC       0x00000080
#define FB_O_NOFOLLOW   0x00000100
#define FB_O_CREAT      0x00000200
#define FB_O_TRUNC      0x00000400
#define FB_O_EXCL       0x00000800
#define FB_O_NOCTTY     0x00008000
#define FB_O_DIRECT     0x00010000
#define FB_O_DIRECTORY  0x00020000
#define FB_O_EXEC       0x00040000
#define FB_O_TTY_INIT   0x00080000
#define FB_O_CLOEXEC    0x00100000
#define FB_O_PATH       0x00400000

#define LX_O_NONBLOCK   0x00000800
#define LX_O_APPEND     0x00000400
#define LX_O_ASYNC      0x00002000
#define LX_O_SYNC       0x00101000
#define LX_O_NOFOLLOW   0x00020000
#define LX_O_CREAT      0x00000040
#define LX_O_TRUNC      0x00000200
#define LX_O_EXCL       0x00000080
#define LX_O_NOCTTY     0x00000100
#define LX_O_DIRECT     0x00004000
#define LX_O_DIRECTORY  0x00010000
#define LX_O_PATH       0x00200000
#define LX_O_CLOEXEC    0x00080000

int oflags_fb_to_lx_fwd(int);  /* exported for impl/posix.c */
/* Host-native O_* values via the system header. On darwin most flags
 * collide with the FreeBSD bit pattern (both are BSD lineage), but
 * a few don't: darwin O_CLOEXEC=0x01000000 vs FreeBSD's 0x00100000,
 * and darwin doesn't define O_DIRECT or O_PATH at all. Use the
 * system macro where available, else 0 (drop the flag). */
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#ifndef O_PATH
#define O_PATH 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

static int oflags_fb_to_lx(int f)
{
    int r = (f & 3);
    if (f & FB_O_NONBLOCK)   r |= O_NONBLOCK;
    if (f & FB_O_APPEND)     r |= O_APPEND;
    if (f & FB_O_ASYNC)      r |= O_ASYNC;
    if (f & FB_O_SYNC)       r |= O_SYNC;
    if (f & FB_O_NOFOLLOW)   r |= O_NOFOLLOW;
    if (f & FB_O_CREAT)      r |= O_CREAT;
    if (f & FB_O_TRUNC)      r |= O_TRUNC;
    if (f & FB_O_EXCL)       r |= O_EXCL;
    if (f & FB_O_NOCTTY)     r |= O_NOCTTY;
    if (f & FB_O_DIRECT)     r |= O_DIRECT;
    if (f & FB_O_DIRECTORY)  r |= O_DIRECTORY;
    if (f & FB_O_EXEC)       r |= O_PATH;     /* closest match */
    if (f & FB_O_CLOEXEC)    r |= O_CLOEXEC;
    if (f & FB_O_PATH)       r |= O_PATH;
    /* SHLOCK/EXLOCK/TTY_INIT have no portable equivalent — drop. */
    return r;
}

int oflags_fb_to_lx_fwd(int f) { return oflags_fb_to_lx(f); }

/* ── AT_* flag translation (FreeBSD wasm ↔ host Linux) ───────────────
 *
 * These are passed to the openat/fstatat/faccessat/unlinkat/etc.
 * `*at()` family. FreeBSD vs Linux values diverge — most painfully
 * AT_SYMLINK_NOFOLLOW (FreeBSD 0x200, Linux 0x100) collides with
 * Linux's AT_REMOVEDIR (0x200), which is why ls -l /tmp returns
 * "Invalid argument" — host fstatat sees AT_REMOVEDIR and rejects.
 *
 * Mapping (only the bits user code actually passes):
 *
 *   constant            FreeBSD  Linux
 *   AT_EACCESS          0x100    0x200
 *   AT_SYMLINK_NOFOLLOW 0x200    0x100
 *   AT_SYMLINK_FOLLOW   0x400    0x400
 *   AT_REMOVEDIR        0x800    0x200
 *   AT_NO_AUTOMOUNT     —        0x800   (FreeBSD has no equivalent;
 *                                          ignore on guest→host)
 *   AT_EMPTY_PATH       0x4000   0x1000
 *
 * AT_FDCWD = -100 on both, no translation needed.
 */
#ifndef AT_NO_AUTOMOUNT
#define AT_NO_AUTOMOUNT 0x800
#endif
/* AT_EMPTY_PATH is a Linux extension (kernel 5.8+; glibc exposes it
 * via fcntl.h). darwin/BSD have no equivalent — when the guest sets
 * the FreeBSD-shape bit, we silently drop it on those hosts. Guests
 * that depend on AT_EMPTY_PATH semantics (rare; mostly fstatat with
 * an empty path to mean "stat the fd itself") get the wrong answer,
 * but no host crash. */
#ifndef AT_EMPTY_PATH
#  define AT_EMPTY_PATH 0
#endif

#define FB_AT_EACCESS          0x0100
#define FB_AT_SYMLINK_NOFOLLOW 0x0200
#define FB_AT_SYMLINK_FOLLOW   0x0400
#define FB_AT_REMOVEDIR        0x0800
#define FB_AT_EMPTY_PATH       0x4000

int yos_at_flags_fb_to_lx(int f)
{
    int r = 0;
    if (f & FB_AT_EACCESS)          r |= AT_EACCESS;
    if (f & FB_AT_SYMLINK_NOFOLLOW) r |= AT_SYMLINK_NOFOLLOW;
    if (f & FB_AT_SYMLINK_FOLLOW)   r |= AT_SYMLINK_FOLLOW;
    if (f & FB_AT_REMOVEDIR)        r |= AT_REMOVEDIR;
    if (f & FB_AT_EMPTY_PATH)       r |= AT_EMPTY_PATH;
    return r;
}

static int oflags_lx_to_fb(int f)
{
    int r = (f & 3);
    if (f & O_NONBLOCK)             r |= FB_O_NONBLOCK;
    if (f & O_APPEND)               r |= FB_O_APPEND;
    if (f & O_ASYNC)                r |= FB_O_ASYNC;
    if (f & O_SYNC)                 r |= FB_O_SYNC;
    if (O_NOFOLLOW  && (f & O_NOFOLLOW))   r |= FB_O_NOFOLLOW;
    if (f & O_CREAT)                r |= FB_O_CREAT;
    if (f & O_TRUNC)                r |= FB_O_TRUNC;
    if (f & O_EXCL)                 r |= FB_O_EXCL;
    if (f & O_NOCTTY)               r |= FB_O_NOCTTY;
    if (O_DIRECT    && (f & O_DIRECT))     r |= FB_O_DIRECT;
    if (O_DIRECTORY && (f & O_DIRECTORY))  r |= FB_O_DIRECTORY;
    if (f & O_CLOEXEC)              r |= FB_O_CLOEXEC;
    if (O_PATH      && (f & O_PATH))       r |= FB_O_PATH;
    return r;
}

int32_t yos_fcntl(struct yos_exec_ctx *ctx, int32_t fd, int32_t cmd, int32_t arg)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) {
        ydebug("fcntl(wfd=%d) -> EBADF (no fd_map entry)\n", fd);
        return yos_errno_neg(ctx, EBADF);
    }
    int hcmd = fcntl_cmd_fb_to_lx(cmd);
    /* fcntl is declared `int fcntl(int fd, int cmd, ...)` in the
     * FreeBSD headers nvim was built against. clang's wasm32 ABI
     * passes the variadic arg via a va_list pointer in the shadow
     * stack, NOT as a direct i32 — so the `arg` parameter we
     * receive is a wasm offset into a small struct of varargs.
     * Read the actual int from the first slot. (Variadic ints are
     * 4-byte-aligned in clang's wasm32 va layout — see
     * impl/printf.c's va_align for the same convention.) */
    int real_arg = arg;
    if (hcmd == F_DUPFD || hcmd == F_DUPFD_CLOEXEC ||
        hcmd == F_SETFD || hcmd == F_SETFL ||
        hcmd == F_SETOWN) {
        if (arg && (uint32_t)arg + 4 <= ctx->memory_size)
            real_arg = *(int32_t *)(ctx->memory + (uint32_t)arg);
    }
    if (ytrace_default_enabled())
        ydebug("fcntl(wfd=%d hfd=%d cmd=%d->%d va_off=%d arg=%d)\n",
               fd, hfd, cmd, hcmd, arg, real_arg);
    /* F_DUPFD / F_DUPFD_CLOEXEC return a fresh host fd that needs a
     * wasm-fd slot like dup() does. Other fcntl commands return flags
     * or 0 — pass through unchanged. */
    if (hcmd == F_DUPFD || hcmd == F_DUPFD_CLOEXEC) {
        int r = fcntl(hfd, hcmd, real_arg);
        if (r < 0) return yos_errno_neg(ctx, errno);
        return yos_fd_alloc(ctx, r);
    }
    if (hcmd == F_SETFL) {
        int r = fcntl(hfd, hcmd, oflags_fb_to_lx(real_arg));
        return yos_errno_check(ctx, (int32_t)r);
    }
    if (hcmd == F_GETFL) {
        int r = fcntl(hfd, hcmd, 0);
        if (r < 0) return yos_errno_neg(ctx, errno);
        return oflags_lx_to_fb(r);
    }
    int r = fcntl(hfd, hcmd, real_arg);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_vfs_fcntl64(struct yos_exec_ctx *ctx, int32_t fd, int32_t cmd, int32_t arg)
{
    return yos_fcntl(ctx, fd, cmd, arg);
}

int32_t yos_vfs_chroot(struct yos_exec_ctx *ctx, uint32_t filename)
{
    (void)filename;
    return yos_errno_neg(ctx, EPERM);
}

int32_t yos_symlink(struct yos_exec_ctx *ctx, uint32_t oldpath, uint32_t newpath)
{
    /* `oldpath` is the symlink target — STORED as text in the new
     * symlink, never dereferenced here — so do NOT resolve it against
     * ctx->cwd. Only `newpath` (where the symlink is created) needs
     * cwd resolution. */
    const char *o = wstr_check(ctx, oldpath);
    const char *n_in = wstr_check(ctx, newpath);
    if (!o || !n_in) return yos_errno_neg(ctx, EFAULT);
    const char *n = yos_path_resolve(ctx, n_in);
    return yos_errno_check(ctx, symlink(o, n));
}

int32_t yos_readlink(struct yos_exec_ctx *ctx, uint32_t path, uint32_t buf, uint32_t bufsiz)
{
    const char *s_in = wstr_check(ctx, path);
    char *b = (char *)wptr_range(ctx, buf, bufsiz);
    if (!s_in || !b) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, s_in);

    /* Check if path is in a virtual filesystem */
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (mt) {
        const char *remaining;
        const struct yos_file_operations *ops = yos_mount_resolve(mt, s, &remaining);
        if (ops && ops->readlink) {
            return ops->readlink(ctx, remaining, b, bufsiz);
        }
    }

    ssize_t r = readlink(s, b, bufsiz);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_truncate(struct yos_exec_ctx *ctx, uint32_t path, int32_t length)
{
    const char *s_in = wstr_check(ctx, path);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, s_in);
    return yos_errno_check(ctx, truncate(s, length));
}

int32_t yos_ftruncate(struct yos_exec_ctx *ctx, int32_t fd, int32_t length)
{
    (void)ctx;
    return yos_errno_check(ctx, ftruncate(host_fd(ctx, fd), length));
}

int32_t yos_fchmod(struct yos_exec_ctx *ctx, int32_t fd, int32_t mode)
{
    (void)ctx;
    return yos_errno_check(ctx, fchmod(host_fd(ctx, fd), mode));
}

int32_t yos_fchown(struct yos_exec_ctx *ctx, int32_t fd, int32_t user, int32_t group)
{
    (void)ctx;
    return yos_errno_check(ctx, fchown(host_fd(ctx, fd), user, group));
}

/* Convert a wasm32 iovec[] (8 bytes/entry: u32 base, u32 len) into a host
 * iovec[] (16 bytes/entry: u64 base, u64 len) by translating each base
 * pointer through the wasm linear memory. The host array must already
 * be sized for `vlen` entries. Returns 0 on success, -errno on bad input
 * (including unbounded vlen, vector table out of memory, or any iovec
 * base+len pair out of wasm memory). YOS_IOV_MAX is in io-internal.h
 * so the linux-slice iovec callers see the same cap. */
int yos_iovec_w32_to_host(struct yos_exec_ctx *ctx,
                                  uint32_t wasm_vec, int vlen,
                                  struct iovec *host_iov)
{
    if (vlen == 0) return 0;
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    /* Validate the FULL wasm-iovec table range — wptr(wasm_vec) alone
     * doesn't catch wasm_vec + vlen*8 spilling past memory_size. */
    uint8_t *iov_ptr = wptr_range(ctx, wasm_vec, (uint64_t)vlen * 8u);
    if (!iov_ptr) return yos_errno_neg(ctx, EFAULT);
    for (int i = 0; i < vlen; i++) {
        uint32_t base = *(uint32_t *)(iov_ptr + i * 8);
        uint32_t len  = *(uint32_t *)(iov_ptr + i * 8 + 4);
        /* Validate each base+len pair; a single bad iovec must fail
         * the whole call before any kernel I/O touches wasm memory. */
        void *p = wptr_range(ctx, base, len);
        if (!p && len > 0) return yos_errno_neg(ctx, EFAULT);
        host_iov[i].iov_base = p;
        host_iov[i].iov_len = len;
    }
    return 0;
}

int32_t yos_readv(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen)
{
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec host_iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, vlen, host_iov);
    if (r) return r;
    ssize_t n = readv(host_fd(ctx, fd), host_iov, vlen);
    return yos_errno_check(ctx, (int32_t)n);
}

int32_t yos_writev(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen)
{
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec host_iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, vlen, host_iov);
    if (r) return r;
    int hfd = host_fd(ctx, fd);
    ssize_t n = writev(hfd, host_iov, vlen);
    if (ytrace_default_enabled()) {
        size_t total = 0;
        for (int i = 0; i < vlen; i++) total += host_iov[i].iov_len;
        pid_t tid = yos_plat_gettid();
        ydebug("writev(tid=%d wfd=%d hfd=%d vlen=%d total=%zu) = %zd%s\n",
               (int)tid, fd, hfd, vlen, total, n,
               n < 0 ? strerror(errno) : "");
    }
    return yos_errno_check(ctx, (int32_t)n);
}

int32_t yos_vfs_pread64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count, uint32_t pos)
{
    void *p = wptr_range(ctx, buf, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    ssize_t r = pread(host_fd(ctx, fd), p, count, pos);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_vfs_pwrite64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count, uint32_t pos)
{
    void *p = wptr_range(ctx, buf, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    ssize_t r = pwrite(host_fd(ctx, fd), p, count, pos);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_chown(struct yos_exec_ctx *ctx, uint32_t filename, int32_t user, int32_t group)
{
    const char *raw = wstr_check(ctx, filename);
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *s = yos_path_resolve(ctx, raw);
    return yos_errno_check(ctx, chown(s, user, group));
}

int32_t yos_getcwd(struct yos_exec_ctx *ctx, uint32_t buf, uint32_t size)
{
    /* Linux extension: getcwd(NULL, 0) → libc allocates a buffer big
     * enough for the path. We honour that by allocating wasm-side
     * memory via yos_malloc and returning the new offset. pwd
     * (FreeBSD) uses this convention; without it, my bridge
     * returned -EFAULT and pwd dereferenced 0xfffffff2 as a string
     * → SIGSEGV. */
    if (buf == 0) {
        char hostbuf[4096];
        const char *src = NULL;
        if (ctx->cwd[0] == '/') src = ctx->cwd;
        else if (getcwd(hostbuf, sizeof(hostbuf))) src = hostbuf;
        else return yos_errno_neg(ctx, errno);
        size_t n = strlen(src) + 1;
        extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);
        uint32_t off = yos_malloc(ctx, (uint32_t)n);
        if (!off) return yos_errno_neg(ctx, ENOMEM);
        memcpy(ctx->memory + off, src, n);
        return (int32_t)off;
    }
    char *b = wptr(ctx, buf);
    if (!b) return yos_errno_neg(ctx, EFAULT);
    /* Prefer the per-runtime tracked cwd over host getcwd() so that two
     * forked yos processes (pthreads of one host pid sharing one host
     * cwd) report their own paths after each does its own chdir. Falls
     * back to host getcwd if ctx->cwd hasn't been initialized. */
    if (ctx->cwd[0] == '/') {
        size_t n = strlen(ctx->cwd) + 1;
        if (n > size) return yos_errno_neg(ctx, ERANGE);
        memcpy(b, ctx->cwd, n);
        return (int32_t)buf;
    }
    return getcwd(b, size) ? (int32_t)buf : yos_errno_neg(ctx, errno);
}

int32_t yos_openat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t flags, int32_t mode)
{
    const char *path_in = wstr_check(ctx, filename);
    if (!path_in) return yos_errno_neg(ctx, EFAULT);

    /* Check if path is in a virtual filesystem */
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (mt) {
        const char *remaining;
        const struct yos_file_operations *ops = yos_mount_resolve(mt, path_in, &remaining);
        if (ops && ops->open) {
            return ops->open(ctx, remaining, flags, mode);
        }
    }

    /* Translate dfd through the per-runtime fd table. AT_FDCWD passes
     * through. Without this, dfd was the wasm-side fd number and the
     * host kernel interpreted it as some unrelated host fd. */
    /* dfd may legitimately be AT_FDCWD (-100) — only treat it as
     * an error if it's negative AND not AT_FDCWD. The previous
     * check returned -100 immediately for AT_FDCWD so the host
     * openat call never fired. */
    int host_dfd = (dfd == AT_FDCWD) ? AT_FDCWD : yos_fd_get(ctx, dfd);
    if (host_dfd < 0 && host_dfd != AT_FDCWD)
        return host_dfd;
    /* For AT_FDCWD, resolve the relative path against ctx->cwd so the
     * guest's chdir is honored — the host's AT_FDCWD points at the
     * host process cwd, which is shared across all guests. */
    const char *path = yos_path_resolve_at(ctx, host_dfd, path_in);

    int hflags = oflags_fb_to_lx(flags);
    /* See yos_open: openat is also variadic; mode comes via a wasm
     * va_list pointer when O_CREAT is set. */
    int real_mode = mode;
    if (hflags & O_CREAT) {
        if (mode && (uint32_t)mode + 4 <= ctx->memory_size)
            real_mode = *(int32_t *)(ctx->memory + (uint32_t)mode);
        real_mode &= ~ctx->umask;
    } else {
        real_mode = 0;
    }
    int r = openat(host_dfd, path, hflags, real_mode);
    if (ytrace_default_enabled())
        ydebug("openat(dfd=%d \"%s\" flags=0x%x->0x%x mode_off=%d real_mode=0%o) = %d%s\n",
               host_dfd, path, flags, hflags, mode, real_mode, r,
               r < 0 ? strerror(errno) : "");
    if (r < 0) return yos_errno_neg(ctx, errno);
    /* Resolve the path the openat actually targeted so we can record
     * it in the fd table. Three cases:
     *   1. `path` absolute → store as-is.
     *   2. dfd == AT_FDCWD → resolve against ctx->cwd.
     *   3. real dfd → resolve against the dfd's stored path. */
    char openat_abs[PATH_MAX];
    const char *record = NULL;
    if (path[0] == '/') {
        record = path;
    } else if (dfd == YOS_FBSD_AT_FDCWD || dfd == AT_FDCWD) {
        record = NULL;   /* let yos_fd_alloc_with_path do cwd-relative join */
    } else {
        const char *base = (dfd >= 0 && dfd < YOS_FD_MAX)
                           ? ctx->fd_paths[dfd] : NULL;
        if (base) {
            size_t bn = strlen(base);
            int need_slash = (bn > 0 && base[bn - 1] != '/');
            snprintf(openat_abs, sizeof openat_abs, "%s%s%s", base,
                     need_slash ? "/" : "", path);
            record = openat_abs;
        }
    }
    int wfd = yos_fd_alloc_with_path(ctx, r, record ? record : path);
    if (wfd < 0) yos_plat_close(r);
    return wfd;
}

int32_t yos_mkdirat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname, int32_t mode)
{
    const char *s_in = wstr_check(ctx, pathname);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    /* dfd was previously passed straight to host mkdirat — interpreting
     * the guest's wasm fd as a host fd number; any real dfd hit the
     * wrong directory or EBADF. */
    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    return yos_errno_check(ctx, mkdirat(host_dfd, s, mode & ~ctx->umask));
}

int32_t yos_vfs_mknodat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t mode, uint32_t dev)
{
    const char *s_in = wstr_check(ctx, filename);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    return yos_errno_check(ctx, mknodat(host_dfd, s, mode, dev));
}

int32_t yos_fchownat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t user, int32_t group, int32_t flag)
{
    const char *s_in = wstr_check(ctx, filename);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    return yos_errno_check(ctx, fchownat(host_dfd, s, user, group, yos_at_flags_fb_to_lx(flag)));
}

int32_t yos_vfs_fstatat64(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, uint32_t statbuf, int32_t flag)
{
    const char *path_in = wstr_check(ctx, filename);
    if (!path_in) return yos_errno_neg(ctx, EFAULT);

    /* Check if path is in a virtual filesystem */
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (mt) {
        const char *remaining;
        const struct yos_file_operations *ops = yos_mount_resolve(mt, path_in, &remaining);
        if (ops && ops->stat) {
            void *buf = wptr(ctx, statbuf);
            if (!buf) return yos_errno_neg(ctx, EFAULT);
            return ops->stat(ctx, remaining, buf);
        }
    }

    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *path = yos_path_resolve_at(ctx, host_dfd, path_in);
    int host_flag = yos_at_flags_fb_to_lx(flag);
    ydebug("fstatat(dfd=%d->%d path=\"%s\" flag=0x%x->0x%x)\n",
           dfd, host_dfd, path, flag, host_flag);
    struct stat st;
    int ret = fstatat(host_dfd, path, &st, host_flag);
    if (ret < 0) return yos_errno_neg(ctx, errno);
    /* Convert host stat to wasm32 stat64 - simplified, copy key fields */
    if (statbuf && statbuf < ctx->memory_size - 96) {
        uint8_t *buf = ctx->memory + statbuf;
        memset(buf, 0, 96);
        /* stat64 layout for i386 - see include/linux-i386/asm/stat.h */
        *(uint64_t *)(buf + 0) = st.st_dev;
        *(uint32_t *)(buf + 12) = st.st_ino;  /* __st_ino (32-bit) */
        *(uint32_t *)(buf + 16) = st.st_mode;
        *(uint32_t *)(buf + 20) = st.st_nlink;
        *(uint32_t *)(buf + 24) = st.st_uid;
        *(uint32_t *)(buf + 28) = st.st_gid;
        *(uint64_t *)(buf + 32) = st.st_rdev;
        *(int64_t *)(buf + 48) = st.st_size;
        *(uint32_t *)(buf + 56) = (uint32_t)yos_plat_stat_blksize(&st);
        *(uint64_t *)(buf + 64) = (uint64_t)yos_plat_stat_blocks(&st);
        *(uint32_t *)(buf + 72) = st.st_atime;
        *(uint32_t *)(buf + 80) = st.st_mtime;
        *(uint32_t *)(buf + 88) = st.st_ctime;
        *(uint64_t *)(buf + 96 - 8) = st.st_ino; /* st_ino (64-bit) */
    }
    return 0;
}

int32_t yos_unlinkat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname, int32_t flag)
{
    const char *s_in = wstr_check(ctx, pathname);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    return yos_errno_check(ctx, unlinkat(host_dfd, s, yos_at_flags_fb_to_lx(flag)));
}

int32_t yos_renameat(struct yos_exec_ctx *ctx, int32_t olddfd, uint32_t oldname, int32_t newdfd, uint32_t newname)
{
    const char *o_in = wstr_check(ctx, oldname);
    const char *n_in = wstr_check(ctx, newname);
    if (!o_in || !n_in) return yos_errno_neg(ctx, EFAULT);
    int host_olddfd = yos_xlate_dfd(ctx, olddfd);
    int host_newdfd = yos_xlate_dfd(ctx, newdfd);
    /* Both resolves may use yos_path_resolve's TLS buffer — snapshot
     * the first result before kicking off the second. */
    char o_buf[PATH_MAX];
    const char *o = yos_path_resolve_at(ctx, host_olddfd, o_in);
    snprintf(o_buf, sizeof o_buf, "%s", o);
    const char *n = yos_path_resolve_at(ctx, host_newdfd, n_in);
    return yos_errno_check(ctx, renameat(host_olddfd, o_buf, host_newdfd, n));
}

int32_t yos_linkat(struct yos_exec_ctx *ctx, int32_t olddfd, uint32_t oldname, int32_t newdfd, uint32_t newname, int32_t flags)
{
    const char *o_in = wstr_check(ctx, oldname);
    const char *n_in = wstr_check(ctx, newname);
    if (!o_in || !n_in) return yos_errno_neg(ctx, EFAULT);
    int host_olddfd = yos_xlate_dfd(ctx, olddfd);
    int host_newdfd = yos_xlate_dfd(ctx, newdfd);
    char o_buf[PATH_MAX];
    const char *o = yos_path_resolve_at(ctx, host_olddfd, o_in);
    snprintf(o_buf, sizeof o_buf, "%s", o);
    const char *n = yos_path_resolve_at(ctx, host_newdfd, n_in);
    return yos_errno_check(ctx, linkat(host_olddfd, o_buf, host_newdfd, n, yos_at_flags_fb_to_lx(flags)));
}

int32_t yos_symlinkat(struct yos_exec_ctx *ctx, uint32_t oldname, int32_t newdfd, uint32_t newname)
{
    /* `oldname` is the link's TEXT CONTENT, never dereferenced here. */
    const char *o = wstr_check(ctx, oldname);
    const char *n_in = wstr_check(ctx, newname);
    if (!o || !n_in) return yos_errno_neg(ctx, EFAULT);
    int host_newdfd = yos_xlate_dfd(ctx, newdfd);
    const char *n = yos_path_resolve_at(ctx, host_newdfd, n_in);
    return yos_errno_check(ctx, symlinkat(o, host_newdfd, n));
}

int32_t yos_readlinkat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t path, uint32_t buf, uint32_t bufsiz)
{
    const char *s_in = wstr_check(ctx, path);
    char *b = (char *)wptr_range(ctx, buf, bufsiz);
    if (!s_in || !b) return yos_errno_neg(ctx, EFAULT);

    /* Check if path is in a virtual filesystem */
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (mt) {
        const char *remaining;
        const struct yos_file_operations *ops = yos_mount_resolve(mt, s_in, &remaining);
        if (ops && ops->readlink) {
            return ops->readlink(ctx, remaining, b, bufsiz);
        }
    }

    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    ssize_t r = readlinkat(host_dfd, s, b, bufsiz);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_fchmodat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t mode)
{
    const char *s_in = wstr_check(ctx, filename);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    return yos_errno_check(ctx, fchmodat(host_dfd, s, mode, 0));
}

int32_t yos_faccessat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t mode)
{
    const char *s_in = wstr_check(ctx, filename);
    if (!s_in) return yos_errno_neg(ctx, EFAULT);
    int host_dfd = yos_xlate_dfd(ctx, dfd);
    const char *s = yos_path_resolve_at(ctx, host_dfd, s_in);
    return yos_errno_check(ctx, faccessat(host_dfd, s, mode, 0));
}

/* dup2/dup3: assign a fresh host-fd dup of oldfd into wasm-slot newfd.
 * The dup is mandatory — POSIX dup2(X, Y); close(X) must leave Y open,
 * and with per-runtime fd tables we further need each wasm fd to own
 * its own host fd so close in one runtime doesn't yank the underlying
 * file out from under the other. */
int32_t yos_dup2(struct yos_exec_ctx *ctx, int32_t oldfd, int32_t newfd)
{
    ydebug("dup2(oldfd=%d, newfd=%d) ENTRY\n", oldfd, newfd);
    int32_t host_old = yos_fd_get(ctx, oldfd);
    if (host_old < 0) {
        ydebug("dup2: bad oldfd, host_old=%d\n", host_old);
        return host_old;
    }
    if (oldfd == newfd) return newfd;  /* POSIX: no-op */
    int host_new = fcntl(host_old, F_DUPFD, 0);
    if (host_new < 0) return yos_errno_neg(ctx, errno);
    char *carry = (oldfd >= 0 && oldfd < YOS_FD_MAX && ctx->fd_paths[oldfd])
                  ? strdup(ctx->fd_paths[oldfd]) : NULL;
    ydebug("dup2(oldwfd=%d hfd=%d, newwfd=%d) -> new_hfd=%d\n",
           oldfd, host_old, newfd, host_new);
    int32_t r = yos_fd_assign(ctx, newfd, host_new);
    if (r >= 0 && carry) ctx->fd_paths[r] = carry;
    else free(carry);
    return r;
}

int32_t yos_dup3(struct yos_exec_ctx *ctx, int32_t oldfd, int32_t newfd, int32_t flags)
{
    int32_t host_old = yos_fd_get(ctx, oldfd);
    if (host_old < 0) return host_old;
    if (oldfd == newfd) return yos_errno_neg(ctx, EINVAL);  /* dup3 forbids equal fds */
    int host_new = fcntl(host_old,
                         (flags & O_CLOEXEC) ? F_DUPFD_CLOEXEC : F_DUPFD, 0);
    if (host_new < 0) return yos_errno_neg(ctx, errno);
    char *carry = (oldfd >= 0 && oldfd < YOS_FD_MAX && ctx->fd_paths[oldfd])
                  ? strdup(ctx->fd_paths[oldfd]) : NULL;
    int32_t r = yos_fd_assign(ctx, newfd, host_new);
    if (r >= 0 && carry) ctx->fd_paths[r] = carry;
    else free(carry);
    return r;
}


#include <sys/socket.h>

int32_t yos_vfs_socketpair(struct yos_exec_ctx *ctx, int32_t domain,
                           int32_t type, int32_t protocol, uint32_t sv)
{
    int *p = wptr(ctx, sv);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    int hfds[2];

    /* FreeBSD encodes SOCK_NONBLOCK / SOCK_CLOEXEC in the high bits of
     * `type` (0x20000000 / 0x10000000); Linux uses different values
     * (0x800 / 0x80000); darwin doesn't accept them at all and the
     * call EINVALs / silently masks them. Strip those bits before
     * the host call and apply via fcntl afterwards on every host
     * that doesn't natively handle them. */
    int want_nonblock = !!(type & 0x20000000);  /* FreeBSD SOCK_NONBLOCK */
    int want_cloexec  = !!(type & 0x10000000);  /* FreeBSD SOCK_CLOEXEC */
    int htype = type & ~0x30000000;
    if (socketpair(domain, htype, protocol, hfds) < 0) return yos_errno_neg(ctx, errno);
    for (int i = 0; i < 2; i++) {
        if (want_nonblock) {
            int fl = fcntl(hfds[i], F_GETFL);
            if (fl >= 0) fcntl(hfds[i], F_SETFL, fl | O_NONBLOCK);
        }
        if (want_cloexec) {
            int fl = fcntl(hfds[i], F_GETFD);
            if (fl >= 0) fcntl(hfds[i], F_SETFD, fl | FD_CLOEXEC);
        }
    }
    int32_t a = yos_fd_alloc(ctx, hfds[0]);
    if (a < 0) { yos_plat_close(hfds[1]); return a; }
    int32_t b = yos_fd_alloc(ctx, hfds[1]);
    if (b < 0) { yos_fd_close(ctx, a); return b; }
    p[0] = a;
    p[1] = b;
    return 0;
}

int32_t yos_preadv(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, vlen, iov);
    if (r) return r;
    off_t offset = ((off_t)pos_h << 32) | pos_l;
    ssize_t n = preadv(hfd, iov, vlen, offset);
    return yos_errno_check(ctx, (int32_t)n);
}

int32_t yos_pwritev(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, vlen, iov);
    if (r) return r;
    off_t offset = ((off_t)pos_h << 32) | pos_l;
    ssize_t n = pwritev(hfd, iov, vlen, offset);
    return yos_errno_check(ctx, (int32_t)n);
}
