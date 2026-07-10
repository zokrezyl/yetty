/* impl/io/io-darwin.c — POSIX I/O bridges, darwin host.
 *
 * Covers what's available on macOS / iOS / tvOS / iOS-Sim (libSystem):
 *   - yos_pipe2 via pipe(2) + fcntl(F_SETFD|F_SETFL) — there is no
 *     darwin pipe2(2).
 *   - ioctl_cmd_fb_to_lx is the identity — darwin shares the BSD
 *     ioctl encoding (`_IOR/_IOW` macros), so the FB_TIO and FB_FIO
 *     numeric values already match the host's. Translating to Linux
 *     values here would feed the darwin kernel ENOTTY.
 *
 * The Linux-only set (statx, sendfile/64, timer_create/settime/
 * gettime/delete, set_mempolicy / get_mempolicy / mbind /
 * migrate_pages, execveat, get_robust_list, io_setup,destroy,submit,
 * cancel, futex, preadv2, pwritev2, renameat2) all return -ENOSYS
 * here. These stubs let the wasm guest fail gracefully on calls it
 * shouldn't be making on darwin in the first place (most are libuv /
 * glibc-extension callers that already retry against the POSIX
 * baseline when they see ENOSYS).
 *
 * NO #ifdef inside this file — meson selects it only on darwin hosts.
 * linux hosts use io-linux.c.
 */

#include "yos/types.h"
#include "impl/io/io-internal.h"
#include "impl/errno_helpers.h"
#include <yos/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* ── yos_pipe2 ──── pipe(2) + fcntl fallback ───────────────────────── */
int32_t yos_pipe2(struct yos_exec_ctx *ctx, uint32_t fildes, int32_t flags)
{
    int *p = wptr(ctx, fildes);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    int hfds[2];
    if (pipe(hfds) < 0) return yos_errno_neg(ctx, errno);
    int want_cloexec  = !!(flags & 0x00100000);
    int want_nonblock = !!(flags & 0x00000004);
    for (int i = 0; i < 2; i++) {
        if (want_cloexec) {
            int fl = fcntl(hfds[i], F_GETFD);
            if (fl >= 0) fcntl(hfds[i], F_SETFD, fl | FD_CLOEXEC);
        }
        if (want_nonblock) {
            int fl = fcntl(hfds[i], F_GETFL);
            if (fl >= 0) fcntl(hfds[i], F_SETFL, fl | O_NONBLOCK);
        }
    }
    extern int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);
    extern int32_t yos_fd_close(struct yos_exec_ctx *ctx, int32_t wfd);
    /* yos_fd_alloc(hfds[i]) takes ownership of the host fd:
     *   - on success it's recorded in fd_map (released by yos_fd_close
     *     on the returned wfd, or by ctx teardown);
     *   - on EMFILE failure fd_alloc closes the host fd internally
     *     before returning the negative errno.
     * Either way the caller MUST NOT close hfds[i] directly. */
    int32_t r = yos_fd_alloc(ctx, hfds[0]);
    if (r < 0) {
        /* hfds[0] already closed by fd_alloc on EMFILE; we only need
         * to clean up the partner end. */
        close(hfds[1]);
        return r;
    }
    int32_t w = yos_fd_alloc(ctx, hfds[1]);
    if (w < 0) {
        /* hfds[1] already closed by fd_alloc; release the wfd we
         * allocated for hfds[0] (yos_fd_close also closes hfds[0]). */
        yos_fd_close(ctx, r);
        return w;
    }
    p[0] = r;
    p[1] = w;
    return 0;
}

/* ── ioctl_cmd_fb_to_lx — identity passthrough on darwin/BSD ──────── */
uint32_t ioctl_cmd_fb_to_lx(uint32_t cmd) { return cmd; }

/* ── Linux-only stubs ─────────────────────────────────────────────── */
int32_t yos_vfs_preadv2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h, int32_t flags)
{ (void)ctx;(void)fd;(void)vec;(void)vlen;(void)pos_l;(void)pos_h;(void)flags; return -ENOSYS; }
int32_t yos_vfs_pwritev2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h, int32_t flags)
{ (void)ctx;(void)fd;(void)vec;(void)vlen;(void)pos_l;(void)pos_h;(void)flags; return -ENOSYS; }
int32_t yos_vfs_statx(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname, int32_t flags, uint32_t mask, uint32_t buffer)
{ (void)ctx;(void)dfd;(void)pathname;(void)flags;(void)mask;(void)buffer; return -ENOSYS; }
int32_t yos_vfs_sendfile(struct yos_exec_ctx *ctx, int32_t out_fd, int32_t in_fd, uint32_t offset_ptr, uint32_t count)
{ (void)ctx;(void)out_fd;(void)in_fd;(void)offset_ptr;(void)count; return -ENOSYS; }
int32_t yos_vfs_sendfile64(struct yos_exec_ctx *ctx, int32_t out_fd, int32_t in_fd, uint32_t offset_ptr, uint32_t count)
{ (void)ctx;(void)out_fd;(void)in_fd;(void)offset_ptr;(void)count; return -ENOSYS; }
int32_t yos_vfs_timer_create(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t sevp, uint32_t timerid_out)
{ (void)ctx;(void)clockid;(void)sevp;(void)timerid_out; return -ENOSYS; }
int32_t yos_vfs_timer_settime(struct yos_exec_ctx *ctx, int32_t timerid, int32_t flags, uint32_t new_value, uint32_t old_value)
{ (void)ctx;(void)timerid;(void)flags;(void)new_value;(void)old_value; return -ENOSYS; }
int32_t yos_vfs_timer_settime64(struct yos_exec_ctx *ctx, int32_t timerid, int32_t flags, uint32_t new_value, uint32_t old_value)
{ (void)ctx;(void)timerid;(void)flags;(void)new_value;(void)old_value; return -ENOSYS; }
int32_t yos_vfs_timer_gettime(struct yos_exec_ctx *ctx, int32_t timerid, uint32_t cur_value)
{ (void)ctx;(void)timerid;(void)cur_value; return -ENOSYS; }
int32_t yos_vfs_timer_gettime64(struct yos_exec_ctx *ctx, int32_t timerid, uint32_t cur_value)
{ (void)ctx;(void)timerid;(void)cur_value; return -ENOSYS; }
int32_t yos_vfs_timer_delete(struct yos_exec_ctx *ctx, int32_t timerid)
{ (void)ctx;(void)timerid; return -ENOSYS; }
int32_t yos_vfs_timer_getoverrun(struct yos_exec_ctx *ctx, int32_t timerid)
{ (void)ctx;(void)timerid; return -ENOSYS; }
int32_t yos_vfs_set_mempolicy(struct yos_exec_ctx *ctx, int32_t mode, uint32_t nmask, uint32_t maxnode)
{ (void)ctx;(void)mode;(void)nmask;(void)maxnode; return -ENOSYS; }
int32_t yos_vfs_get_mempolicy(struct yos_exec_ctx *ctx, uint32_t mode, uint32_t nmask, uint32_t maxnode, uint32_t addr, uint32_t flags)
{ (void)ctx;(void)mode;(void)nmask;(void)maxnode;(void)addr;(void)flags; return -ENOSYS; }
int32_t yos_vfs_mbind(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t mode, uint32_t nmask, uint32_t maxnode, uint32_t flags)
{ (void)ctx;(void)start;(void)len;(void)mode;(void)nmask;(void)maxnode;(void)flags; return -ENOSYS; }
int32_t yos_vfs_migrate_pages(struct yos_exec_ctx *ctx, int32_t pid, uint32_t maxnode, uint32_t old_nodes, uint32_t new_nodes)
{ (void)ctx;(void)pid;(void)maxnode;(void)old_nodes;(void)new_nodes; return -ENOSYS; }
int32_t yos_vfs_execveat(struct yos_exec_ctx *ctx, int32_t dirfd, uint32_t pathname, uint32_t argv, uint32_t envp, int32_t flags)
{ (void)ctx;(void)dirfd;(void)pathname;(void)argv;(void)envp;(void)flags; return -ENOSYS; }
int32_t yos_vfs_get_robust_list(struct yos_exec_ctx *ctx, int32_t pid, uint32_t head_ptr, uint32_t len_ptr)
{ (void)ctx;(void)pid;(void)head_ptr;(void)len_ptr; return -ENOSYS; }
int32_t yos_vfs_io_setup(struct yos_exec_ctx *ctx, uint32_t nr_events, uint32_t ctx_idp)
{ (void)ctx;(void)nr_events;(void)ctx_idp; return -ENOSYS; }
int32_t yos_vfs_io_destroy(struct yos_exec_ctx *ctx, uint32_t ctx_id)
{ (void)ctx;(void)ctx_id; return -ENOSYS; }
int32_t yos_vfs_io_submit(struct yos_exec_ctx *ctx, uint32_t ctx_id, int32_t nr, uint32_t iocbpp)
{ (void)ctx;(void)ctx_id;(void)nr;(void)iocbpp; return -ENOSYS; }
int32_t yos_vfs_io_cancel(struct yos_exec_ctx *ctx, uint32_t ctx_id, uint32_t iocb_addr, uint32_t result)
{ (void)ctx;(void)ctx_id;(void)iocb_addr;(void)result; return -ENOSYS; }
int32_t yos_vfs_futex(struct yos_exec_ctx *ctx, uint32_t uaddr, int32_t op, int32_t val, uint32_t utime, uint32_t uaddr2, int32_t val3)
{ (void)ctx;(void)uaddr;(void)op;(void)val;(void)utime;(void)uaddr2;(void)val3; return -ENOSYS; }

/* renameat2 only existed in glibc's syscall wrapper on Linux; darwin
 * has no equivalent. Wasm guests that call it (rename(2) under newer
 * libc) get ENOSYS and fall back to the older renameat(2). */
int32_t yos_vfs_renameat2(struct yos_exec_ctx *ctx, int32_t olddfd, uint32_t oldpath, int32_t newdfd, uint32_t newpath, uint32_t flags)
{ (void)ctx;(void)olddfd;(void)oldpath;(void)newdfd;(void)newpath;(void)flags; return -ENOSYS; }
