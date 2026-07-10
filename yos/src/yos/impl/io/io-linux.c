/* impl/io/io-linux.c — Linux-only POSIX I/O bridges.
 *
 * Contains:
 *   - yos_pipe2          (uses Linux's pipe2(2))
 *   - ioctl_cmd_fb_to_lx (FreeBSD ↔ Linux ioctl number remap table)
 *   - yos_vfs_preadv2 / pwritev2 / renameat2 (Linux-only)
 *   - yos_vfs_statx     (Linux-only — uses kernel statx syscall path
 *                         inherited from yos-private; flagged for
 *                         migration to libc fstatx when one exists)
 *   - yos_vfs_sendfile / sendfile64 (Linux sendfile semantics)
 *   - timerfd / mempolicy / mbind / migrate_pages / execveat /
 *     get_robust_list / io_setup,destroy,submit,cancel / futex
 *     (all Linux-native; darwin returns ENOSYS via io-darwin.c)
 *
 * NO #ifdef inside this file — meson selects it only on linux hosts.
 * darwin hosts use io-darwin.c (ENOSYS stubs for the Linux-only set
 * + a pipe(2)+fcntl fallback for yos_pipe2).
 */

#define _GNU_SOURCE
#include "yos/types.h"
#include "impl/io/io-internal.h"
#include "impl/errno_helpers.h"
#include "host64_structs.h"
#include "struct_convert.h"
#include "vfs/mount.h"
#include "vfs/file.h"
#include <yos/ytrace/ytrace.h>

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>     /* snprintf */
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <linux/stat.h>
#include <linux/time_types.h>

/* ── yos_pipe2 ────────────────────────────────────────────────────── */
int32_t yos_pipe2(struct yos_exec_ctx *ctx, uint32_t fildes, int32_t flags)
{
    int *p = wptr(ctx, fildes);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    int hfds[2];
    /* FreeBSD vs Linux flag remap. FreeBSD: O_CLOEXEC=0x00100000,
     * O_NONBLOCK=0x00000004. Linux: O_CLOEXEC=0x00080000,
     * O_NONBLOCK=0x00000800. */
    int hflags = 0;
    if (flags & 0x00100000) hflags |= 0x00080000;
    if (flags & 0x00000004) hflags |= 0x00000800;
    int leftover = flags & ~(0x00100000 | 0x00000004);
    if (leftover) {
        ydebug("yos_pipe2: untranslated flag bits 0x%x (passed through)\n",
               leftover);
        hflags |= leftover;
    }
    if (pipe2(hfds, hflags) < 0) {
        ydebug("yos_pipe2(flags=0x%x->0x%x) host failed: %s\n",
               flags, hflags, strerror(errno));
        return yos_errno_neg(ctx, errno);
    }
    extern int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);
    extern int32_t yos_fd_close(struct yos_exec_ctx *ctx, int32_t wfd);
    int32_t r = yos_fd_alloc(ctx, hfds[0]);
    if (r < 0) { close(hfds[1]); return r; }
    int32_t w = yos_fd_alloc(ctx, hfds[1]);
    if (w < 0) { yos_fd_close(ctx, r); return w; }
    p[0] = r;
    p[1] = w;
    return 0;
}

/* ── ioctl_cmd_fb_to_lx (Linux table) ─────────────────────────────── */
#define FB_TIOCGWINSZ   0x40087468u
#define FB_TIOCSWINSZ   0x80087467u
#define FB_TIOCGPGRP    0x40047477u
#define FB_TIOCSPGRP    0x80047476u
#define FB_TIOCSCTTY    0x20007461u
#define FB_TIOCNOTTY    0x20007471u
#define FB_FIONREAD     0x4004667fu
#define FB_FIONBIO      0x8004667eu
#define FB_FIOCLEX      0x20006601u
#define FB_FIONCLEX     0x20006602u
#define FB_FIOASYNC     0x8004667du
#define FB_TIOCPKT      0x80047470u
#define LX_TIOCGWINSZ   0x5413u
#define LX_TIOCSWINSZ   0x5414u
#define LX_TIOCGPGRP    0x540fu
#define LX_TIOCSPGRP    0x5410u
#define LX_TIOCSCTTY    0x540eu
#define LX_TIOCNOTTY    0x5422u
#define LX_FIONREAD     0x541bu
#define LX_FIONBIO      0x5421u
#define LX_FIOCLEX      0x5451u
#define LX_FIONCLEX     0x5450u
#define LX_FIOASYNC     0x5452u
#define LX_TIOCPKT      0x5420u

uint32_t ioctl_cmd_fb_to_lx(uint32_t cmd)
{
    switch (cmd) {
    case FB_TIOCGWINSZ: return LX_TIOCGWINSZ;
    case FB_TIOCSWINSZ: return LX_TIOCSWINSZ;
    case FB_TIOCGPGRP:  return LX_TIOCGPGRP;
    case FB_TIOCSPGRP:  return LX_TIOCSPGRP;
    case FB_TIOCSCTTY:  return LX_TIOCSCTTY;
    case FB_TIOCNOTTY:  return LX_TIOCNOTTY;
    case FB_FIONREAD:   return LX_FIONREAD;
    case FB_FIONBIO:    return LX_FIONBIO;
    case FB_FIOCLEX:    return LX_FIOCLEX;
    case FB_FIONCLEX:   return LX_FIONCLEX;
    case FB_FIOASYNC:   return LX_FIOASYNC;
    case FB_TIOCPKT:    return LX_TIOCPKT;
    default:            return cmd;
    }
}

/* preadv2/pwritev2 add a `flags` arg (RWF_HIPRI / RWF_DSYNC / RWF_SYNC etc.)
 * — passed straight through to the kernel. */
int32_t yos_vfs_preadv2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec,
                        int32_t vlen, uint32_t pos_l, uint32_t pos_h,
                        int32_t flags)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, vlen, iov);
    if (r) return r;
    off_t offset = ((off_t)pos_h << 32) | pos_l;
    ssize_t n = preadv2(hfd, iov, vlen, offset, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

int32_t yos_vfs_pwritev2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec,
                         int32_t vlen, uint32_t pos_l, uint32_t pos_h,
                         int32_t flags)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (vlen < 0 || vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, vlen, iov);
    if (r) return r;
    off_t offset = ((off_t)pos_h << 32) | pos_l;
    ssize_t n = pwritev2(hfd, iov, vlen, offset, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

/* vmsplice — pipe fd + iovec[]. Fewer callers than preadv2 but the kernel
 * accepts it through the same iovec ABI. */
int32_t yos_vfs_vmsplice(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec,
                         uint32_t vlen, uint32_t flags)
{
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, (int)vlen, iov);
    if (r) return r;
    ssize_t n = vmsplice(hfd, iov, vlen, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

/* process_madvise — pidfd + iovec[] of memory ranges. Need <sys/mman.h>
 * for MADV_*; the syscall itself is what we wrap. */
int32_t yos_vfs_process_madvise(struct yos_exec_ctx *ctx, int32_t pidfd,
                                uint32_t vec, uint32_t vlen,
                                int32_t behavior, uint32_t flags)
{
    int32_t hfd = yos_fd_get(ctx, pidfd);
    if (hfd < 0) return hfd;
    if (vlen > YOS_IOV_MAX) return yos_errno_neg(ctx, EINVAL);
    struct iovec iov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, vec, (int)vlen, iov);
    if (r) return r;
    /* Use raw syscall — process_madvise has no glibc wrapper everywhere. */
    long n = syscall(SYS_process_madvise, hfd, iov, (unsigned long)vlen,
                     behavior, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

/* process_vm_readv / process_vm_writev — *cross-process* iovec moves.
 * The remote_iov array describes addresses in another process; passing
 * those addresses straight to the kernel is what's intended (it does NOT
 * touch our wasm memory for those). Only the *local* iovec needs
 * translation. */
int32_t yos_vfs_process_vm_readv(struct yos_exec_ctx *ctx, int32_t pid,
                                 uint32_t lvec, uint32_t liovcnt,
                                 uint32_t rvec, uint32_t riovcnt,
                                 uint32_t flags)
{
    if (liovcnt > YOS_IOV_MAX || riovcnt > YOS_IOV_MAX)
        return yos_errno_neg(ctx, EINVAL);
    struct iovec liov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, lvec, (int)liovcnt, liov);
    if (r) return r;
    /* Remote iovec: addresses are in the OTHER process — we still have to
     * convert the wasm32 layout to host layout, but the bases stay raw
     * because they're not pointers into our memory. */
    struct iovec riov[YOS_IOV_MAX];
    if (riovcnt) {
        uint8_t *p = wptr_range(ctx, rvec, (uint64_t)riovcnt * 8u);
        if (!p) return yos_errno_neg(ctx, EFAULT);
        for (uint32_t i = 0; i < riovcnt; i++) {
            uint32_t base = *(uint32_t *)(p + i * 8);
            uint32_t len  = *(uint32_t *)(p + i * 8 + 4);
            riov[i].iov_base = (void *)(uintptr_t)base;
            riov[i].iov_len = len;
        }
    }
    long n = syscall(SYS_process_vm_readv, pid, liov, (unsigned long)liovcnt,
                     riov, (unsigned long)riovcnt, (unsigned long)flags);
    return yos_errno_check(ctx, (int32_t)n);
}

int32_t yos_vfs_process_vm_writev(struct yos_exec_ctx *ctx, int32_t pid,
                                  uint32_t lvec, uint32_t liovcnt,
                                  uint32_t rvec, uint32_t riovcnt,
                                  uint32_t flags)
{
    if (liovcnt > YOS_IOV_MAX || riovcnt > YOS_IOV_MAX)
        return yos_errno_neg(ctx, EINVAL);
    struct iovec liov[YOS_IOV_MAX];
    int r = yos_iovec_w32_to_host(ctx, lvec, (int)liovcnt, liov);
    if (r) return r;
    struct iovec riov[YOS_IOV_MAX];
    if (riovcnt) {
        uint8_t *p = wptr_range(ctx, rvec, (uint64_t)riovcnt * 8u);
        if (!p) return yos_errno_neg(ctx, EFAULT);
        for (uint32_t i = 0; i < riovcnt; i++) {
            uint32_t base = *(uint32_t *)(p + i * 8);
            uint32_t len  = *(uint32_t *)(p + i * 8 + 4);
            riov[i].iov_base = (void *)(uintptr_t)base;
            riov[i].iov_len = len;
        }
    }
    long n = syscall(SYS_process_vm_writev, pid, liov, (unsigned long)liovcnt,
                     riov, (unsigned long)riovcnt, (unsigned long)flags);
    return yos_errno_check(ctx, (int32_t)n);
}

int32_t yos_vfs_getdents(struct yos_exec_ctx *ctx, int32_t fd, uint32_t dirent, uint32_t count)
{
    /* Validate the FULL [dirent, dirent+count) range — the kernel
     * writes up to `count` bytes of dirent records into the buffer. */
    void *p = wptr_range(ctx, dirent, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    long r = syscall(SYS_getdents, hfd, p, count);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_vfs_getdents64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t dirent, uint32_t count)
{
    void *p = wptr_range(ctx, dirent, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);

    /* Check if virtual fd */
    if (yos_is_virtual_fd(fd)) {
        struct yos_file_table *ft = (struct yos_file_table *)ctx->procfs_fds;
        if (!ft) return yos_errno_neg(ctx, EBADF);
        struct yos_file *file = yos_file_get(ft, fd);
        if (!file) return yos_errno_neg(ctx, EBADF);
        if (file->ops && file->ops->getdents64)
            return file->ops->getdents64(ctx, file, p, count);
        return yos_errno_neg(ctx, EBADF);
    }

    int32_t hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    long r = syscall(SYS_getdents64, hfd, p, count);
    return yos_errno_check(ctx, (int32_t)r);
}
/*
 * statx - extended stat with mount table support
 * Kernel ABI constants for mode bits
 */
#define YOS_S_IFDIR  0040000
#define YOS_S_IFREG  0100000
#define YOS_S_IFLNK  0120000

int32_t yos_vfs_statx(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname,
                      int32_t flags, uint32_t mask, uint32_t buffer)
{
    const char *path = wstr_check(ctx, pathname);
    if (!path) return yos_errno_neg(ctx, EFAULT);

    uint8_t *buf = wptr(ctx, buffer);
    if (!buf) return yos_errno_neg(ctx, EFAULT);

    ydebug("yos_vfs_statx: path=%s\n", path);

    /* Check if path is in a virtual filesystem */
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (mt) {
        const char *remaining;
        const struct yos_file_operations *ops = yos_mount_resolve(mt, path, &remaining);
        if (ops && ops->stat) {
            /*
             * Virtual filesystem - generate statx directly
             * We call the stat op which fills a stat64 buffer, then we
             * need to convert key fields to statx format
             */
            uint8_t stat64_buf[96];
            memset(stat64_buf, 0, sizeof(stat64_buf));
            int32_t ret = ops->stat(ctx, remaining, stat64_buf);
            if (ret < 0) return ret;

            /* Convert stat64 to statx format
             * stat64 layout: mode at +16, nlink at +20
             * statx layout (wasm32_statx):
             *   +0: stx_mask (uint32)
             *   +4: stx_blksize (uint32)
             *   +8: stx_attributes (uint64)
             *   +16: stx_nlink (uint32)
             *   +20: stx_uid (uint32)
             *   +24: stx_gid (uint32)
             *   +28: stx_mode (uint16)
             */
            memset(buf, 0, 256);
            uint32_t mode = *(uint32_t *)(stat64_buf + 16);
            uint32_t nlink = *(uint32_t *)(stat64_buf + 20);

            *(uint32_t *)(buf + 0) = 0x7ff;  /* stx_mask - basic stats */
            *(uint32_t *)(buf + 4) = 4096;   /* stx_blksize */
            *(uint32_t *)(buf + 16) = nlink; /* stx_nlink */
            *(uint16_t *)(buf + 28) = (uint16_t)mode; /* stx_mode */
            *(uint64_t *)(buf + 32) = 1;     /* stx_ino */
            return 0;
        }
    }

    /* Pass through to host statx, translating dfd through host_stdio[]
     * so AT_EMPTY_PATH lookups against wasm fd 0/1/2 hit the right host
     * fd in the parent. AT_FDCWD is special and stays as-is. */
    int host_dfd = (dfd == -100 /* AT_FDCWD */) ? dfd : host_fd(ctx, dfd);
    struct statx host_statx;
    long ret = syscall(__NR_statx, host_dfd, path, flags, mask, &host_statx);
    if (ret < 0) return yos_errno_neg(ctx, errno);

    /* Convert host statx to wasm32 statx - same layout, direct copy */
    memcpy(buf, &host_statx, sizeof(host_statx));
    return 0;
}

/* ============================================================================
 * sendfile / sendfile64 — host's `off_t *offset` is 8 bytes; wasm32's
 * old-style sendfile takes a `long *offset` (4 bytes) and the sendfile64
 * variant takes `loff_t *offset` (already 8). For sendfile (the i386
 * "long *" variant) we read a 32-bit value, run the host syscall with a
 * scratch off_t, and write back with overflow detection.
 * ========================================================================= */

#  include <sys/sendfile.h>

int32_t yos_vfs_sendfile(struct yos_exec_ctx *ctx, int32_t out_fd, int32_t in_fd, uint32_t offset_ptr, uint32_t count)
{
    int32_t hout = yos_fd_get(ctx, out_fd);
    if (hout < 0) return hout;
    int32_t hin = yos_fd_get(ctx, in_fd);
    if (hin < 0) return hin;

    off_t off_val = 0;
    off_t *off_arg = NULL;
    int32_t *wasm_off = NULL;
    if (offset_ptr) {
        wasm_off = wptr(ctx, offset_ptr);
        if (!wasm_off) return yos_errno_neg(ctx, EFAULT);
        off_val = (off_t)(int32_t)*wasm_off;  /* sign-extend 4→8 */
        off_arg = &off_val;
    }

    ssize_t r = sendfile(hout, hin, off_arg, count);
    if (r < 0) return yos_errno_neg(ctx, errno);
    if (wasm_off) {
        if (off_val > 0x7fffffffLL || off_val < -0x80000000LL) return yos_errno_neg(ctx, EOVERFLOW);
        *wasm_off = (int32_t)off_val;
    }
    return (int32_t)r;
}

int32_t yos_vfs_sendfile64(struct yos_exec_ctx *ctx, int32_t out_fd, int32_t in_fd, uint32_t offset_ptr, uint32_t count)
{
    /* sendfile64's offset is already loff_t (8 bytes) on both sides;
     * the only thing that differs is the wasm pointer. */
    int32_t hout = yos_fd_get(ctx, out_fd);
    if (hout < 0) return hout;
    int32_t hin = yos_fd_get(ctx, in_fd);
    if (hin < 0) return hin;
    off_t *off_arg = NULL;
    if (offset_ptr) {
        off_arg = (off_t *)wptr(ctx, offset_ptr);
        if (!off_arg) return yos_errno_neg(ctx, EFAULT);
    }
    ssize_t r = sendfile(hout, hin, off_arg, count);
    return yos_errno_check(ctx, (int32_t)r);
}

/* ============================================================================
 * POSIX timer_t mapping. The host kernel hands back a `void *` timer_t
 * (8 bytes). wasm32 expects 4 bytes. We keep a per-runtime table that
 * maps wasm-side int32 IDs to host timer_t values; the wasm caller
 * sees a small handle and we look up the real one before each kernel
 * call. Capacity is fixed (rare for one process to have hundreds of
 * timers) — overflow returns EAGAIN.
 * ========================================================================= */

#include <time.h>
#include <signal.h>
#include <pthread.h>

/* Per-ctx timer-id table helpers. The table itself lives in the exec
 * context (ctx->timer_ids[], see types.h); these functions just
 * lock/look-up/free a slot. A global table would let one wasm process
 * delete or read timers another process owns — moving it per-ctx is
 * what the docstring promised but the code didn't implement. */
static void timer_table_init(struct yos_exec_ctx *ctx)
{
    if (ctx->timer_lock_init) return;
    pthread_mutex_init(&ctx->timer_lock, NULL);
    ctx->timer_lock_init = 1;
}

static int timer_table_alloc(struct yos_exec_ctx *ctx, timer_t host_id)
{
    timer_table_init(ctx);
    pthread_mutex_lock(&ctx->timer_lock);
    for (int i = 0; i < YOS_TIMER_MAX; i++) {
        if (!ctx->timer_ids[i]) {
            ctx->timer_ids[i] = host_id;
            pthread_mutex_unlock(&ctx->timer_lock);
            return i + 1;  /* 0 reserved as 'free' */
        }
    }
    pthread_mutex_unlock(&ctx->timer_lock);
    return -1;
}

static timer_t timer_table_get(struct yos_exec_ctx *ctx, int wasm_id)
{
    if (wasm_id < 1 || wasm_id > YOS_TIMER_MAX) return NULL;
    timer_table_init(ctx);
    return ctx->timer_ids[wasm_id - 1];
}

static void timer_table_free(struct yos_exec_ctx *ctx, int wasm_id)
{
    if (wasm_id < 1 || wasm_id > YOS_TIMER_MAX) return;
    timer_table_init(ctx);
    pthread_mutex_lock(&ctx->timer_lock);
    ctx->timer_ids[wasm_id - 1] = NULL;
    pthread_mutex_unlock(&ctx->timer_lock);
}

/* timer_create writes the new ID through `timerid_out`. wasm-side
 * timer_t is 4 bytes; we always store our int32 handle there. The
 * sevp arg (struct sigevent) is converted via the auto-generated
 * sigevent_wasm32_to_host (declared in struct_convert.h). */
int32_t yos_vfs_timer_create(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t sevp, uint32_t timerid_out)
{
    timer_t host_id = NULL;
    int rc;
    if (sevp == 0) {
        rc = timer_create(clockid, NULL, &host_id);
    } else {
        const void *sevp_p = wptr_range(ctx, sevp, sizeof(struct wasm32_sigevent));
        if (!sevp_p) return yos_errno_neg(ctx, EFAULT);
        struct host64_sigevent host_sev;
        sigevent_wasm32_to_host(
            (const struct wasm32_sigevent *)sevp_p,
            &host_sev);
        rc = timer_create(clockid, (struct sigevent *)&host_sev, &host_id);
    }
    if (rc < 0) return yos_errno_neg(ctx, errno);
    int wid = timer_table_alloc(ctx, host_id);
    if (wid < 0) { timer_delete(host_id); return yos_errno_neg(ctx, EAGAIN); }
    int32_t *out = wptr_range(ctx, timerid_out, sizeof(int32_t));
    if (!out) { timer_delete(host_id); timer_table_free(ctx, wid); return yos_errno_neg(ctx, EFAULT); }
    *out = wid;
    return 0;
}

int32_t yos_vfs_timer_settime(struct yos_exec_ctx *ctx, int32_t timerid, int32_t flags, uint32_t new_value, uint32_t old_value)
{
    timer_t hid = timer_table_get(ctx, timerid);
    if (!hid) return yos_errno_neg(ctx, EINVAL);
    struct host64___kernel_itimerspec host_new, host_old;
    if (!new_value) return yos_errno_neg(ctx, EFAULT);
    void *wp = wptr_range(ctx, new_value, sizeof(struct wasm32___kernel_itimerspec));
    if (!wp) return yos_errno_neg(ctx, EFAULT);
    void *wo = NULL;
    if (old_value) {
        wo = wptr_range(ctx, old_value, sizeof(struct wasm32___kernel_itimerspec));
        if (!wo) return yos_errno_neg(ctx, EFAULT);
    }
    __kernel_itimerspec_wasm32_to_host(
        (const struct wasm32___kernel_itimerspec *)wp, &host_new);
    int rc = timer_settime(hid, flags, (struct itimerspec *)&host_new,
                            wo ? (struct itimerspec *)&host_old : NULL);
    if (rc < 0) return yos_errno_neg(ctx, errno);
    if (wo) __kernel_itimerspec_host_to_wasm32(
            &host_old, (struct wasm32___kernel_itimerspec *)wo);
    return 0;
}

int32_t yos_vfs_timer_settime64(struct yos_exec_ctx *ctx, int32_t timerid, int32_t flags, uint32_t new_value, uint32_t old_value)
{
    /* _time64 variant: wasm passes the modern 16-byte timespec; layouts
     * match host's __kernel_itimerspec. Plain memcpy + passthrough. */
    timer_t hid = timer_table_get(ctx, timerid);
    if (!hid) return yos_errno_neg(ctx, EINVAL);
    if (!new_value) return yos_errno_neg(ctx, EFAULT);
    struct itimerspec *host_new = wptr_range(ctx, new_value, sizeof(struct itimerspec));
    if (!host_new) return yos_errno_neg(ctx, EFAULT);
    struct itimerspec *host_old = NULL;
    if (old_value) {
        host_old = wptr_range(ctx, old_value, sizeof(struct itimerspec));
        if (!host_old) return yos_errno_neg(ctx, EFAULT);
    }
    int rc = timer_settime(hid, flags, host_new, host_old);
    return yos_errno_check(ctx, rc);
}

int32_t yos_vfs_timer_gettime(struct yos_exec_ctx *ctx, int32_t timerid, uint32_t cur_value)
{
    timer_t hid = timer_table_get(ctx, timerid);
    if (!hid) return yos_errno_neg(ctx, EINVAL);
    void *wo = wptr_range(ctx, cur_value, sizeof(struct wasm32___kernel_itimerspec));
    if (!wo) return yos_errno_neg(ctx, EFAULT);
    struct host64___kernel_itimerspec host_val;
    int rc = timer_gettime(hid, (struct itimerspec *)&host_val);
    if (rc < 0) return yos_errno_neg(ctx, errno);
    __kernel_itimerspec_host_to_wasm32(
        &host_val, (struct wasm32___kernel_itimerspec *)wo);
    return 0;
}

int32_t yos_vfs_timer_gettime64(struct yos_exec_ctx *ctx, int32_t timerid, uint32_t cur_value)
{
    timer_t hid = timer_table_get(ctx, timerid);
    if (!hid) return yos_errno_neg(ctx, EINVAL);
    struct itimerspec *host_val = wptr_range(ctx, cur_value, sizeof(struct itimerspec));
    if (!host_val) return yos_errno_neg(ctx, EFAULT);
    int rc = timer_gettime(hid, host_val);
    return yos_errno_check(ctx, rc);
}

int32_t yos_vfs_timer_delete(struct yos_exec_ctx *ctx, int32_t timerid)
{
    timer_t hid = timer_table_get(ctx, timerid);
    if (!hid) return yos_errno_neg(ctx, EINVAL);
    int rc = timer_delete(hid);
    timer_table_free(ctx, timerid);
    return yos_errno_check(ctx, rc);
}

int32_t yos_vfs_timer_getoverrun(struct yos_exec_ctx *ctx, int32_t timerid)
{
    timer_t hid = timer_table_get(ctx, timerid);
    if (!hid) return yos_errno_neg(ctx, EINVAL);
    int rc = timer_getoverrun(hid);
    return yos_errno_check(ctx, (int32_t)rc);
}

/* ============================================================================
 * futex with optional old_timespec32 timeout. The host's futex syscall
 * (#202 on x86_64) takes `struct __kernel_timespec *` (16 bytes); wasm
 * musl passes `struct old_timespec32 *` (8 bytes). NULL is valid and
 * means "no timeout". val2 is also reused for FUTEX_WAKE_OP etc.
 * ========================================================================= */

/* ============================================================================
 * NUMA mempolicy: bitmask arrays of `unsigned long`. wasm32 elements
 * are 4 bytes, host64 are 8. The kernel reads ceil(maxnode/8) bytes
 * regardless. We widen by zero-padding the high 32 bits of each
 * 64-bit slot (and narrow by checking the high bits are zero — if a
 * NUMA node ID > 32 came back from the kernel, the wasm side can't
 * represent it and we return -EOVERFLOW).
 * ========================================================================= */

static int nmask_w32_to_host(struct yos_exec_ctx *ctx, uint32_t wasm_addr,
                              uint32_t maxnode, uint64_t *host_buf,
                              size_t host_cap)
{
    if (!wasm_addr) return 0;
    size_t bits  = maxnode;
    size_t words = (bits + 63) / 64;  /* host 64-bit words */
    if (words > host_cap) return yos_errno_neg(ctx, EINVAL);
    /* wasm side: ceil(bits/32) 32-bit words. */
    size_t w32_words = (bits + 31) / 32;
    const uint32_t *wp = wptr_range(ctx, wasm_addr, (uint64_t)w32_words * 4ULL);
    if (!wp) return yos_errno_neg(ctx, EFAULT);
    for (size_t i = 0; i < words; i++) {
        uint32_t lo = (i*2     < w32_words) ? wp[i*2]     : 0;
        uint32_t hi = (i*2 + 1 < w32_words) ? wp[i*2 + 1] : 0;
        host_buf[i] = (uint64_t)lo | ((uint64_t)hi << 32);
    }
    return 0;
}

static int nmask_host_to_w32(struct yos_exec_ctx *ctx, uint32_t wasm_addr,
                              uint32_t maxnode, const uint64_t *host_buf)
{
    if (!wasm_addr) return 0;
    size_t bits  = maxnode;
    size_t words = (bits + 63) / 64;
    size_t w32_words = (bits + 31) / 32;
    uint32_t *wp = wptr_range(ctx, wasm_addr, (uint64_t)w32_words * 4ULL);
    if (!wp) return yos_errno_neg(ctx, EFAULT);
    for (size_t i = 0; i < words; i++) {
        uint64_t v = host_buf[i];
        if (i*2 < w32_words) wp[i*2]     = (uint32_t)(v & 0xffffffffu);
        if (i*2 + 1 < w32_words) wp[i*2 + 1] = (uint32_t)(v >> 32);
    }
    return 0;
}

#define NMASK_HOST_MAX 16   /* covers up to 1024 NUMA nodes */

int32_t yos_vfs_set_mempolicy(struct yos_exec_ctx *ctx, int32_t mode, uint32_t nmask, uint32_t maxnode)
{
    uint64_t host[NMASK_HOST_MAX] = {0};
    int r = nmask_w32_to_host(ctx, nmask, maxnode, host, NMASK_HOST_MAX);
    if (r < 0) return r;
    long ret = syscall(SYS_set_mempolicy, mode,
                        nmask ? (long)host : 0L, (long)maxnode);
    return yos_errno_check(ctx, (int32_t)ret);
}

int32_t yos_vfs_get_mempolicy(struct yos_exec_ctx *ctx, uint32_t mode, uint32_t nmask, uint32_t maxnode, uint32_t addr, uint32_t flags)
{
    uint64_t host[NMASK_HOST_MAX] = {0};
    int *mode_p = NULL;
    if (mode) {
        mode_p = wptr_range(ctx, mode, sizeof(int));
        if (!mode_p) return yos_errno_neg(ctx, EFAULT);
    }
    long ret = syscall(SYS_get_mempolicy, (long)mode_p,
                        nmask ? (long)host : 0L,
                        (long)maxnode, (long)addr, (long)flags);
    if (ret < 0) return yos_errno_neg(ctx, errno);
    if (nmask) nmask_host_to_w32(ctx, nmask, maxnode, host);
    return (int32_t)ret;
}

int32_t yos_vfs_mbind(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t mode, uint32_t nmask, uint32_t maxnode, uint32_t flags)
{
    uint64_t host[NMASK_HOST_MAX] = {0};
    void *start_p = wptr_range(ctx, start, len);
    if (!start_p && len) return yos_errno_neg(ctx, EFAULT);
    int r = nmask_w32_to_host(ctx, nmask, maxnode, host, NMASK_HOST_MAX);
    if (r < 0) return r;
    long ret = syscall(SYS_mbind, (long)(uintptr_t)start_p,
                        (long)len, (long)mode,
                        nmask ? (long)host : 0L, (long)maxnode, (long)flags);
    return yos_errno_check(ctx, (int32_t)ret);
}

int32_t yos_vfs_migrate_pages(struct yos_exec_ctx *ctx, int32_t pid, uint32_t maxnode, uint32_t old_nodes, uint32_t new_nodes)
{
    uint64_t old_host[NMASK_HOST_MAX] = {0};
    uint64_t new_host[NMASK_HOST_MAX] = {0};
    int r;
    if ((r = nmask_w32_to_host(ctx, old_nodes, maxnode, old_host, NMASK_HOST_MAX)) < 0) return r;
    if ((r = nmask_w32_to_host(ctx, new_nodes, maxnode, new_host, NMASK_HOST_MAX)) < 0) return r;
    long ret = syscall(SYS_migrate_pages, (long)pid, (long)maxnode,
                        old_nodes ? (long)old_host : 0L,
                        new_nodes ? (long)new_host : 0L);
    return yos_errno_check(ctx, (int32_t)ret);
}

/* ============================================================================
 * execveat: like execve but with dirfd + flags. The argv/envp arrays
 * are pointer-to-pointer — same shape as execve already handles, so
 * we delegate by setting up an exec_pending and reusing the outer
 * loop. For brevity we just call back into yos_execve via path
 * resolved through dirfd; supports AT_EMPTY_PATH (re-exec self).
 * ========================================================================= */

extern int32_t yos_execve(struct yos_exec_ctx *ctx, uint32_t filename,
                                uint32_t argv, uint32_t envp);

int32_t yos_vfs_execveat(struct yos_exec_ctx *ctx, int32_t dirfd, uint32_t pathname, uint32_t argv, uint32_t envp, int32_t flags)
{
    /* Resolve dirfd-relative pathname to an absolute path so we can
     * reuse yos_execve. AT_EMPTY_PATH means "use whatever dirfd
     * points to" (Linux 3.18+). */
    const char *p = wstr_check(ctx, pathname);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    char resolved[PATH_MAX];
    if (dirfd == -100 /* AT_FDCWD */ || p[0] == '/') {
        snprintf(resolved, sizeof(resolved), "%s", p);
    } else if ((flags & 0x1000 /* AT_EMPTY_PATH */) && p[0] == '\0') {
        /* /proc/self/fd/<dirfd> resolves to the underlying executable. */
        int hfd = yos_fd_get(ctx, dirfd);
        if (hfd < 0) return hfd;
        snprintf(resolved, sizeof(resolved), "/proc/self/fd/%d", hfd);
    } else {
        int hfd = yos_fd_get(ctx, dirfd);
        if (hfd < 0) return hfd;
        snprintf(resolved, sizeof(resolved), "/proc/self/fd/%d/%s", hfd, p);
    }
    /* Marshal a fresh wasm-side filename string into a scratch slot.
     * Easier: just copy into a temp wasm buffer at heap_end and call
     * yos_execve with that wasm address. */
    size_t need = strlen(resolved) + 1;
    if ((uint64_t)ctx->heap_end + need > (uint64_t)ctx->memory_size)
        return yos_errno_neg(ctx, ENOMEM);
    uint32_t scratch = ctx->heap_end;
    memcpy(ctx->memory + scratch, resolved, need);
    return yos_execve(ctx, scratch, argv, envp);
}

/* ============================================================================
 * get_robust_list: head_ptr stores the wasm-side robust_list_head pointer.
 * The kernel doesn't dereference it; it just records what was given via
 * set_robust_list. Just translate the outer level.
 * ========================================================================= */

int32_t yos_vfs_get_robust_list(struct yos_exec_ctx *ctx, int32_t pid, uint32_t head_ptr, uint32_t len_ptr)
{
    /* The kernel returns pid 0's saved head — likely a wasm address or
     * NULL. Pass the outer pointers through; the inner address is
     * opaque. */
    void *hp = head_ptr ? wptr(ctx, head_ptr) : NULL;
    void *lp = len_ptr ? wptr(ctx, len_ptr) : NULL;
    long r = syscall(SYS_get_robust_list, (long)pid, (long)hp, (long)lp);
    return yos_errno_check(ctx, (int32_t)r);
}

/* ============================================================================
 * Linux AIO. aio_context_t is unsigned long — 4 bytes on wasm32,
 * 8 bytes on the host. Real handles fit in 32 bits in practice
 * (kernel returns small refcount IDs), so we narrow with overflow
 * detection. iocb** in io_submit needs per-pointer translation.
 * ========================================================================= */

int32_t yos_vfs_io_setup(struct yos_exec_ctx *ctx, uint32_t nr_events, uint32_t ctx_idp)
{
    if (!ctx_idp) return yos_errno_neg(ctx, EFAULT);
    unsigned long host_id = 0;
    long r = syscall(SYS_io_setup, (unsigned long)nr_events, &host_id);
    if (r < 0) return yos_errno_neg(ctx, errno);
    if (host_id > 0xffffffffUL) {
        long _ignore = syscall(SYS_io_destroy, host_id); (void)_ignore;
        return yos_errno_neg(ctx, EOVERFLOW);
    }
    uint32_t *out = wptr(ctx, ctx_idp);
    if (!out) { long _i = syscall(SYS_io_destroy, host_id); (void)_i; return yos_errno_neg(ctx, EFAULT); }
    *out = (uint32_t)host_id;
    return 0;
}

int32_t yos_vfs_io_destroy(struct yos_exec_ctx *ctx, uint32_t ctx_id)
{
    (void)ctx;
    long r = syscall(SYS_io_destroy, (unsigned long)ctx_id);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_vfs_io_submit(struct yos_exec_ctx *ctx, uint32_t ctx_id, int32_t nr, uint32_t iocbpp)
{
    if (nr <= 0) return 0;
    uint32_t *wpp = wptr(ctx, iocbpp);
    if (!wpp) return yos_errno_neg(ctx, EFAULT);
    /* Build a host-side array of iocb pointers. The IOCBs themselves
     * stay in wasm memory; the kernel reads through these pointers,
     * but it expects the iocb layout to be the host's 64-bit one. We
     * convert each iocb in place into a per-call scratch buffer and
     * pass scratch addresses. Cap at 64 IOCBs per submit; bigger
     * batches are rare and can be split by the caller. */
    if (nr > 64) return yos_errno_neg(ctx, E2BIG);
    struct host64_iocb scratch[64];
    struct iocb *hpp[64];
    for (int i = 0; i < nr; i++) {
        uint32_t w_iocb = wpp[i];
        if (!w_iocb) return yos_errno_neg(ctx, EFAULT);
        const struct wasm32_iocb *wcb = wptr(ctx, w_iocb);
        if (!wcb) return yos_errno_neg(ctx, EFAULT);
        iocb_wasm32_to_host(wcb, &scratch[i]);
        hpp[i] = (struct iocb *)&scratch[i];
    }
    long r = syscall(SYS_io_submit, (unsigned long)ctx_id, (long)nr, (long)hpp);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_vfs_io_cancel(struct yos_exec_ctx *ctx, uint32_t ctx_id, uint32_t iocb_addr, uint32_t result)
{
    const struct wasm32_iocb *wcb = wptr(ctx, iocb_addr);
    if (!wcb) return yos_errno_neg(ctx, EFAULT);
    struct host64_iocb host_cb;
    iocb_wasm32_to_host(wcb, &host_cb);
    struct host64_io_event host_ev;
    long r = syscall(SYS_io_cancel, (unsigned long)ctx_id,
                      (long)&host_cb, (long)&host_ev);
    if (r < 0) return yos_errno_neg(ctx, errno);
    if (result) {
        struct wasm32_io_event *wev = wptr(ctx, result);
        if (wev) io_event_host_to_wasm32(&host_ev, wev);
    }
    return 0;
}

int32_t yos_vfs_futex(struct yos_exec_ctx *ctx, uint32_t uaddr, int32_t op, int32_t val, uint32_t utime, uint32_t uaddr2, int32_t val3)
{
    /* Use the host's struct timespec from <time.h>; on x86_64 the kernel
     * accepts that shape directly and the field offsets match
     * __kernel_timespec (both int64 sec / int64 nsec). */
    struct timespec ts_buf;
    struct timespec *ts_arg = NULL;
    /* Some FUTEX_* ops reinterpret utime as a small unsigned int (the
     * 'val2' in FUTEX_REQUEUE et al.). The kernel only treats it as a
     * timespec pointer for FUTEX_WAIT / FUTEX_LOCK_PI / etc. */
    int futex_op = op & 0x7f;  /* strip FUTEX_PRIVATE_FLAG / FUTEX_CLOCK_REALTIME */
    int has_timeout = (futex_op == 0  /* FUTEX_WAIT */ ||
                        futex_op == 6  /* FUTEX_WAIT_BITSET */ ||
                        futex_op == 8  /* FUTEX_LOCK_PI */ ||
                        futex_op == 11 /* FUTEX_WAIT_REQUEUE_PI */ );
    if (utime && has_timeout) {
        const int32_t *wts = wptr(ctx, utime);
        if (!wts) return yos_errno_neg(ctx, EFAULT);
        ts_buf.tv_sec  = (int64_t)(int32_t)wts[0];
        ts_buf.tv_nsec = (int64_t)(int32_t)wts[1];
        ts_arg = &ts_buf;
    }
    void *uaddr_p  = wptr(ctx, uaddr);
    void *uaddr2_p = uaddr2 ? wptr(ctx, uaddr2) : NULL;
    /* Use raw syscall — futex's per-op semantics defy the glibc wrapper. */
    long r = syscall(SYS_futex, uaddr_p, op, (long)val,
                     has_timeout ? (long)ts_arg : (long)utime,
                     uaddr2_p, (long)val3);
    return yos_errno_check(ctx, (int32_t)r);
}
