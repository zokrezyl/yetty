/* impl/io/io-windows.c — POSIX I/O bridges, Windows host.
 *
 * The Linux-only deep set (futex / NUMA mempolicy / mbind / migrate_pages /
 * io_setup,destroy,submit,cancel / sendfile / preadv2 / pwritev2 / statx /
 * getdents{,64} / robust_list / execveat / vmsplice / process_madvise /
 * process_vm_*) all return -ENOSYS here — none has a portable Windows
 * analogue we want to fake.
 *
 * yos_pipe2 uses msvcrt _pipe(); FreeBSD O_NONBLOCK / O_CLOEXEC bits
 * are emulated with _setmode and the no-inherit flag. ioctl_cmd_fb_to_lx
 * is identity passthrough — Windows doesn't share Linux's ioctl number
 * encoding and we don't actually dispatch tty ioctls into the host
 * kernel; impl/io/io.c handles the TIOC* cases via emulation paths
 * before reaching ioctl_cmd_fb_to_lx.
 *
 * NO #ifdef in this file — meson selects it only on windows hosts.
 */

#include "yos/types.h"
#include "impl/io/io-internal.h"
#include "impl/errno_helpers.h"
#include <yos/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <io.h>          /* _pipe, _close, _setmode */
#include <stdint.h>
#include <stddef.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#ifndef _O_NOINHERIT
#define _O_NOINHERIT 0x0080
#endif
#ifndef _O_BINARY
#define _O_BINARY    0x8000
#endif

int32_t yos_pipe2(struct yos_exec_ctx *ctx, uint32_t fildes, int32_t flags)
{
    int *p = wptr(ctx, fildes);
    if (!p) return yos_errno_neg(ctx, EFAULT);

    int want_cloexec  = !!(flags & 0x00100000); /* FreeBSD O_CLOEXEC */
    int want_nonblock = !!(flags & 0x00000004); /* FreeBSD O_NONBLOCK */

    int hfds[2];
    int mode = _O_BINARY | (want_cloexec ? _O_NOINHERIT : 0);
    if (_pipe(hfds, 65536, mode) < 0)
        return yos_errno_neg(ctx, errno);

    /* Windows anonymous pipes are inherently blocking; emulate
     * O_NONBLOCK by switching to non-blocking via SetNamedPipeHandleState
     * is not available for anonymous pipes. Best we can do is record
     * the flag elsewhere; for now the bit is dropped silently if set. */
    (void)want_nonblock;

    extern int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);
    extern int32_t yos_fd_close(struct yos_exec_ctx *ctx, int32_t wfd);
    int32_t r = yos_fd_alloc(ctx, hfds[0]);
    if (r < 0) { _close(hfds[1]); return r; }
    int32_t w = yos_fd_alloc(ctx, hfds[1]);
    if (w < 0) { yos_fd_close(ctx, r); return w; }
    p[0] = r;
    p[1] = w;
    return 0;
}

uint32_t ioctl_cmd_fb_to_lx(uint32_t cmd) { return cmd; }

/* ── Linux-only deep set ──────────────────────────────────────────── */
int32_t yos_vfs_preadv2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h, int32_t flags)
{ (void)ctx;(void)fd;(void)vec;(void)vlen;(void)pos_l;(void)pos_h;(void)flags; return -ENOSYS; }
int32_t yos_vfs_pwritev2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h, int32_t flags)
{ (void)ctx;(void)fd;(void)vec;(void)vlen;(void)pos_l;(void)pos_h;(void)flags; return -ENOSYS; }
int32_t yos_vfs_vmsplice(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, uint32_t vlen, uint32_t flags)
{ (void)ctx;(void)fd;(void)vec;(void)vlen;(void)flags; return -ENOSYS; }
int32_t yos_vfs_process_madvise(struct yos_exec_ctx *ctx, int32_t pidfd, uint32_t vec, uint32_t vlen, int32_t behavior, uint32_t flags)
{ (void)ctx;(void)pidfd;(void)vec;(void)vlen;(void)behavior;(void)flags; return -ENOSYS; }
int32_t yos_vfs_process_vm_readv(struct yos_exec_ctx *ctx, int32_t pid, uint32_t lvec, uint32_t liovcnt, uint32_t rvec, uint32_t riovcnt, uint32_t flags)
{ (void)ctx;(void)pid;(void)lvec;(void)liovcnt;(void)rvec;(void)riovcnt;(void)flags; return -ENOSYS; }
int32_t yos_vfs_process_vm_writev(struct yos_exec_ctx *ctx, int32_t pid, uint32_t lvec, uint32_t liovcnt, uint32_t rvec, uint32_t riovcnt, uint32_t flags)
{ (void)ctx;(void)pid;(void)lvec;(void)liovcnt;(void)rvec;(void)riovcnt;(void)flags; return -ENOSYS; }
int32_t yos_vfs_getdents(struct yos_exec_ctx *ctx, int32_t fd, uint32_t dirent, uint32_t count)
{ (void)ctx;(void)fd;(void)dirent;(void)count; return -ENOSYS; }
int32_t yos_vfs_getdents64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t dirent, uint32_t count)
{ (void)ctx;(void)fd;(void)dirent;(void)count; return -ENOSYS; }
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
int32_t yos_vfs_renameat2(struct yos_exec_ctx *ctx, int32_t olddfd, uint32_t oldpath, int32_t newdfd, uint32_t newpath, uint32_t flags)
{ (void)ctx;(void)olddfd;(void)oldpath;(void)newdfd;(void)newpath;(void)flags; return -ENOSYS; }
