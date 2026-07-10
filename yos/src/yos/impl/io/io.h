#ifndef YOS_VFS_H
#define YOS_VFS_H

#include "yos/types.h"

/* Per-runtime fd table. See comment on yos_exec_ctx::fd_map. */

/* Initialize the table: wfd 0/1/2 → host fd 0/1/2, rest closed. */
void    yos_fd_table_init(struct yos_exec_ctx *ctx);

/* Translate a wasm fd to its host fd. AT_FDCWD passes through. Returns
 * -EBADF for closed/out-of-range slots. Used by every fd-consuming
 * syscall before invoking the kernel. */
int32_t yos_fd_get(struct yos_exec_ctx *ctx, int32_t wfd);

/* Register host_fd in the lowest-free wasm-fd slot and return the wfd.
 * If host_fd < 0, returns it unchanged so callers can chain on syscall
 * errors. On table exhaustion, host-closes host_fd and returns -EMFILE
 * to avoid leaking. */
int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);

/* Place host_fd at wasm slot newfd. Closes any previous host fd at
 * that slot. host_fd < 0 propagates as-is. Used by dup2/dup3 after
 * the caller has produced a fresh host fd for the new slot. */
int32_t yos_fd_assign(struct yos_exec_ctx *ctx, int32_t newfd, int host_fd);

/* Host-close the wasm fd's backing host fd and free the slot. */
int32_t yos_fd_close(struct yos_exec_ctx *ctx, int32_t wfd);

/* Populate child->fd_map from parent->fd_map by dup()ing each used
 * host fd. After this, parent and child have independent host fds for
 * every wasm fd; close/dup2 in one doesn't affect the other. */
void    yos_fd_fork_dup(struct yos_exec_ctx *child,
                        struct yos_exec_ctx *parent);


int32_t yos_read(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count);
int32_t yos_write(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count);
int32_t yos_open(struct yos_exec_ctx *ctx, uint32_t path, int32_t flags, int32_t mode);
int32_t yos_close(struct yos_exec_ctx *ctx, int32_t fd);
int32_t yos_creat(struct yos_exec_ctx *ctx, uint32_t pathname, int32_t mode);
int32_t yos_link(struct yos_exec_ctx *ctx, uint32_t oldname, uint32_t newname);
int32_t yos_unlink(struct yos_exec_ctx *ctx, uint32_t pathname);
int32_t yos_chdir(struct yos_exec_ctx *ctx, uint32_t filename);
int32_t yos_chmod(struct yos_exec_ctx *ctx, uint32_t filename, int32_t mode);
int32_t yos_lchown(struct yos_exec_ctx *ctx, uint32_t filename, int32_t user, int32_t group);
int32_t yos_lseek(struct yos_exec_ctx *ctx, int32_t fd, int32_t offset, int32_t whence);
int32_t yos_vfs__llseek(struct yos_exec_ctx *ctx, int32_t fd, uint32_t offset_high, uint32_t offset_low, uint32_t result_ptr, int32_t whence);
int32_t yos_access(struct yos_exec_ctx *ctx, uint32_t filename, int32_t mode);
int32_t yos_rename(struct yos_exec_ctx *ctx, uint32_t oldname, uint32_t newname);
int32_t yos_mkdir(struct yos_exec_ctx *ctx, uint32_t pathname, int32_t mode);
int32_t yos_rmdir(struct yos_exec_ctx *ctx, uint32_t pathname);
int32_t yos_pipe(struct yos_exec_ctx *ctx, uint32_t fildes);
int32_t yos_ioctl(struct yos_exec_ctx *ctx, int32_t fd, uint32_t cmd, uint32_t arg);
int32_t yos_fcntl(struct yos_exec_ctx *ctx, int32_t fd, int32_t cmd, int32_t arg);
int32_t yos_vfs_fcntl64(struct yos_exec_ctx *ctx, int32_t fd, int32_t cmd, int32_t arg);
int32_t yos_vfs_chroot(struct yos_exec_ctx *ctx, uint32_t filename);
int32_t yos_symlink(struct yos_exec_ctx *ctx, uint32_t oldpath, uint32_t newpath);
int32_t yos_readlink(struct yos_exec_ctx *ctx, uint32_t path, uint32_t buf, uint32_t bufsiz);
int32_t yos_truncate(struct yos_exec_ctx *ctx, uint32_t path, int32_t length);
int32_t yos_ftruncate(struct yos_exec_ctx *ctx, int32_t fd, int32_t length);
int32_t yos_fchmod(struct yos_exec_ctx *ctx, int32_t fd, int32_t mode);
int32_t yos_fchown(struct yos_exec_ctx *ctx, int32_t fd, int32_t user, int32_t group);
int32_t yos_readv(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen);
int32_t yos_writev(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen);
int32_t yos_vfs_pread64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count, uint32_t pos);
int32_t yos_vfs_pwrite64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf, uint32_t count, uint32_t pos);
int32_t yos_chown(struct yos_exec_ctx *ctx, uint32_t filename, int32_t user, int32_t group);
int32_t yos_getcwd(struct yos_exec_ctx *ctx, uint32_t buf, uint32_t size);
int32_t yos_openat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t flags, int32_t mode);
int32_t yos_mkdirat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname, int32_t mode);
int32_t yos_vfs_mknodat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t mode, uint32_t dev);
int32_t yos_fchownat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t user, int32_t group, int32_t flag);
int32_t yos_vfs_fstatat64(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, uint32_t statbuf, int32_t flag);
int32_t yos_unlinkat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname, int32_t flag);
int32_t yos_renameat(struct yos_exec_ctx *ctx, int32_t olddfd, uint32_t oldname, int32_t newdfd, uint32_t newname);
int32_t yos_linkat(struct yos_exec_ctx *ctx, int32_t olddfd, uint32_t oldname, int32_t newdfd, uint32_t newname, int32_t flags);
int32_t yos_symlinkat(struct yos_exec_ctx *ctx, uint32_t oldname, int32_t newdfd, uint32_t newname);
int32_t yos_readlinkat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t path, uint32_t buf, uint32_t bufsiz);
int32_t yos_fchmodat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t mode);
int32_t yos_faccessat(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t filename, int32_t mode);
int32_t yos_dup2(struct yos_exec_ctx *ctx, int32_t oldfd, int32_t newfd);
int32_t yos_dup3(struct yos_exec_ctx *ctx, int32_t oldfd, int32_t newfd, int32_t flags);
int32_t yos_pipe2(struct yos_exec_ctx *ctx, uint32_t fildes, int32_t flags);
int32_t yos_vfs_socketpair(struct yos_exec_ctx *ctx, int32_t domain, int32_t type, int32_t protocol, uint32_t sv);
int32_t yos_preadv(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h);
int32_t yos_pwritev(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h);
int32_t yos_vfs_preadv2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h, int32_t flags);
int32_t yos_vfs_pwritev2(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, int32_t vlen, uint32_t pos_l, uint32_t pos_h, int32_t flags);
int32_t yos_vfs_vmsplice(struct yos_exec_ctx *ctx, int32_t fd, uint32_t vec, uint32_t vlen, uint32_t flags);
int32_t yos_vfs_process_madvise(struct yos_exec_ctx *ctx, int32_t pidfd, uint32_t vec, uint32_t vlen, int32_t behavior, uint32_t flags);
int32_t yos_vfs_process_vm_readv(struct yos_exec_ctx *ctx, int32_t pid, uint32_t lvec, uint32_t liovcnt, uint32_t rvec, uint32_t riovcnt, uint32_t flags);
int32_t yos_vfs_process_vm_writev(struct yos_exec_ctx *ctx, int32_t pid, uint32_t lvec, uint32_t liovcnt, uint32_t rvec, uint32_t riovcnt, uint32_t flags);
int32_t yos_vfs_getdents(struct yos_exec_ctx *ctx, int32_t fd, uint32_t dirent, uint32_t count);
int32_t yos_vfs_getdents64(struct yos_exec_ctx *ctx, int32_t fd, uint32_t dirent, uint32_t count);
int32_t yos_vfs_statx(struct yos_exec_ctx *ctx, int32_t dfd, uint32_t pathname, int32_t flags, uint32_t mask, uint32_t buffer);

/* sendfile / sendfile64: handle the host's 8-byte off_t through the
 * wasm's 4-byte / 8-byte slot. wasm32 musl declares off_t as 8 bytes
 * already (POSIX-2008), but libc's i386 sendfile() prototype uses
 * `long *offset` — extractor sees a `long *` (4-byte pointee).  We
 * read both wasm halves, run the host call with a local off_t, and
 * write the result back, with an overflow check on the wasm side. */
int32_t yos_vfs_sendfile(struct yos_exec_ctx *ctx, int32_t out_fd, int32_t in_fd, uint32_t offset_ptr, uint32_t count);
int32_t yos_vfs_sendfile64(struct yos_exec_ctx *ctx, int32_t out_fd, int32_t in_fd, uint32_t offset_ptr, uint32_t count);

/* POSIX timer_t handling. Host timer_t is opaque pointer-sized, wasm32's
 * is 4 bytes; we run a per-runtime mapping table inside yos-vfs.c so the
 * wasm caller sees a plain int32 handle. */
int32_t yos_vfs_timer_create(struct yos_exec_ctx *ctx, int32_t clockid, uint32_t sevp, uint32_t timerid_out);
int32_t yos_vfs_timer_settime(struct yos_exec_ctx *ctx, int32_t timerid, int32_t flags, uint32_t new_value, uint32_t old_value);
int32_t yos_vfs_timer_settime64(struct yos_exec_ctx *ctx, int32_t timerid, int32_t flags, uint32_t new_value, uint32_t old_value);
int32_t yos_vfs_timer_gettime(struct yos_exec_ctx *ctx, int32_t timerid, uint32_t cur_value);
int32_t yos_vfs_timer_gettime64(struct yos_exec_ctx *ctx, int32_t timerid, uint32_t cur_value);
int32_t yos_vfs_timer_delete(struct yos_exec_ctx *ctx, int32_t timerid);
int32_t yos_vfs_timer_getoverrun(struct yos_exec_ctx *ctx, int32_t timerid);

/* futex with optional timeout — kernel takes __kernel_timespec on x86_64
 * but wasm musl passes old_timespec32 (8 bytes). Convert when non-NULL. */
int32_t yos_vfs_futex(struct yos_exec_ctx *ctx, uint32_t uaddr, int32_t op, int32_t val, uint32_t utime, uint32_t uaddr2, int32_t val3);

/* NUMA mempolicy + page placement. Each takes one or more
 * `unsigned long *` arrays whose element width differs (4 vs 8).
 * The kernel reads/writes bytes equal to ceil(maxnode/8); we widen
 * 32-bit words to 64-bit, call the host syscall, and (for OUT args)
 * narrow back. Listed here so the codegen routes the syscall to a
 * concrete handler instead of #error-ing on the long-pointer-width. */
int32_t yos_vfs_set_mempolicy(struct yos_exec_ctx *ctx, int32_t mode, uint32_t nmask, uint32_t maxnode);
int32_t yos_vfs_get_mempolicy(struct yos_exec_ctx *ctx, uint32_t mode, uint32_t nmask, uint32_t maxnode, uint32_t addr, uint32_t flags);
int32_t yos_vfs_mbind(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t mode, uint32_t nmask, uint32_t maxnode, uint32_t flags);
int32_t yos_vfs_migrate_pages(struct yos_exec_ctx *ctx, int32_t pid, uint32_t maxnode, uint32_t old_nodes, uint32_t new_nodes);

/* execveat: like execve but with dirfd + flags. argv/envp arrays of
 * pointers need full conversion — same shape as execve. */
int32_t yos_vfs_execveat(struct yos_exec_ctx *ctx, int32_t dirfd, uint32_t pathname, uint32_t argv, uint32_t envp, int32_t flags);

/* get_robust_list / set_robust_list: head_ptr is a wasm pointer to a
 * pointer; just translate the outer level. The kernel never dereferences
 * the inner pointer (it stores it for later examination by ptrace). */
int32_t yos_vfs_get_robust_list(struct yos_exec_ctx *ctx, int32_t pid, uint32_t head_ptr, uint32_t len_ptr);

/* io_setup: aio_context_t is `unsigned long`. Allocate via host, write
 * the value back through a 4-byte wasm slot (kernel handles are small
 * enough to fit). */
int32_t yos_vfs_io_setup(struct yos_exec_ctx *ctx, uint32_t nr_events, uint32_t ctx_idp);
int32_t yos_vfs_io_destroy(struct yos_exec_ctx *ctx, uint32_t ctx_id);
int32_t yos_vfs_io_submit(struct yos_exec_ctx *ctx, uint32_t ctx_id, int32_t nr, uint32_t iocbpp);
int32_t yos_vfs_io_cancel(struct yos_exec_ctx *ctx, uint32_t ctx_id, uint32_t iocb, uint32_t result);

#endif /* YOS_VFS_H */
