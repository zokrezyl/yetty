/* impl/fifo.c — userspace FIFO emulation for sandboxes that forbid
 * mkfifo(2).
 *
 * The wasm guest (runit's runsv, anything that uses POSIX FIFOs)
 * calls mkfifo(path, mode) and expects subsequent open(path, ...)
 * calls to behave as a named pipe: one end reads, the other writes,
 * data flows. tvOS (and iOS) sandboxes reject host mkfifo(2) with
 * EPERM, so the auto-bridge that just forwards to host mkfifo fails
 * and runsv aborts at startup.
 *
 * Emulation strategy: keep a host-side registry path → host pipe(2)
 * fds. yos_mkfifo creates a zero-length placeholder file at path
 * (so subsequent stat() / access() / readlink() at that path see
 * SOMETHING) then pipe2() to get a pair of host fds, parks both in
 * the registry under that path. yos_open consults the registry
 * first: if the requested path is a registered FIFO, return a
 * dup() of the read-end or write-end depending on access mode.
 * yos_unlink drops the registry entry and closes the held pipe fds.
 *
 * Limitations vs. a real FIFO:
 *   - Only ONE underlying read end and ONE write end exist; opens
 *     return dup()s so multiple readers / multiple writers each
 *     work for non-blocking IO, but they all share the same
 *     kernel-side pipe buffer.
 *   - open() never blocks (real FIFO open(O_RDONLY) blocks until a
 *     writer arrives). runsv doesn't depend on that semantic —
 *     it opens the read end itself on startup and uses select(2)
 *     to multiplex. Programs that DO rely on open-blocking will
 *     see a permanent "ready" instead.
 *   - lseek() is meaningless on pipes; same as a real FIFO.
 *
 * Concurrency: yos has multiple host pthreads (one per wasm proc).
 * A single mutex serialises registry mutation; the per-fd IO uses
 * the kernel-level pipe synchronization.
 *
 * Fork: yos_fd_fork_dup F_DUPFD-clones every fd in the parent's
 * fd_map for the child. That includes whichever FIFO-dup the parent
 * had open; the child gets a separate host fd pointing at the same
 * kernel pipe. The registry itself lives in shared host memory so
 * the child sees the SAME path → fds mapping the parent did.
 */

#include "yos/types.h"
#include "impl/errno_helpers.h"
#include <yos/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>       /* snprintf */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern const char *yos_path_resolve(struct yos_exec_ctx *ctx, const char *p);

struct fifo_entry {
    char *path;     /* resolved absolute path */
    int   rfd;      /* read end held internally */
    int   wfd;      /* write end held internally */
    struct fifo_entry *next;
};

/* g_fifo_lock and g_fifo_head removed; list head now lives on
 * ctx->fifo_head (types.h). No process-wide mutex needed because
 * each ctx is the sole accessor of its own list. */

static struct fifo_entry *fifo_lookup_locked(struct yos_exec_ctx *ctx, const char *path) {
    for (struct fifo_entry *e = (struct fifo_entry *)ctx->fifo_head; e; e = e->next)
        if (strcmp(e->path, path) == 0) return e;
    return NULL;
}

/* Public: called by the bridge's yos_open before the host open(2).
 * Returns -1 with errno=0 when path is NOT a registered FIFO (caller
 * falls through to host open). On hit, returns a host fd (dup of the
 * appropriate internal end) and the caller wraps it in yos_fd_alloc. */
int yos_fifo_try_open(struct yos_exec_ctx *ctx, const char *path, int flags) {
    struct fifo_entry *e = fifo_lookup_locked(ctx, path);
    if (!e) { errno = 0; return -1; }

    int access = flags & O_ACCMODE;
    int src;
    if (access == O_WRONLY) {
        src = e->wfd;
    } else if (access == O_RDONLY) {
        src = e->rfd;
    } else {
        /* O_RDWR on a FIFO: a real kernel returns a bidirectional fd
         * (the same fd reads and writes). pipe(2) ends are uni-
         * directional, so we can't emulate exactly. runit only uses
         * O_RDONLY / O_WRONLY, so this matters in theory only. Hand
         * back the write end and let any reader on the same fd see
         * EBADF. */
        src = e->wfd;
    }
    int dupcmd = (flags & O_CLOEXEC) ? F_DUPFD_CLOEXEC : F_DUPFD;
    int dup = fcntl(src, dupcmd, 0);
    if (dup < 0) return -1;
    /* Real FIFO open() ignores O_TRUNC / O_APPEND / O_CREAT — we do too. */
    return dup;
}

/* Public: bridge for the wasm guest's mkfifo(path, mode). Returns
 * -errno on failure (matches the rest of yos's vfs convention; the
 * codegen wrapper translates that to the wasm -1 + errno result). */
int32_t yos_mkfifo(struct yos_exec_ctx *ctx, uint32_t path_off, uint16_t mode)
{
    const char *raw = (path_off && path_off < ctx->memory_size)
                      ? (const char *)(ctx->memory + path_off) : NULL;
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *path = yos_path_resolve(ctx, raw);
    /* Real POSIX mkfifo on an existing path returns EEXIST. But on
     * respawn / restart loops we'd otherwise leak the old pipe pair
     * forever and fail every subsequent open() on stale registry. So
     * if the entry already exists, tear it down and re-create. */
    {
        struct fifo_entry **link = (struct fifo_entry **)&ctx->fifo_head;
        while (*link) {
            if (strcmp((*link)->path, path) == 0) {
                struct fifo_entry *old = *link;
                *link = old->next;
                close(old->rfd); close(old->wfd);
                free(old->path); free(old);
                break;
            }
            link = &(*link)->next;
        }
    }
    /* Drop a stale placeholder file too — the previous run's regular
     * file would otherwise EEXIST below. */
    (void)unlink(path);

    /* Marker file so stat()/access() at the path see something. We
     * don't carry FIFO semantics in the filesystem layer, just the
     * placeholder. Mode bits applied where the kernel allows; the
     * per-ctx umask is masked in software since host umask is 0. */
    int marker = open(path, O_WRONLY | O_CREAT | O_EXCL,
                      (mode_t)((mode & 0777) & ~ctx->umask));
    if (marker < 0) {
        int saved = errno;
        return yos_errno_neg(ctx, saved);
    }
    close(marker);

    int p[2];
    if (pipe(p) < 0) {
        int saved = errno;
        unlink(path);
        return yos_errno_neg(ctx, saved);
    }

    struct fifo_entry *e = (struct fifo_entry *)malloc(sizeof *e);
    if (!e) {
        close(p[0]); close(p[1]); unlink(path);
        return yos_errno_neg(ctx, ENOMEM);
    }
    e->path = strdup(path);
    e->rfd  = p[0];
    e->wfd  = p[1];
    e->next = (struct fifo_entry *)ctx->fifo_head;
    ctx->fifo_head = e;

    ydebug("fifo: registered %s (rfd=%d wfd=%d)\n", path, e->rfd, e->wfd);
    return 0;
}

/* Public: bridge for wasm mkfifoat(dfd, path, mode). dfd is yos's
 * virtual descriptor for the at-relative directory; we resolve via
 * the host fchdir / openat pattern used elsewhere in vfs.c. The
 * simplest thing that works correctly: compose an absolute path
 * from dfd's cwd-equivalent + path and forward to yos_mkfifo. */
extern int yos_xlate_dfd(struct yos_exec_ctx *ctx, int32_t dfd);
int32_t yos_mkfifoat(struct yos_exec_ctx *ctx, int32_t dfd,
                     uint32_t path_off, uint16_t mode)
{
    const char *raw = (path_off && path_off < ctx->memory_size)
                      ? (const char *)(ctx->memory + path_off) : NULL;
    if (!raw) return yos_errno_neg(ctx, EFAULT);

    /* Absolute path → ignore dfd, defer to plain mkfifo. */
    if (raw[0] == '/') return yos_mkfifo(ctx, path_off, mode);

    /* Relative: build an absolute path so the registry stores
     * something consistent. For AT_FDCWD, getcwd() directly. For a
     * real dir fd, fchdir-getcwd-restore. NB: fchdir(AT_FDCWD) is
     * EBADF — never call it. */
    int hdfd = yos_xlate_dfd(ctx, dfd);
    char abs[4096];
    char dir[4096];
    if (hdfd == AT_FDCWD) {
        if (!getcwd(dir, sizeof dir))
            return yos_errno_neg(ctx, errno);
    } else {
        int saved_cwd = open(".", O_RDONLY | O_CLOEXEC);
        if (fchdir(hdfd) < 0) {
            if (saved_cwd >= 0) close(saved_cwd);
            return yos_errno_neg(ctx, errno);
        }
        if (!getcwd(dir, sizeof dir)) {
            int saved = errno;
            if (saved_cwd >= 0) { (void)fchdir(saved_cwd); close(saved_cwd); }
            return yos_errno_neg(ctx, saved);
        }
        if (saved_cwd >= 0) { (void)fchdir(saved_cwd); close(saved_cwd); }
    }
    snprintf(abs, sizeof abs, "%s/%s", dir, raw);

    /* Re-enter yos_mkfifo with an absolute path that lands directly
     * in the global registry. We forge a temporary wasm-offset by
     * writing the absolute path into a stack scratch and pointing at
     * ctx->memory — but that's ugly. Cleaner: lift the registry
     * logic into a helper that takes the resolved path directly. */
    extern int32_t yos_mkfifo_path(struct yos_exec_ctx *ctx,
                                   const char *path, uint32_t mode);
    return yos_mkfifo_path(ctx, abs, mode);
}

/* Helper variant of yos_mkfifo that takes a resolved absolute path
 * directly — used by mkfifoat after dfd resolution. */
int32_t yos_mkfifo_path(struct yos_exec_ctx *ctx, const char *path,
                        uint32_t mode)
{
    {
        struct fifo_entry **link = (struct fifo_entry **)&ctx->fifo_head;
        while (*link) {
            if (strcmp((*link)->path, path) == 0) {
                struct fifo_entry *old = *link;
                *link = old->next;
                close(old->rfd); close(old->wfd);
                free(old->path); free(old);
                break;
            }
            link = &(*link)->next;
        }
    }
    (void)unlink(path);
    int marker = open(path, O_WRONLY | O_CREAT | O_EXCL, (mode_t)(mode & 0777));
    if (marker < 0) {
        int saved = errno;
        return yos_errno_neg(ctx, saved);
    }
    close(marker);
    int p[2];
    if (pipe(p) < 0) {
        int saved = errno;
        unlink(path);
        return yos_errno_neg(ctx, saved);
    }
    struct fifo_entry *e = (struct fifo_entry *)malloc(sizeof *e);
    if (!e) {
        close(p[0]); close(p[1]); unlink(path);
        return yos_errno_neg(ctx, ENOMEM);
    }
    e->path = strdup(path);
    e->rfd  = p[0];
    e->wfd  = p[1];
    e->next = (struct fifo_entry *)ctx->fifo_head;
    ctx->fifo_head = e;
    ydebug("fifo: registered %s (rfd=%d wfd=%d)\n", path, e->rfd, e->wfd);
    return 0;
}

/* Called from yos_stat / yos_lstat / yos_fstatat after the host stat
 * to overlay S_IFIFO mode bits on registry-matched paths. Without
 * this, runsv-style code does `stat(path); if ((mode & S_IFMT) !=
 * S_IFIFO) bail` and our pipe-emulated FIFO is rejected. Returns 1
 * if the path was rewritten, 0 if unknown. */
int yos_fifo_stat_fixup(struct yos_exec_ctx *ctx, const char *path, mode_t *st_mode_out)
{
    if (!path || !st_mode_out) return 0;
    struct fifo_entry *e = fifo_lookup_locked(ctx, path);
    if (e) {
        *st_mode_out = (*st_mode_out & ~S_IFMT) | S_IFIFO;
        return 1;
    }
    return 0;
}

/* Called from yos_unlink in vfs.c right after the host unlink, so a
 * stale entry doesn't linger forever even if the program drops the
 * placeholder file. Returns 1 if path WAS a FIFO (caller can ignore
 * any host unlink errno), 0 otherwise. */
int yos_fifo_drop(struct yos_exec_ctx *ctx, const char *path)
{
    struct fifo_entry **link = (struct fifo_entry **)&ctx->fifo_head;
    while (*link) {
        if (strcmp((*link)->path, path) == 0) {
            struct fifo_entry *e = *link;
            *link = e->next;
            close(e->rfd);
            close(e->wfd);
            free(e->path);
            free(e);
            return 1;
        }
        link = &(*link)->next;
    }
    return 0;
}

/* ── stat-family overrides ─────────────────────────────────────────
 *
 * The auto-bridge for stat / lstat / fstatat just forwards to host
 * stat(2), which reports our placeholder file as S_IFREG. runsv
 * does `stat(path); if (!S_ISFIFO(mode)) bail`. Provide hand-written
 * versions that overlay S_IFIFO on registry-matched paths, then run
 * the same host64 → wasm32 converter the auto-bridge does.
 *
 * Layered as custom_vfs in hooks.yaml so codegen skips its own
 * yos_stat body and routes the m3w_stat raw wrapper to ours.
 */

#include "impl/io/cv_stat.h"
extern int  yos_xlate_dfd(struct yos_exec_ctx *ctx, int32_t dfd);

int32_t yos_stat(struct yos_exec_ctx *ctx, uint32_t path_off,
                 uint32_t statbuf_off)
{
    const char *raw = (path_off && path_off < ctx->memory_size)
                      ? (const char *)(ctx->memory + path_off) : NULL;
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *path = yos_path_resolve(ctx, raw);

    struct stat host_st;
    memset(&host_st, 0, sizeof host_st);
    errno = 0;
    int rc = stat(path, &host_st);
    if (rc < 0) return yos_errno_neg(ctx, errno);

    (void)yos_fifo_stat_fixup(ctx, path, &host_st.st_mode);

    if (statbuf_off && statbuf_off < ctx->memory_size)
        yos_cv_stat_fbi((uint8_t *)(ctx->memory + statbuf_off), &host_st);
    return 0;
}

int32_t yos_lstat(struct yos_exec_ctx *ctx, uint32_t path_off,
                  uint32_t statbuf_off)
{
    const char *raw = (path_off && path_off < ctx->memory_size)
                      ? (const char *)(ctx->memory + path_off) : NULL;
    if (!raw) return yos_errno_neg(ctx, EFAULT);
    const char *path = yos_path_resolve(ctx, raw);

    struct stat host_st;
    memset(&host_st, 0, sizeof host_st);
    errno = 0;
    int rc = lstat(path, &host_st);
    if (rc < 0) return yos_errno_neg(ctx, errno);

    (void)yos_fifo_stat_fixup(ctx, path, &host_st.st_mode);

    if (statbuf_off && statbuf_off < ctx->memory_size)
        yos_cv_stat_fbi((uint8_t *)(ctx->memory + statbuf_off), &host_st);
    return 0;
}

int32_t yos_fstatat(struct yos_exec_ctx *ctx, int32_t dfd,
                    uint32_t path_off, uint32_t statbuf_off,
                    int32_t flags)
{
    const char *raw = (path_off && path_off < ctx->memory_size)
                      ? (const char *)(ctx->memory + path_off) : NULL;
    if (!raw) return yos_errno_neg(ctx, EFAULT);

    int hdfd = yos_xlate_dfd(ctx, dfd);
    const char *path = yos_path_resolve(ctx, raw);

    /* AT_SYMLINK_NOFOLLOW (and AT_EMPTY_PATH etc.) have DIFFERENT
     * numeric values on FreeBSD vs the host. fstatat(host, flags)
     * silently does the wrong thing — `lstat` semantics collapse to
     * `stat` if AT_SYMLINK_NOFOLLOW=0x200 isn't translated to the
     * host's bit. Run flags through the translator before the host
     * syscall. */
    extern int yos_at_flags_fb_to_lx(int f);
    int hflags = yos_at_flags_fb_to_lx(flags);

    struct stat host_st;
    memset(&host_st, 0, sizeof host_st);
    errno = 0;
    int rc = fstatat(hdfd, path, &host_st, hflags);
    if (rc < 0) return yos_errno_neg(ctx, errno);

    /* FIFO registry overlay. The registry stores absolute paths, so a
     * dfd-relative call only hits if the caller happened to pass an
     * already-absolute path. That's the runsv case; for other relative
     * lookups the regular stat result stands, which is correct on hosts
     * where mkfifo(2) works natively (the placeholder file isn't there
     * — the real FIFO is). */
    (void)yos_fifo_stat_fixup(ctx, path, &host_st.st_mode);

    if (statbuf_off && statbuf_off < ctx->memory_size)
        yos_cv_stat_fbi((uint8_t *)(ctx->memory + statbuf_off), &host_st);
    return 0;
}
