/* impl/posix.c — host-libc passthrough impls for POSIX fns that
 * hooks.yaml routes to custom_<area> but for which the auto-bridge
 * isn't usable (fd-virtualisation, signature edge cases). All small,
 * mostly one-liners.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>      /* posix_madvise */

#if defined(__APPLE__)
/* darwin has no fdatasync; fsync is the closest equivalent (it does
 * the same data-and-metadata flush, and F_FULLFSYNC is a stronger
 * variant). Map fdatasync -> fsync for the host. */
#  define fdatasync(fd) fsync(fd)
/* SOCK_CLOEXEC / SOCK_NONBLOCK are Linux extensions to socket(2); darwin
 * has no equivalent flags-on-socket-create. Define as 0 so the bit-or
 * compiles; the atomic semantics are lost — call sites that need them
 * must follow up with fcntl(F_SETFD, FD_CLOEXEC) / fcntl(F_SETFL, O_NONBLOCK).
 * TODO: do that fcntl postwork in yos_socketpair on darwin. */
#  ifndef SOCK_CLOEXEC
#    define SOCK_CLOEXEC 0
#  endif
#  ifndef SOCK_NONBLOCK
#    define SOCK_NONBLOCK 0
#  endif
#endif
#include <sys/socket.h>
#include <sys/stat.h>     /* umask */
#include <sys/uio.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <poll.h>
#include <glob.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "platform.h"
#include "impl/errno_helpers.h"
#include "impl/libc/posix-internal.h"

extern int  yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);
extern int  yos_fd_get  (struct yos_exec_ctx *ctx, int wasm_fd);
extern void yos_fd_close(struct yos_exec_ctx *ctx, int wasm_fd);
extern const char *yos_path_resolve(struct yos_exec_ctx *ctx, const char *p);
extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);
extern void     yos_free  (struct yos_exec_ctx *ctx, uint32_t off);

/* Validated [offset, offset+len) range → host pointer. Mirrors the
 * same-named helper in impl/io/io-internal.h. Returns NULL when the
 * range falls outside wasm memory, when the 64-bit end overflows,
 * OR when offset is 0 with len>0 (guest NULL pointer + intent to
 * write — that's EFAULT).
 *
 * Zero-length calls (len==0) succeed regardless of offset value, so
 * POSIX `send(fd, NULL, 0, ...)` / `recv(fd, NULL, 0, ...)` etc. do
 * not EFAULT. Returns a non-NULL host pointer (validated against
 * memory_size so we don't leak a wild pointer for an out-of-range
 * offset). */
static inline void *posix_wptr_range(struct yos_exec_ctx *ctx, uint32_t offset,
                                     uint64_t len)
{
    if (len == 0)
        return (offset <= ctx->memory_size) ? (ctx->memory + offset) : NULL;
    if (offset == 0) return NULL;
    if (offset >= ctx->memory_size) return NULL;
    if ((uint64_t)offset + len > (uint64_t)ctx->memory_size) return NULL;
    return ctx->memory + offset;
}

/* ── fd-remapping passthroughs ────────────────────────────────────── */

int32_t yos_dup(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    ydebug("dup(wfd=%d hfd=%d)\n", wfd, hfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int new_hfd = dup(hfd);
    if (new_hfd < 0) {
        ydebug("dup(wfd=%d hfd=%d) host dup failed: %s\n",
               wfd, hfd, strerror(errno));
        return yos_errno_neg(ctx, errno);
    }
    int new_wfd = yos_fd_alloc(ctx, new_hfd);
    if (new_wfd < 0) { close(new_hfd); return yos_errno_neg(ctx, EMFILE); }
    /* Propagate recorded path. fts(3) under find(1) opens "." into one
     * fd then dups it into another for stash-and-restore; without this
     * the dup'd fd has no path, so a later fchdir on it can't update
     * ctx->cwd. */
    if (ctx->fd_paths[wfd])
        ctx->fd_paths[new_wfd] = strdup(ctx->fd_paths[wfd]);
    ydebug("dup(wfd=%d hfd=%d) -> new_wfd=%d new_hfd=%d\n",
           wfd, hfd, new_wfd, new_hfd);
    return new_wfd;
}

int32_t yos_isatty(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) { errno = EBADF; ydebug("isatty(wfd=%d) -> 0 (EBADF)\n", wfd); return 0; }
    /* Fake-PTY recognition. tvOS sandboxed posix_openpt failed, so
     * impl/pty.c gave the caller socketpair fds. host isatty() on a
     * socket returns 0 — but the wasm guest (zsh) really IS hooked up
     * to what is semantically a terminal pair. Without saying yes
     * here zsh never enters interactive mode (no prompt, no echo). */
    extern int yos_pty_is_pty_fd(int hfd);
    if (yos_pty_is_pty_fd(hfd)) {
        ydebug("isatty(wfd=%d hfd=%d) -> 1 (fake PTY)\n", wfd, hfd);
        return 1;
    }
    int r = yos_plat_isatty(hfd);
    ydebug("isatty(wfd=%d hfd=%d) -> %d\n", wfd, hfd, r);
    return r;
}

/* yos_getsockname — translate host fd, then convert host's Linux
 * sockaddr layout (sa_family uint16 @0) to wasm's FreeBSD layout
 * (sa_len uint8 @0, sa_family uint8 @1). socklen_t is 4 bytes both
 * sides — the wasm slot is fine. nvim/libuv's uv_guess_handle reads
 * ss_family at FreeBSD offset 1 to detect AF_UNIX spawn pipes; without
 * the conversion it sees 0 (the high byte of Linux's family field),
 * decides the fd is UV_UNKNOWN_HANDLE and asserts in stream_init. */
int32_t yos_getsockname(struct yos_exec_ctx *ctx, int32_t wfd,
                        uint32_t addr_off, uint32_t addrlen_off)
{
    int hfd = yos_fd_get(ctx, wfd);
    ydebug("getsockname(wfd=%d hfd=%d)\n", wfd, hfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    if (!posix_wptr_range(ctx, addrlen_off, 4))
        return yos_errno_neg(ctx, EFAULT);
    uint32_t *addrlen_p = (uint32_t *)(ctx->memory + addrlen_off);
    socklen_t cap = (socklen_t)*addrlen_p;
    if (cap > 256) cap = 256;  /* cap; libuv only needs the family */
    if (!posix_wptr_range(ctx, addr_off, cap))
        return yos_errno_neg(ctx, EFAULT);
    uint8_t host_buf[256];
    socklen_t host_len = cap;
    if (getsockname(hfd, (struct sockaddr *)host_buf, &host_len) < 0) {
        ydebug("getsockname host failed: %s\n", strerror(errno));
        return yos_errno_neg(ctx, errno);
    }
    /* Decode the host family. Linux: sa_family is uint16_t at offset
     * 0. BSD-lineage hosts (darwin/FreeBSD) put sa_len uint8 at 0,
     * sa_family uint8 at 1. The wrong decoding gave nvim's
     * uv_guess_handle ss_family = sa_len (e.g. 16) on darwin instead
     * of AF_UNIX, classifying the IPC socketpair end as
     * UV_UNKNOWN_HANDLE — root cause of "ch 1 was closed by the
     * client" in tmp/nvim-runtime-issues.md. */
    uint16_t host_fam = read_host_sa_family(host_buf);
    /* Re-emit FreeBSD shape: sa_len, sa_family[, sa_data...]. */
    uint8_t *w = ctx->memory + addr_off;
    socklen_t out = host_len < cap ? host_len : cap;
    if (out >= 2) {
        w[0] = (uint8_t)out;          /* sa_len */
        w[1] = (uint8_t)(host_fam & 0xff); /* sa_family */
        if (out > 2)
            memcpy(w + 2, host_buf + 2, out - 2);
    }
    *addrlen_p = (uint32_t)host_len;
    ydebug("getsockname -> family=%u out_len=%u\n",
           (unsigned)host_fam, (unsigned)out);
    return 0;
}

/* SOL_SOCKET + SO_TYPE constants differ between FreeBSD and Linux.
 *   FreeBSD: SOL_SOCKET=0xffff, SO_TYPE=0x1008, SO_ERROR=0x1007,
 *            SO_REUSEADDR=0x4, SO_KEEPALIVE=0x8, SO_BROADCAST=0x20,
 *            SO_LINGER=0x80, SO_SNDBUF=0x1001, SO_RCVBUF=0x1002,
 *            SO_SNDLOWAT=0x1003, SO_RCVLOWAT=0x1004, SO_SNDTIMEO=0x1005,
 *            SO_RCVTIMEO=0x1006, SO_OOBINLINE=0x100, SO_ACCEPTCONN=0x2.
 *   Linux:   SOL_SOCKET=1,      SO_TYPE=3,      SO_ERROR=4,
 *            SO_REUSEADDR=2, SO_KEEPALIVE=9, SO_BROADCAST=6,
 *            SO_LINGER=13, SO_SNDBUF=7, SO_RCVBUF=8,
 *            SO_SNDLOWAT=19, SO_RCVLOWAT=18, SO_SNDTIMEO=21,
 *            SO_RCVTIMEO=20, SO_OOBINLINE=10, SO_ACCEPTCONN=30.
 * Only the few that nvim/libuv actually exercise are translated below;
 * unknown options pass through and may EINVAL. */
static int sol_fb_to_lx(int level)
{
    if (level == 0xffff) return SOL_SOCKET;
    return level;
}
static int soopt_fb_to_lx(int level, int opt)
{
    if (level == 0xffff) {
        /* SOL_SOCKET options — same mapping table. */
        switch (opt) {
        case 0x1008: return SO_TYPE;
        case 0x1007: return SO_ERROR;
        case 0x0004: return SO_REUSEADDR;
        case 0x0008: return SO_KEEPALIVE;
        case 0x0020: return SO_BROADCAST;
        case 0x0080: return SO_LINGER;
        case 0x1001: return SO_SNDBUF;
        case 0x1002: return SO_RCVBUF;
        case 0x1005: return SO_SNDTIMEO;
        case 0x1006: return SO_RCVTIMEO;
        case 0x0100: return SO_OOBINLINE;
        case 0x0002: return SO_ACCEPTCONN;
        default:     return opt;
        }
    }
    /* IPPROTO_IP = 0. The option numbers diverge between FreeBSD and
     * Linux. ssh's connect path calls setsockopt(IPPROTO_IP, IP_TOS=3
     * (FreeBSD), 0x10) — on Linux that opt number is IP_HDRINCL, which
     * silently rejects the call and ssh prints the noisy
     * "setsockopt socket N IP_TOS 16: No message of desired type"
     * warning. Translate the handful of IPv4 options ssh actually
     * sets. Darwin/BSD hosts match FreeBSD, so the table is no-op
     * there. */
    if (level == 0 /* IPPROTO_IP */) {
#if defined(__linux__)
        switch (opt) {
        case 1:  return 4;   /* FB IP_OPTIONS    → LX IP_OPTIONS    (=4) */
        case 2:  return 3;   /* FB IP_HDRINCL    → LX IP_HDRINCL    (=3) */
        case 3:  return 1;   /* FB IP_TOS        → LX IP_TOS        (=1) */
        case 4:  return 2;   /* FB IP_TTL        → LX IP_TTL        (=2) */
        case 5:  return 6;   /* FB IP_RECVOPTS   → LX IP_RECVOPTS   (=6) */
        case 6:  return 7;   /* FB IP_RECVRETOPTS→ LX IP_RETOPTS    (=7) */
        case 7:  return 8;   /* FB IP_RECVDSTADDR→ LX IP_PKTINFO    (=8, closest) */
        case 9:  return 32;  /* FB IP_MULTICAST_IF  → LX IP_MULTICAST_IF (=32) */
        case 10: return 33;  /* FB IP_MULTICAST_TTL → LX IP_MULTICAST_TTL (=33) */
        case 11: return 34;  /* FB IP_MULTICAST_LOOP→ LX IP_MULTICAST_LOOP (=34) */
        case 12: return 35;  /* FB IP_ADD_MEMBERSHIP → LX IP_ADD_MEMBERSHIP (=35) */
        case 13: return 36;  /* FB IP_DROP_MEMBERSHIP→ LX IP_DROP_MEMBERSHIP (=36) */
        default: return opt;
        }
#else
        /* darwin / FreeBSD: same numbering as the FreeBSD wasm guest. */
        return opt;
#endif
    }
    return opt;
}

int32_t yos_getsockopt(struct yos_exec_ctx *ctx, int32_t wfd, int32_t level,
                       int32_t opt, uint32_t valbuf, uint32_t lenptr)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int hlevel = sol_fb_to_lx(level);
    int hopt = soopt_fb_to_lx(level, opt);
    void *vbuf = ctx->memory + valbuf;
    socklen_t *lp = (socklen_t *)(ctx->memory + lenptr);
    int r = getsockopt(hfd, hlevel, hopt, vbuf, lp);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_setsockopt(struct yos_exec_ctx *ctx, int32_t wfd, int32_t level,
                       int32_t opt, uint32_t valbuf, uint32_t valbuflen)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int hlevel = sol_fb_to_lx(level);
    int hopt = soopt_fb_to_lx(level, opt);
    const void *vbuf = ctx->memory + valbuf;
    int r = setsockopt(hfd, hlevel, hopt, vbuf, valbuflen);
    return yos_errno_check(ctx, (int32_t)r);
}

extern int sock_type_fb_to_lx_fwd(int t);  /* defined below */

int32_t yos_socket(struct yos_exec_ctx *ctx, int32_t domain, int32_t type, int32_t protocol)
{
    int htype = sock_type_fb_to_lx_fwd(type);
    int hfd = socket(domain, htype, protocol);
    if (hfd < 0) return yos_errno_neg(ctx, errno);
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { yos_plat_close(hfd); return yos_errno_neg(ctx, EMFILE); }
    return wfd;
}

/* Socket-side bridges that need wfd → hfd translation. The auto-
 * bridge for these passes the wasm fd straight through to the host
 * which sees an unrelated fd (or EBADF) — that's what makes the
 * coverage probe's `bind(127.0.0.1:0)` and `listen()` fail with
 * errno=9 (EBADF). */
/* FreeBSD sockaddr_in (and sockaddr_in6) put a 1-byte sin_len at
 * offset 0 and sa_family at offset 1. Linux drops sin_len and uses
 * a 2-byte sin_family at offset 0. Without translation, the host
 * bind/connect/sendto see family=0x0002 (sin_len=2 in the low
 * byte, AF_INET=2 in the high byte → 0x0202, an unknown family)
 * and silently fail. Same for sockaddr_in6 (family always at
 * offset 0/1 regardless, but sin6_len needs stripping). */

int32_t yos_bind(struct yos_exec_ctx *ctx, int32_t fd, uint32_t addr_off,
                 uint32_t addrlen)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (addr_off >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    /* Make a host-shape copy of the address and convert in-place. */
    uint8_t hostbuf[256];
    if (addrlen > sizeof(hostbuf)) return yos_errno_neg(ctx, EINVAL);
    memcpy(hostbuf, ctx->memory + addr_off, addrlen);
    freebsd_sockaddr_to_host(hostbuf, (socklen_t)addrlen);
    return yos_errno_check(ctx,
        bind(hfd, (struct sockaddr *)hostbuf, (socklen_t)addrlen));
}

int32_t yos_listen(struct yos_exec_ctx *ctx, int32_t fd, int32_t backlog)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    return yos_errno_check(ctx, listen(hfd, backlog));
}

int32_t yos_connect(struct yos_exec_ctx *ctx, int32_t fd, uint32_t addr_off,
                    uint32_t addrlen)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    uint8_t hostbuf[256];
    if (addrlen > sizeof(hostbuf)) return yos_errno_neg(ctx, EINVAL);
    if (!posix_wptr_range(ctx, addr_off, addrlen))
        return yos_errno_neg(ctx, EFAULT);
    memcpy(hostbuf, ctx->memory + addr_off, addrlen);
    freebsd_sockaddr_to_host(hostbuf, (socklen_t)addrlen);
    return yos_errno_check(ctx,
        connect(hfd, (struct sockaddr *)hostbuf, (socklen_t)addrlen));
}

int32_t yos_accept(struct yos_exec_ctx *ctx, int32_t fd, uint32_t addr_off,
                   uint32_t addrlen_off)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    socklen_t hlen = 0;
    socklen_t *hlen_p = NULL;
    struct sockaddr *haddr = NULL;
    if (addr_off && addrlen_off) {
        /* Read *addrlen first so we can validate the addr buffer range
         * before the host kernel writes into it. */
        if (!posix_wptr_range(ctx, addrlen_off, 4))
            return yos_errno_neg(ctx, EFAULT);
        hlen = (socklen_t)*(uint32_t *)(ctx->memory + addrlen_off);
        if (!posix_wptr_range(ctx, addr_off, hlen))
            return yos_errno_neg(ctx, EFAULT);
        hlen_p = &hlen;
        haddr  = (struct sockaddr *)(ctx->memory + addr_off);
    }
    int newhfd = accept(hfd, haddr, hlen_p);
    if (newhfd < 0) return yos_errno_neg(ctx, errno);
    if (addrlen_off) *(uint32_t *)(ctx->memory + addrlen_off) = (uint32_t)hlen;
    int newwfd = yos_fd_alloc(ctx, newhfd);
    if (newwfd < 0) { close(newhfd); return yos_errno_neg(ctx, EMFILE); }
    return newwfd;
}

ssize_t yos_send(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf_off,
                 uint32_t len, int32_t flags)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    void *p = posix_wptr_range(ctx, buf_off, len);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    ssize_t n = send(hfd, p, len, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

ssize_t yos_recv(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf_off,
                 uint32_t len, int32_t flags)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    void *p = posix_wptr_range(ctx, buf_off, len);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    ssize_t n = recv(hfd, p, len, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

ssize_t yos_sendto(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf_off,
                   uint32_t len, int32_t flags, uint32_t dst_off,
                   uint32_t dst_len)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    void *p = posix_wptr_range(ctx, buf_off, len);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    const struct sockaddr *dst = NULL;
    if (dst_off) {
        if (!posix_wptr_range(ctx, dst_off, dst_len))
            return yos_errno_neg(ctx, EFAULT);
        dst = (const struct sockaddr *)(ctx->memory + dst_off);
    }
    ssize_t n = sendto(hfd, p, len, flags, dst, (socklen_t)dst_len);
    return yos_errno_check(ctx, (int32_t)n);
}

ssize_t yos_recvfrom(struct yos_exec_ctx *ctx, int32_t fd, uint32_t buf_off,
                     uint32_t len, int32_t flags, uint32_t src_off,
                     uint32_t srclen_off)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    void *p = posix_wptr_range(ctx, buf_off, len);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    socklen_t hlen = 0;
    socklen_t *hlen_p = NULL;
    struct sockaddr *src = NULL;
    if (src_off && srclen_off) {
        if (!posix_wptr_range(ctx, srclen_off, 4))
            return yos_errno_neg(ctx, EFAULT);
        hlen = (socklen_t)*(uint32_t *)(ctx->memory + srclen_off);
        if (!posix_wptr_range(ctx, src_off, hlen))
            return yos_errno_neg(ctx, EFAULT);
        hlen_p = &hlen;
        src    = (struct sockaddr *)(ctx->memory + src_off);
    }
    ssize_t n = recvfrom(hfd, p, len, flags, src, hlen_p);
    if (n < 0) return yos_errno_neg(ctx, errno);
    if (srclen_off) *(uint32_t *)(ctx->memory + srclen_off) = (uint32_t)hlen;
    return n;
}

int32_t yos_shutdown(struct yos_exec_ctx *ctx, int32_t fd, int32_t how)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    return yos_errno_check(ctx, shutdown(hfd, how));
}

/* ── sendmsg / recvmsg ─────────────────────────────────────────────
 *
 * struct msghdr is pointer-heavy and the FreeBSD-i386 layout differs
 * from the host's, so the auto-bridge can't handle it: it passed the
 * wasm fd straight to the host call and treated the guest msghdr (whose
 * msg_name / msg_iov / msg_control are GUEST offsets) as a host struct.
 *
 * FreeBSD-i386 struct msghdr — seven 4-byte fields, 28 bytes total:
 *   0  msg_name (ptr)       4  msg_namelen (socklen_t)
 *   8  msg_iov (ptr)       12  msg_iovlen (int)
 *  16  msg_control (ptr)   20  msg_controllen (socklen_t)
 *  24  msg_flags (int)
 * FreeBSD struct iovec: iov_base @0, iov_len @4 (both 4 bytes).
 * FreeBSD struct cmsghdr: cmsg_len @0 (socklen_t), cmsg_level @4,
 *   cmsg_type @8; data at offset 12, cmsg objects aligned to 4 bytes.
 *
 * SOL_SOCKET differs between the ABIs (FreeBSD 0xffff, host typically 1);
 * SCM_RIGHTS is 0x01 on both. fds carried in an SCM_RIGHTS control
 * message are wasm fds on the guest side and must be translated to/from
 * host fds — this is exactly how tmux/imsg hands the pty and stdio fds
 * to the server. */
enum {
    YOS_FBMSG_NAME = 0, YOS_FBMSG_NAMELEN = 4,
    YOS_FBMSG_IOV = 8, YOS_FBMSG_IOVLEN = 12,
    YOS_FBMSG_CONTROL = 16, YOS_FBMSG_CONTROLLEN = 20,
    YOS_FBMSG_FLAGS = 24, YOS_FBMSG_SIZE = 28,
};
#define YOS_FBSD_SOL_SOCKET 0xffff
#define YOS_FBSD_SCM_RIGHTS 0x01
/* imsg (tmux's client/server transport) batches many buffers into one
 * sendmsg, up to the host IOV_MAX. Match that so large message bursts
 * aren't rejected with EINVAL. 1024 × sizeof(struct iovec) = 16 KiB of
 * stack — acceptable for the fork-thread stacks. */
#ifdef IOV_MAX
#define YOS_MSG_MAX_IOV     IOV_MAX
#else
#define YOS_MSG_MAX_IOV     1024
#endif
#define YOS_MSG_MAX_FDS     64
/* Generous host control buffer: room for the SCM_RIGHTS fds we cap at. */
#define YOS_MSG_CTRL_BUF    (CMSG_SPACE(YOS_MSG_MAX_FDS * sizeof(int)))

static inline uint32_t yos_g_u32(struct yos_exec_ctx *ctx, uint32_t off)
{
    return *(uint32_t *)(ctx->memory + off);
}
static inline void yos_g_set_u32(struct yos_exec_ctx *ctx, uint32_t off,
                                 uint32_t value)
{
    *(uint32_t *)(ctx->memory + off) = value;
}

/* FreeBSD cmsg alignment is 4 bytes on i386. */
static inline uint32_t yos_fbsd_cmsg_align(uint32_t n) { return (n + 3u) & ~3u; }

/* Build a host iovec array from the guest msg_iov. Returns the count, or
 * -1 on a bad pointer. */
static int yos_msg_build_iov(struct yos_exec_ctx *ctx, uint32_t iov_off,
                             uint32_t iovlen, struct iovec *hiov)
{
    if (iovlen > YOS_MSG_MAX_IOV) return -1;
    for (uint32_t i = 0; i < iovlen; i++) {
        uint32_t entry = iov_off + i * 8u;       /* FreeBSD iovec = 8 bytes */
        if (!posix_wptr_range(ctx, entry, 8)) return -1;
        uint32_t base = yos_g_u32(ctx, entry);
        uint32_t len  = yos_g_u32(ctx, entry + 4u);
        if (len && !posix_wptr_range(ctx, base, len)) return -1;
        hiov[i].iov_base = len ? (void *)(ctx->memory + base) : NULL;
        hiov[i].iov_len  = len;
    }
    return (int)iovlen;
}

ssize_t yos_sendmsg(struct yos_exec_ctx *ctx, int32_t fd, uint32_t msg_off,
                    int32_t flags)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (!posix_wptr_range(ctx, msg_off, YOS_FBMSG_SIZE))
        return yos_errno_neg(ctx, EFAULT);

    uint32_t name_off   = yos_g_u32(ctx, msg_off + YOS_FBMSG_NAME);
    uint32_t name_len   = yos_g_u32(ctx, msg_off + YOS_FBMSG_NAMELEN);
    uint32_t iov_off    = yos_g_u32(ctx, msg_off + YOS_FBMSG_IOV);
    uint32_t iov_len    = yos_g_u32(ctx, msg_off + YOS_FBMSG_IOVLEN);
    uint32_t ctrl_off   = yos_g_u32(ctx, msg_off + YOS_FBMSG_CONTROL);
    uint32_t ctrl_len   = yos_g_u32(ctx, msg_off + YOS_FBMSG_CONTROLLEN);

    struct iovec hiov[YOS_MSG_MAX_IOV];
    int niov = yos_msg_build_iov(ctx, iov_off, iov_len, hiov);
    if (niov < 0) return yos_errno_neg(ctx, EINVAL);

    struct msghdr hmsg;
    memset(&hmsg, 0, sizeof(hmsg));
    hmsg.msg_iov = hiov;
    hmsg.msg_iovlen = (size_t)niov;

    /* Optional destination address (unusual for connected sockets). */
    uint8_t namebuf[256];
    if (name_off && name_len && name_len <= sizeof(namebuf)) {
        if (!posix_wptr_range(ctx, name_off, name_len))
            return yos_errno_neg(ctx, EFAULT);
        memcpy(namebuf, ctx->memory + name_off, name_len);
        freebsd_sockaddr_to_host(namebuf, (socklen_t)name_len);
        hmsg.msg_name = namebuf;
        hmsg.msg_namelen = (socklen_t)name_len;
    }

    /* Translate the SCM_RIGHTS control data (wasm fds → host fds). We
     * only understand SOL_SOCKET/SCM_RIGHTS; anything else is dropped
     * (the kernel would reject unknown ancillary data anyway). */
    uint8_t hctrl[YOS_MSG_CTRL_BUF];
    if (ctrl_off && ctrl_len >= 12) {
        if (!posix_wptr_range(ctx, ctrl_off, ctrl_len))
            return yos_errno_neg(ctx, EFAULT);
        uint32_t pos = 0;
        size_t hpos = 0;
        while (pos + 12u <= ctrl_len) {
            uint32_t cmsg_len   = yos_g_u32(ctx, ctrl_off + pos);
            int32_t  cmsg_level = (int32_t)yos_g_u32(ctx, ctrl_off + pos + 4u);
            int32_t  cmsg_type  = (int32_t)yos_g_u32(ctx, ctrl_off + pos + 8u);
            if (cmsg_len < 12u || pos + cmsg_len > ctrl_len) break;
            if (cmsg_level == YOS_FBSD_SOL_SOCKET &&
                cmsg_type == YOS_FBSD_SCM_RIGHTS) {
                uint32_t nfds = (cmsg_len - 12u) / 4u;
                if (nfds > YOS_MSG_MAX_FDS) nfds = YOS_MSG_MAX_FDS;
                struct cmsghdr *hc = (struct cmsghdr *)(hctrl + hpos);
                hc->cmsg_level = SOL_SOCKET;
                hc->cmsg_type  = SCM_RIGHTS;
                hc->cmsg_len   = CMSG_LEN(nfds * sizeof(int));
                int *hfds = (int *)CMSG_DATA(hc);
                for (uint32_t i = 0; i < nfds; i++) {
                    uint32_t wfd = yos_g_u32(ctx, ctrl_off + pos + 12u + i * 4u);
                    int real = yos_fd_get(ctx, (int32_t)wfd);
                    hfds[i] = real;          /* pass the host fd to the peer */
                }
                hpos += CMSG_SPACE(nfds * sizeof(int));
            }
            pos += yos_fbsd_cmsg_align(cmsg_len);
        }
        if (hpos) {
            hmsg.msg_control = hctrl;
            hmsg.msg_controllen = (socklen_t)hpos;
        }
    }

    ssize_t n = sendmsg(hfd, &hmsg, flags);
    return yos_errno_check(ctx, (int32_t)n);
}

ssize_t yos_recvmsg(struct yos_exec_ctx *ctx, int32_t fd, uint32_t msg_off,
                    int32_t flags)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (!posix_wptr_range(ctx, msg_off, YOS_FBMSG_SIZE))
        return yos_errno_neg(ctx, EFAULT);

    uint32_t iov_off  = yos_g_u32(ctx, msg_off + YOS_FBMSG_IOV);
    uint32_t iov_len  = yos_g_u32(ctx, msg_off + YOS_FBMSG_IOVLEN);
    uint32_t ctrl_off = yos_g_u32(ctx, msg_off + YOS_FBMSG_CONTROL);
    uint32_t ctrl_len = yos_g_u32(ctx, msg_off + YOS_FBMSG_CONTROLLEN);

    struct iovec hiov[YOS_MSG_MAX_IOV];
    int niov = yos_msg_build_iov(ctx, iov_off, iov_len, hiov);
    if (niov < 0) return yos_errno_neg(ctx, EINVAL);

    struct msghdr hmsg;
    memset(&hmsg, 0, sizeof(hmsg));
    hmsg.msg_iov = hiov;
    hmsg.msg_iovlen = (size_t)niov;

    uint8_t hctrl[YOS_MSG_CTRL_BUF];
    int want_ctrl = (ctrl_off && ctrl_len >= 12);
    if (want_ctrl) {
        if (!posix_wptr_range(ctx, ctrl_off, ctrl_len))
            return yos_errno_neg(ctx, EFAULT);
        hmsg.msg_control = hctrl;
        hmsg.msg_controllen = sizeof(hctrl);
    }

    ssize_t n = recvmsg(hfd, &hmsg, flags);
    if (n < 0) return yos_errno_neg(ctx, errno);

    /* Rebuild the guest control buffer from the host cmsgs, translating
     * any SCM_RIGHTS host fds back into freshly-allocated wasm fds. */
    uint32_t gpos = 0;
    if (want_ctrl) {
        for (struct cmsghdr *hc = CMSG_FIRSTHDR(&hmsg); hc != NULL;
             hc = CMSG_NXTHDR(&hmsg, hc)) {
            if (hc->cmsg_level == SOL_SOCKET && hc->cmsg_type == SCM_RIGHTS) {
                uint32_t nfds = (uint32_t)((hc->cmsg_len - CMSG_LEN(0)) /
                                           sizeof(int));
                uint32_t need = 12u + nfds * 4u;
                if (gpos + need > ctrl_len) break;
                yos_g_set_u32(ctx, ctrl_off + gpos, need);              /* cmsg_len */
                yos_g_set_u32(ctx, ctrl_off + gpos + 4u, YOS_FBSD_SOL_SOCKET);
                yos_g_set_u32(ctx, ctrl_off + gpos + 8u, YOS_FBSD_SCM_RIGHTS);
                int *hfds = (int *)CMSG_DATA(hc);
                for (uint32_t i = 0; i < nfds; i++) {
                    int32_t wfd = yos_fd_alloc(ctx, hfds[i]);
                    if (wfd < 0) { close(hfds[i]); wfd = -1; }
                    yos_g_set_u32(ctx, ctrl_off + gpos + 12u + i * 4u,
                                  (uint32_t)wfd);
                }
                gpos += yos_fbsd_cmsg_align(need);
            }
        }
    }
    if (ctrl_off || ctrl_len)
        yos_g_set_u32(ctx, msg_off + YOS_FBMSG_CONTROLLEN, gpos);
    /* Report truncation flags to the guest (MSG_TRUNC/MSG_CTRUNC share
     * their low-bit values across the ABIs). */
    yos_g_set_u32(ctx, msg_off + YOS_FBMSG_FLAGS, (uint32_t)hmsg.msg_flags);
    return n;
}

/* ── glob / globfree ───────────────────────────────────────────────
 *
 * The auto-bridge can't handle glob(3): it turns the NULL errfunc arg
 * into ctx->memory+0 (a non-NULL host pointer), which host glob calls on
 * the first read error → jump to a wild address → SIGSEGV. It also lets
 * host glob fill the guest glob_t with HOST-allocated gl_pathv pointers
 * the guest can't use.
 *
 * Hand bridge: run host glob with errfunc forced NULL, then copy the
 * matched paths into guest memory (a wasm-offset gl_pathv array + a
 * yos_malloc'd copy of each string), and write the FreeBSD-shape glob_t.
 * The glob flag bits and return codes differ between FreeBSD and the host
 * libc, so both are remapped. tmux globs its config paths this way.
 *
 * FreeBSD glob_t (44 B): gl_pathc@0, gl_matchc@4, gl_offs@8, gl_flags@12,
 * gl_pathv@16, then five alt-function pointers we never populate. */
enum {
    YOS_FBGLOB_PATHC = 0, YOS_FBGLOB_MATCHC = 4, YOS_FBGLOB_OFFS = 8,
    YOS_FBGLOB_FLAGS = 12, YOS_FBGLOB_PATHV = 16, YOS_FBGLOB_SIZE = 44,
};
/* FreeBSD glob(3) flag bits (sys glob.h). */
enum {
    FB_GLOB_APPEND = 0x0001, FB_GLOB_DOOFFS = 0x0002, FB_GLOB_ERR = 0x0004,
    FB_GLOB_MARK = 0x0008, FB_GLOB_NOCHECK = 0x0010, FB_GLOB_NOSORT = 0x0020,
    FB_GLOB_NOESCAPE = 0x2000, FB_GLOB_BRACE = 0x0080, FB_GLOB_NOMAGIC = 0x0100,
    FB_GLOB_TILDE = 0x0800,
};
/* FreeBSD glob(3) return codes. */
enum { FB_GLOB_NOSPACE = -1, FB_GLOB_ABORTED = -2, FB_GLOB_NOMATCH = -3 };

static int yos_glob_flags_g2h(int fbsd)
{
    int h = 0;
    if (fbsd & FB_GLOB_ERR)      h |= GLOB_ERR;
    if (fbsd & FB_GLOB_MARK)     h |= GLOB_MARK;
    if (fbsd & FB_GLOB_NOSORT)   h |= GLOB_NOSORT;
    if (fbsd & FB_GLOB_NOCHECK)  h |= GLOB_NOCHECK;
    if (fbsd & FB_GLOB_NOESCAPE) h |= GLOB_NOESCAPE;
#ifdef GLOB_BRACE
    if (fbsd & FB_GLOB_BRACE)    h |= GLOB_BRACE;
#endif
#ifdef GLOB_NOMAGIC
    if (fbsd & FB_GLOB_NOMAGIC)  h |= GLOB_NOMAGIC;
#endif
#ifdef GLOB_TILDE
    if (fbsd & FB_GLOB_TILDE)    h |= GLOB_TILDE;
#endif
    /* GLOB_APPEND/GLOB_DOOFFS/GLOB_ALTDIRFUNC are intentionally dropped —
     * we always build a fresh result and never use alt dir functions. */
    return h;
}

static int yos_glob_rc_h2g(int host_rc)
{
    if (host_rc == 0) return 0;
#ifdef GLOB_NOSPACE
    if (host_rc == GLOB_NOSPACE) return FB_GLOB_NOSPACE;
#endif
#ifdef GLOB_ABORTED
    if (host_rc == GLOB_ABORTED) return FB_GLOB_ABORTED;
#endif
#ifdef GLOB_NOMATCH
    if (host_rc == GLOB_NOMATCH) return FB_GLOB_NOMATCH;
#endif
    return FB_GLOB_NOMATCH;
}

int32_t yos_glob(struct yos_exec_ctx *ctx, uint32_t pat_off, int32_t flags,
                 uint32_t errfunc_off, uint32_t glob_off)
{
    (void)errfunc_off;   /* never call a guest function pointer from the host */
    if (!pat_off || pat_off >= ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);
    if (!posix_wptr_range(ctx, glob_off, YOS_FBGLOB_SIZE))
        return yos_errno_neg(ctx, EFAULT);
    const char *pattern = (const char *)(ctx->memory + pat_off);

    glob_t hg;
    memset(&hg, 0, sizeof(hg));
    int host_rc = glob(pattern, yos_glob_flags_g2h(flags), NULL, &hg);

    uint32_t count = (host_rc == 0) ? (uint32_t)hg.gl_pathc : 0;

    /* gl_pathv: count path offsets + a trailing NULL slot. */
    uint32_t arr = yos_malloc(ctx, (count + 1u) * 4u);
    if (!arr) { globfree(&hg); return FB_GLOB_NOSPACE; }
    for (uint32_t i = 0; i < count; i++) {
        size_t len = strlen(hg.gl_pathv[i]) + 1u;
        uint32_t s = yos_malloc(ctx, (uint32_t)len);
        if (!s) { globfree(&hg); return FB_GLOB_NOSPACE; }
        memcpy(ctx->memory + s, hg.gl_pathv[i], len);
        yos_g_set_u32(ctx, arr + i * 4u, s);
    }
    yos_g_set_u32(ctx, arr + count * 4u, 0);

    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_PATHC, count);
    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_MATCHC, count);
    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_OFFS, 0);
    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_FLAGS, (uint32_t)flags);
    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_PATHV, arr);

    globfree(&hg);
    return yos_glob_rc_h2g(host_rc);
}

void yos_globfree(struct yos_exec_ctx *ctx, uint32_t glob_off)
{
    if (!posix_wptr_range(ctx, glob_off, YOS_FBGLOB_SIZE)) return;
    uint32_t count = yos_g_u32(ctx, glob_off + YOS_FBGLOB_PATHC);
    uint32_t arr   = yos_g_u32(ctx, glob_off + YOS_FBGLOB_PATHV);
    if (arr) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t s = yos_g_u32(ctx, arr + i * 4u);
            if (s) yos_free(ctx, s);
        }
        yos_free(ctx, arr);
    }
    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_PATHC, 0);
    yos_g_set_u32(ctx, glob_off + YOS_FBGLOB_PATHV, 0);
}

/* yos_select — fd_set translation across the wasm/host fd boundary.
 *
 * Auto-bridge passes fd_set bitmaps straight through. The bitmap is
 * indexed by FD NUMBER; the wasm guest's fd numbers (its fd_map slots
 * 0..YOS_FD_MAX) are NOT the host fd numbers (host kernel pipe / pty
 * / socket fds). Without translation, host select() looks for the
 * wrong fds, returns 0 or -1 immediately, and any program that uses
 * select to block (telnetd's I/O proxy, runsv's iopause, openssh's
 * channel loop) busy-spins eating CPU + getting no data.
 *
 * Translate by walking each wasm fd in [0, nfds), looking up its host
 * fd via yos_fd_get, and rebuilding the fd_set on the host side. After
 * host select returns, walk the host fd_set and translate back to wasm
 * fds for the guest. */
int32_t yos_select(struct yos_exec_ctx *ctx, int32_t nfds,
                   uint32_t r_off, uint32_t w_off, uint32_t e_off,
                   uint32_t to_off)
{
    if (nfds < 0 || nfds > YOS_FD_MAX) return yos_errno_neg(ctx, EINVAL);

    fd_set hr, hw, he;
    FD_ZERO(&hr); FD_ZERO(&hw); FD_ZERO(&he);
    int max_hfd = -1;

    /* The wasm guest's fd_set is FreeBSD i386 layout: a uint32 bitmap
     * indexed by fd number (bit fd%32 of word fd/32). Win32's host
     * fd_set is a different struct (fd_count + fd_array), so casting
     * the wasm-memory bytes to host fd_set* and calling host FD_ISSET
     * reads garbage. Probe the wasm bitmap by hand instead. */
    const uint8_t *gr_p = r_off ? (const uint8_t *)(ctx->memory + r_off) : NULL;
    const uint8_t *gw_p = w_off ? (const uint8_t *)(ctx->memory + w_off) : NULL;
    const uint8_t *ge_p = e_off ? (const uint8_t *)(ctx->memory + e_off) : NULL;
    #define YOS_WFDS_ISSET(p, fd) ((p) && ((p)[(fd)/8] & (1u << ((fd)%8))))

    /* Walk the guest sets, build a flat (wfd, hfd) list of fds we care
     * about, and populate the host sets. The h2w lookup is a linear
     * walk over this list — array indexing by hfd doesn't work on
     * Windows where fds are raw SOCKET handle integers that can lie
     * well outside any reasonable array bound. */
    struct { int wfd; int hfd; int in_r; int in_w; int in_e; } pairs[YOS_FD_MAX];
    int npairs = 0;

    for (int wfd = 0; wfd < nfds; wfd++) {
        int in_r = YOS_WFDS_ISSET(gr_p, wfd);
        int in_w = YOS_WFDS_ISSET(gw_p, wfd);
        int in_e = YOS_WFDS_ISSET(ge_p, wfd);
        if (!(in_r || in_w || in_e)) continue;
        int hfd = yos_fd_get(ctx, wfd);
        if (hfd < 0) continue;
        pairs[npairs].wfd = wfd;
        pairs[npairs].hfd = hfd;
        pairs[npairs].in_r = in_r;
        pairs[npairs].in_w = in_w;
        pairs[npairs].in_e = in_e;
        npairs++;
        if (in_r) FD_SET(hfd, &hr);
        if (in_w) FD_SET(hfd, &hw);
        if (in_e) FD_SET(hfd, &he);
        if (hfd > max_hfd) max_hfd = hfd;
    }

    /* struct timeval on FreeBSD wasm32 is 8 bytes (32-bit time_t +
     * 32-bit suseconds_t). On macOS/Linux x86_64 host it's 16 bytes
     * (64-bit time_t + 32-bit suseconds_t + 4 bytes padding).
     * Treating the wasm-memory bytes directly as `struct timeval *`
     * makes the host read 8 bytes of garbage as the high 32 bits of
     * tv_sec, often producing tv_sec = -1 → EINVAL from select.
     * Convert via the codegen-emitted cv_timeval_w2h. */
    extern void cv_timeval_w2h(struct timeval *h, const uint8_t *w);
    struct timeval host_tv;
    struct timeval *tv = NULL;
    if (to_off) {
        cv_timeval_w2h(&host_tv, ctx->memory + to_off);
        tv = &host_tv;
    }
    /* Pump pending signals BEFORE blocking. Without this, a signal
     * that arrived between the previous bridge exit and this select
     * entry stays unprocessed — the guest blocks in select() without
     * the wasm-side handler ever firing. tcpserver/telnetd are
     * select-loop daemons: kill <pid> sets target's sig_pending,
     * pthread_kill wakes the host thread with SIGUSR2 (EINTR returns
     * from host select), but the wasm handler is only invoked from
     * inside a yos_signal_pump call.
     *
     * Two pump call sites: (1) here before host select to catch
     * already-pending signals; (2) immediately after host select if
     * it returned -1+EINTR, so the next bridge call doesn't have to
     * wait for some other yield point to process the signal that
     * just woke us up. */
    extern void yos_signal_pump(struct yos_exec_ctx *);
    yos_signal_pump(ctx);
#ifdef _WIN32
    /* Windows select() only watches winsock SOCKETs — anonymous pipes
     * (msvcrt _pipe) and other CRT handles silently fail with
     * WSAENOTSOCK. Hand-roll a hybrid: convert the (wfd, hfd) list to
     * pollfds and call our own poll() shim (compat_libc.c) which
     * partitions sockets through WSAPoll and pipes through
     * PeekNamedPipe. The fd_set bits are reconstructed after. */
    int rc;
    {
        struct pollfd pfds[64];
        int pn = 0;
        for (int i = 0; i < npairs && pn < 64; i++) {
            short events = 0;
            if (pairs[i].in_r) events |= POLLIN;
            if (pairs[i].in_w) events |= POLLOUT;
            /* select's "exception" set has no clean POSIX poll
             * equivalent — POLLERR is reported unconditionally. */
            pfds[pn].fd = pairs[i].hfd;
            pfds[pn].events = events;
            pfds[pn].revents = 0;
            pn++;
        }
        int ms;
        if (!tv) ms = -1;
        else ms = (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
        rc = poll(pfds, pn, ms);
        if (rc >= 0) {
            FD_ZERO(&hr); FD_ZERO(&hw); FD_ZERO(&he);
            for (int i = 0; i < pn; i++) {
                int hfd = pfds[i].fd;
                if (pfds[i].revents & POLLIN)  FD_SET(hfd, &hr);
                if (pfds[i].revents & POLLOUT) FD_SET(hfd, &hw);
                if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                    FD_SET(hfd, &he);
            }
        }
    }
    int saved_errno = (rc < 0) ? errno : 0;
#else
    int rc = select(max_hfd + 1, &hr, &hw, &he, tv);
    int saved_errno = (rc < 0) ? errno : 0;
#endif
    if (rc < 0 && saved_errno == EINTR) {
        /* Drain the pending bitmask — the signal that interrupted us
         * needs to reach its wasm handler before the guest's libc
         * loops back into another select. SIGTERM with SIG_DFL will
         * pthread_exit() inside the pump and never return here. */
        yos_signal_pump(ctx);
    }
    if (rc < 0) {
        errno = saved_errno;
        return yos_errno_neg(ctx, errno);
    }

    /* Rebuild the guest fd_sets with only the wasm-fd bits set. The
     * wasm fd_set is the FreeBSD i386 bitmap; clear the requested-bit
     * area (16 uint32 words covers the whole 1024-fd set) and re-OR
     * in the bits for fds that came back ready. */
    uint8_t *gr_w = r_off ? (uint8_t *)(ctx->memory + r_off) : NULL;
    uint8_t *gw_w = w_off ? (uint8_t *)(ctx->memory + w_off) : NULL;
    uint8_t *ge_w = e_off ? (uint8_t *)(ctx->memory + e_off) : NULL;
    if (gr_w) memset(gr_w, 0, 128);
    if (gw_w) memset(gw_w, 0, 128);
    if (ge_w) memset(ge_w, 0, 128);
    #define YOS_WFDS_SET(p, fd) do { if (p) (p)[(fd)/8] |= (uint8_t)(1u << ((fd)%8)); } while (0)
    for (int i = 0; i < npairs; i++) {
        int hfd = pairs[i].hfd;
        int wfd = pairs[i].wfd;
        if (gr_w && FD_ISSET(hfd, &hr)) YOS_WFDS_SET(gr_w, wfd);
        if (gw_w && FD_ISSET(hfd, &hw)) YOS_WFDS_SET(gw_w, wfd);
        if (ge_w && FD_ISSET(hfd, &he)) YOS_WFDS_SET(ge_w, wfd);
    }
    #undef YOS_WFDS_SET
    #undef YOS_WFDS_ISSET
    return rc;
}

int32_t yos_getpeername(struct yos_exec_ctx *ctx, int32_t fd,
                        uint32_t addr_off, uint32_t addrlen_off)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return hfd;
    if (!addr_off || !addrlen_off) return yos_errno_neg(ctx, EFAULT);
    if (!posix_wptr_range(ctx, addrlen_off, 4)) return yos_errno_neg(ctx, EFAULT);
    socklen_t cap = (socklen_t)*(uint32_t *)(ctx->memory + addrlen_off);
    if (cap > 256) cap = 256;
    if (!posix_wptr_range(ctx, addr_off, cap))
        return yos_errno_neg(ctx, EFAULT);
    /* Fetch into a host-shaped scratch, then re-emit in FreeBSD layout
     * (sa_len byte 0, sa_family byte 1, sa_data byte 2+). Without this
     * step the guest reads sa_family from the wrong byte and the test's
     * sin_family / ss_family checks fail. */
    uint8_t host_buf[256];
    socklen_t host_len = cap;
    if (getpeername(hfd, (struct sockaddr *)host_buf, &host_len) < 0)
        return yos_errno_neg(ctx, errno);
    uint16_t host_fam = read_host_sa_family(host_buf);
    uint8_t *w = ctx->memory + addr_off;
    socklen_t out = host_len < cap ? host_len : cap;
    if (out >= 2) {
        w[0] = (uint8_t)out;
        w[1] = (uint8_t)(host_fam & 0xff);
        if (out > 2)
            memcpy(w + 2, host_buf + 2, out - 2);
    }
    *(uint32_t *)(ctx->memory + addrlen_off) = (uint32_t)host_len;
    return 0;
}

/* posix_fadvise / posix_fallocate — fd-taking; the auto-bridge
 * passed the wasm fd straight to host which then sees EBADF. They
 * return the error code as the function value (not via errno). */
int32_t yos_posix_fadvise(struct yos_exec_ctx *ctx, int32_t fd,
                          int64_t offset, int64_t len, int32_t advice)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return -hfd;  /* return positive error code */
    return yos_plat_posix_fadvise(hfd, (off_t)offset, (off_t)len, advice);
}

int32_t yos_posix_fallocate(struct yos_exec_ctx *ctx, int32_t fd,
                            int64_t offset, int64_t len)
{
    int hfd = yos_fd_get(ctx, fd);
    if (hfd < 0) return -hfd;
    return yos_plat_posix_fallocate(hfd, (off_t)offset, (off_t)len);
}

/* lpathconf — FreeBSD-specific; Linux only has pathconf (which
 * follows symlinks; lpathconf doesn't). Map to host lstat-then-
 * pathconf for the typical _PC_ACL_* / _PC_LINK_MAX queries. ls
 * calls lpathconf(name, _PC_ACL_NFS4) for every entry; without a
 * bridge it returned -1 with ENOSYS in wasm errno → ls's check
 * `errno != EINVAL` was true → ls warned `<name>: Invalid argument`
 * for every entry, then exited. Returning EINVAL (which is the
 * error Linux pathconf produces for ACL queries on filesystems
 * without ACL support) makes ls skip the warn cleanly. */
int32_t yos_lpathconf(struct yos_exec_ctx *ctx, uint32_t path_off, int32_t name)
{
    if (!path_off) return yos_errno_neg(ctx, EFAULT);
    const char *path = (const char *)(ctx->memory + path_off);
    /* Try host pathconf — works for many _PC_* values (LINK_MAX,
     * NAME_MAX, …). Linux has no lpathconf; pathconf follows
     * symlinks but for ls's use case that's fine. */
    errno = 0;
    long r = pathconf(path, name);
    if (r < 0) {
        /* Many FreeBSD-only _PC_* (NFS4 ACL, ACL_PATH_MAX) return -1
         * on Linux with EINVAL, which is exactly what ls expects to
         * mean "no ACLs". Set the wasm errno so ls's `errno != EINVAL`
         * check correctly skips the warning — without this the wasm
         * errno keeps whatever it was set to before the call (often
         * 0 from a prior `errno = 0` reset in fts_safe_readdir), and
         * ls prints "<name>: Undefined error: 0" / "<name>: Success"
         * for every entry it tries to ACL-probe. */
        return yos_errno_neg(ctx, errno ? errno : EINVAL);
    }
    return (int32_t)r;
}

#include <netdb.h>

/* getaddrinfo — auto-bridge stubs this with -ENOSYS. ssh, scp, sftp,
 * curl, anything that resolves a hostname needs it. We translate the
 * wasm-side hints struct, call host getaddrinfo, then build a wasm-
 * side `struct addrinfo` linked-list inside the wasm linear memory
 * via yos_malloc. ssh's freeaddrinfo is bridged separately to walk
 * and free that wasm-side list.
 *
 * FreeBSD wasm32 struct addrinfo (sizeof = 32):
 *   off  0: int  ai_flags
 *   off  4: int  ai_family
 *   off  8: int  ai_socktype
 *   off 12: int  ai_protocol
 *   off 16: socklen_t ai_addrlen   (4)
 *   off 20: char *ai_canonname     (4)
 *   off 24: struct sockaddr *ai_addr (4)
 *   off 28: struct addrinfo *ai_next (4)
 *
 * sockaddr_in is 16 bytes (FreeBSD: sin_len 1, sin_family 1, sin_port 2,
 * sin_addr 4, sin_zero[8]). The bridge writes sockaddr_in / in6 to the
 * wasm side in FreeBSD layout (sin_len byte then family byte).
 */

extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);

#define WASM_ADDRINFO_SZ 32u

static void host_sockaddr_to_freebsd(uint8_t *out, const struct sockaddr *src,
                                     socklen_t len)
{
    memcpy(out, src, len);
    /* FreeBSD wasm guest expects: sa_len byte @0, sa_family byte @1.
     * Linux host gives: sa_family uint16 little-endian @0/1. BSD-
     * lineage host (darwin, FreeBSD, *BSD) already matches the guest's
     * layout (sa_len @0, sa_family @1) so no rewrite is needed and
     * doing one corrupts sa_family. Read the family via the platform-
     * specific helper to get this right on both. */
    if (len >= 2) {
        uint16_t fam = read_host_sa_family(out);
        out[0] = (uint8_t)len;
        out[1] = (uint8_t)(fam & 0xff);
    }
}

/* AI_* / NI_* flag bits diverge between FreeBSD and Linux. Examples:
 *   FreeBSD NI_NUMERICHOST = 0x02 — Linux NI_NUMERICHOST = 0x01.
 *   FreeBSD NI_NUMERICSERV = 0x08 — Linux NI_NUMERICSERV = 0x02.
 * Without translation, ssh's `getnameinfo(..., NI_NUMERICHOST)` on a
 * Linux host runs a reverse DNS lookup instead and returns the
 * remote's FQDN where the dotted-quad IP should appear — visible as
 * "The authenticity of host 'macbook.main.misi.com (macbook.main.misi.com)'
 * can't be established." (the IP slot is the FQDN, not the IP).
 *
 * Darwin's NI_* / AI_* match FreeBSD's, so the translation is a no-op
 * there — return the bits unchanged. */
#if defined(__linux__)
/* FreeBSD bit values (from /usr/include/netdb.h on the FreeBSD guest). */
#define FB_NI_NOFQDN       0x01
#define FB_NI_NUMERICHOST  0x02
#define FB_NI_NAMEREQD     0x04
#define FB_NI_NUMERICSERV  0x08
#define FB_NI_DGRAM        0x10
#define FB_NI_NUMERICSCOPE 0x20

#define FB_AI_PASSIVE      0x0001
#define FB_AI_CANONNAME    0x0002
#define FB_AI_NUMERICHOST  0x0004
#define FB_AI_NUMERICSERV  0x0008
#define FB_AI_ALL          0x0100
#define FB_AI_V4MAPPED_CFG 0x0200
#define FB_AI_ADDRCONFIG   0x0400
#define FB_AI_V4MAPPED     0x0800

static int ni_flags_fb_to_lx(int f)
{
    int r = 0;
    if (f & FB_NI_NOFQDN)       r |= NI_NOFQDN;
    if (f & FB_NI_NUMERICHOST)  r |= NI_NUMERICHOST;
    if (f & FB_NI_NAMEREQD)     r |= NI_NAMEREQD;
    if (f & FB_NI_NUMERICSERV)  r |= NI_NUMERICSERV;
    if (f & FB_NI_DGRAM)        r |= NI_DGRAM;
    /* NI_NUMERICSCOPE: glibc accepts it as bit 4 (=32) on IPv6 scopes;
     * older glibc doesn't define it. Fall through silently if absent. */
#ifdef NI_NUMERICSCOPE
    if (f & FB_NI_NUMERICSCOPE) r |= NI_NUMERICSCOPE;
#endif
    return r;
}

static int ai_flags_fb_to_lx(int f)
{
    int r = 0;
    /* PASSIVE / CANONNAME / NUMERICHOST share bit values across both.
     * The rest (V4MAPPED, ALL, ADDRCONFIG, NUMERICSERV) don't. */
    if (f & FB_AI_PASSIVE)      r |= AI_PASSIVE;
    if (f & FB_AI_CANONNAME)    r |= AI_CANONNAME;
    if (f & FB_AI_NUMERICHOST)  r |= AI_NUMERICHOST;
    if (f & FB_AI_NUMERICSERV)  r |= AI_NUMERICSERV;
    if (f & FB_AI_ALL)          r |= AI_ALL;
    if (f & FB_AI_ADDRCONFIG)   r |= AI_ADDRCONFIG;
    if (f & (FB_AI_V4MAPPED | FB_AI_V4MAPPED_CFG)) r |= AI_V4MAPPED;
    return r;
}

/* Reverse: Linux ai_flags bits → FreeBSD bits, used when copying the
 * host's getaddrinfo result chain back to the wasm guest. The guest
 * rarely reads ai_flags but the wasm-side type is signed int — leaving
 * Linux's bits in there would make any guest-side `if (info->ai_flags
 * & AI_CANONNAME)` test misfire. */
static int ai_flags_lx_to_fb(int f)
{
    int r = 0;
    if (f & AI_PASSIVE)      r |= FB_AI_PASSIVE;
    if (f & AI_CANONNAME)    r |= FB_AI_CANONNAME;
    if (f & AI_NUMERICHOST)  r |= FB_AI_NUMERICHOST;
    if (f & AI_NUMERICSERV)  r |= FB_AI_NUMERICSERV;
    if (f & AI_ALL)          r |= FB_AI_ALL;
    if (f & AI_ADDRCONFIG)   r |= FB_AI_ADDRCONFIG;
    if (f & AI_V4MAPPED)     r |= FB_AI_V4MAPPED;
    return r;
}
#else  /* darwin / FreeBSD / *BSD — same bits as the guest. */
static inline int ni_flags_fb_to_lx(int f) { return f; }
static inline int ai_flags_fb_to_lx(int f) { return f; }
static inline int ai_flags_lx_to_fb(int f) { return f; }
#endif

int32_t yos_getaddrinfo(struct yos_exec_ctx *ctx, uint32_t node_off,
                        uint32_t service_off, uint32_t hints_off,
                        uint32_t res_off)
{
    if (!res_off || res_off + 4 > ctx->memory_size) return EAI_SYSTEM;
    const char *node    = node_off    ? (const char *)(ctx->memory + node_off)    : NULL;
    const char *service = service_off ? (const char *)(ctx->memory + service_off) : NULL;

    struct addrinfo host_hints = {0};
    struct addrinfo *host_hints_p = NULL;
    if (hints_off && hints_off + WASM_ADDRINFO_SZ <= ctx->memory_size) {
        const uint8_t *w = ctx->memory + hints_off;
        /* AI_* flag bits diverge — translate before handing to host
         * getaddrinfo. Without this, ssh's hints.ai_flags=AI_CANONNAME
         * (0x02 on both) survives but ai_socktype's host-side filter
         * trips on AI_NUMERICSERV=8 (FreeBSD) ↔ AI_V4MAPPED=8 (Linux). */
        host_hints.ai_flags    = ai_flags_fb_to_lx(*(int32_t *)(w +  0));
        host_hints.ai_family   = *(int32_t *)(w +  4);
        host_hints.ai_socktype = *(int32_t *)(w +  8);
        host_hints.ai_protocol = *(int32_t *)(w + 12);
        host_hints_p = &host_hints;
    }

    struct addrinfo *host_res = NULL;
    int rc = getaddrinfo(node, service, host_hints_p, &host_res);
    if (rc != 0) {
        *(uint32_t *)(ctx->memory + res_off) = 0;
        return rc;
    }

    /* Build wasm-side linked list. Each entry: struct + sockaddr +
     * canonname (if present) — all in one yos_malloc per node so the
     * matching freeaddrinfo can release the lot. */
    uint32_t first = 0;
    uint32_t prev = 0;
    for (struct addrinfo *p = host_res; p; p = p->ai_next) {
        uint32_t addrlen = p->ai_addrlen;
        size_t namelen = p->ai_canonname ? strlen(p->ai_canonname) + 1 : 0;
        uint32_t total = WASM_ADDRINFO_SZ + addrlen + (uint32_t)namelen;
        uint32_t blk = yos_malloc(ctx, total);
        if (!blk) { freeaddrinfo(host_res); return EAI_MEMORY; }
        uint8_t *w = ctx->memory + blk;
        memset(w, 0, total);
        *(int32_t *)(w +  0) = ai_flags_lx_to_fb(p->ai_flags);
        *(int32_t *)(w +  4) = p->ai_family;
        *(int32_t *)(w +  8) = p->ai_socktype;
        *(int32_t *)(w + 12) = p->ai_protocol;
        *(uint32_t *)(w + 16) = addrlen;
        uint32_t addr_off = blk + WASM_ADDRINFO_SZ;
        host_sockaddr_to_freebsd(ctx->memory + addr_off, p->ai_addr, addrlen);
        *(uint32_t *)(w + 24) = addr_off;
        if (namelen) {
            uint32_t name_off = addr_off + addrlen;
            memcpy(ctx->memory + name_off, p->ai_canonname, namelen);
            *(uint32_t *)(w + 20) = name_off;
        }
        /* ai_next */
        if (prev) *(uint32_t *)(ctx->memory + prev + 28) = blk;
        else      first = blk;
        prev = blk;
    }
    freeaddrinfo(host_res);
    *(uint32_t *)(ctx->memory + res_off) = first;
    return 0;
}

/* freeaddrinfo — walk the wasm-side list, yos_free each block. */
extern void yos_free(struct yos_exec_ctx *ctx, uint32_t off);

void yos_freeaddrinfo(struct yos_exec_ctx *ctx, uint32_t res_off)
{
    while (res_off && res_off + WASM_ADDRINFO_SZ <= ctx->memory_size) {
        uint32_t next = *(uint32_t *)(ctx->memory + res_off + 28);
        yos_free(ctx, res_off);
        res_off = next;
    }
}

/* getnameinfo — host call with FreeBSD→host sockaddr conversion.
 *
 * The guest hands us a FreeBSD-shape sockaddr (sa_len @0, sa_family @1).
 * Linux host wants sa_family as uint16 LE @0/1; darwin / BSD host wants
 * sa_len @0, sa_family @1 — same as the guest, no rewrite. The
 * platform-specific freebsd_sockaddr_to_host helper does the right
 * thing in both cases.
 *
 * NI_* flag values also diverge between FreeBSD and Linux (see
 * ni_flags_fb_to_lx above); without translation ssh's
 * `getnameinfo(addr, NI_NUMERICHOST)` on a Linux host runs a reverse-
 * DNS lookup instead of returning the dotted-quad IP, and the
 * "host key not yet known" prompt shows the FQDN in the IP slot. */
int32_t yos_getnameinfo(struct yos_exec_ctx *ctx, uint32_t sa_off,
                        uint32_t salen, uint32_t host_off, uint32_t hostlen,
                        uint32_t serv_off, uint32_t servlen, int32_t flags)
{
    if (!posix_wptr_range(ctx, sa_off, salen)) return EAI_SYSTEM;
    uint8_t hostbuf_sa[256];
    if (salen > sizeof(hostbuf_sa)) return EAI_SYSTEM;
    memcpy(hostbuf_sa, ctx->memory + sa_off, salen);
    freebsd_sockaddr_to_host(hostbuf_sa, (socklen_t)salen);
    /* host_off / serv_off are optional output buffers. Validate the
     * full range when given so the host doesn't write past the guest's
     * buffer. */
    char *hbuf = NULL;
    if (host_off) {
        if (!posix_wptr_range(ctx, host_off, hostlen)) return EAI_SYSTEM;
        hbuf = (char *)(ctx->memory + host_off);
    }
    char *sbuf = NULL;
    if (serv_off) {
        if (!posix_wptr_range(ctx, serv_off, servlen)) return EAI_SYSTEM;
        sbuf = (char *)(ctx->memory + serv_off);
    }
    return getnameinfo((const struct sockaddr *)hostbuf_sa, (socklen_t)salen,
                       hbuf, (socklen_t)hostlen, sbuf, (socklen_t)servlen,
                       ni_flags_fb_to_lx(flags));
}

/* posix_madvise — auto-bridge passes wasm offset converted to host
 * pointer, but the conversion happens on EVERY call without checking
 * the offset's validity, and certain Linux versions reject our addr
 * range with EINVAL because the bridge passes addr without rounding
 * to a page boundary. Hand-bridge to round addr/len to the page
 * boundary on the host side. */
int32_t yos_posix_madvise(struct yos_exec_ctx *ctx, uint32_t addr_off,
                          uint32_t len, int32_t advice)
{
    if (!posix_wptr_range(ctx, addr_off, len)) return EINVAL;
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t host_addr = (uintptr_t)(ctx->memory + addr_off);
    uintptr_t aligned = host_addr & ~((uintptr_t)ps - 1);
    size_t pad = host_addr - aligned;
    /* posix_madvise returns the errno-style code directly. */
    return posix_madvise((void *)aligned, len + pad, advice);
}

/* getloadavg — auto-bridge stubs this with -ENOSYS because the
 * output type is `double *`. Hand-bridge: take a wasm offset to an
 * array of nelem doubles, write the values directly into wasm
 * memory. */
int32_t yos_getloadavg(struct yos_exec_ctx *ctx, uint32_t loadavg_off,
                       int32_t nelem)
{
    if (nelem <= 0) return 0;
    if (nelem > 3) nelem = 3;
    if (!loadavg_off ||
        loadavg_off + (uint32_t)nelem * 8 > ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);
    double host[3];
    int n = getloadavg(host, nelem);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++)
        memcpy(ctx->memory + loadavg_off + (uint32_t)i * 8, &host[i], 8);
    return n;
}

#include <stdlib.h>  /* mkdtemp/mkstemp */

/* realpath: resolve absolute pathname. Two calling conventions:
 *   resolved_off != 0: write into the guest-supplied buffer (FreeBSD
 *                       PATH_MAX = 1024). Return resolved_off on
 *                       success, 0 on error.
 *   resolved_off == 0: caller wants malloc'd buffer. We don't have a
 *                       guest-side allocator hooked up here, so today
 *                       we return 0 + errno=ENOMEM. The FreeBSD-base
 *                       realpath(1) tool always passes a non-NULL buf,
 *                       which is the case we care about.
 * The auto-bridge stubs this to NULL because the result is "alias of
 * second arg" — a pattern the generator doesn't yet recognise. */
uint32_t yos_realpath(struct yos_exec_ctx *ctx, uint32_t path_off,
                      uint32_t resolved_off)
{
    if (!path_off || path_off >= ctx->memory_size) {
        if (ctx && ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = EFAULT;
        return 0;
    }
    const char *p = (const char *)(ctx->memory + path_off);
    if (!resolved_off) {
        /* malloc-style call — not yet supported here. */
        if (ctx && ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = ENOMEM;
        return 0;
    }
    if (resolved_off >= ctx->memory_size) {
        if (ctx && ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = EFAULT;
        return 0;
    }
    char *buf = (char *)(ctx->memory + resolved_off);
    /* Resolve against ctx->cwd so a relative input to realpath() is
     * canonicalized from the guest's cwd, not the host process cwd.
     * We deliberately compute the POSIX-shape absolute path WITHOUT
     * the platform path translation (yos_plat_translate_path) — the
     * wasm guest expects POSIX-shape output ("/tmp/x", not
     * "%TEMP%\\x"); the platform realpath impl below canonicalises
     * the POSIX form in place. */
    char abs[4096];
    if (p[0] == '/') {
        snprintf(abs, sizeof abs, "%s", p);
    } else {
        snprintf(abs, sizeof abs, "%s/%s",
                 (ctx && ctx->cwd[0]) ? ctx->cwd : "/", p);
    }
    char *r = realpath(abs, buf);
    if (!r) {
        extern int yos_remap_errno_h2g(int);
        if (ctx && ctx->memory && ctx->errno_off)
            *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(errno);
        return 0;
    }
    return resolved_off;
}

/* mkdtemp: replaces the trailing "XXXXXX" in template with random
 * chars, creates the directory, returns the (modified) template
 * pointer on success or NULL on failure. The auto-bridge stubbed
 * this to NULL — fatal for nvim's swap-file machinery. */
uint32_t yos_mkdtemp(struct yos_exec_ctx *ctx, uint32_t template_off)
{
    if (!template_off || template_off >= ctx->memory_size) return 0;
    char *t = (char *)(ctx->memory + template_off);
    /* Bound the in-place modification to the readable region.
     * mkdtemp writes back into the same buffer, replacing X chars. */
    char *r = mkdtemp(t);
    if (!r) return 0;
    return template_off;
}

int32_t yos_mkstemp(struct yos_exec_ctx *ctx, uint32_t template_off)
{
    if (!template_off || template_off >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    char *t = (char *)(ctx->memory + template_off);
    int hfd = mkstemp(t);
    if (hfd < 0) return yos_errno_neg(ctx, errno);
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { yos_plat_close(hfd); return yos_errno_neg(ctx, EMFILE); }
    return wfd;
}

int32_t yos_mkostemp(struct yos_exec_ctx *ctx, uint32_t template_off, int32_t flags)
{
    if (!template_off || template_off >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    char *t = (char *)(ctx->memory + template_off);
    /* flags here are FreeBSD O_* (e.g. O_CLOEXEC) — translate to host. */
    extern int oflags_fb_to_lx_fwd(int);
    int hflags = oflags_fb_to_lx_fwd(flags);
    int hfd = mkostemp(t, hflags);
    if (hfd < 0) return yos_errno_neg(ctx, errno);
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { yos_plat_close(hfd); return yos_errno_neg(ctx, EMFILE); }
    return wfd;
}

int32_t yos_mkostemps(struct yos_exec_ctx *ctx, uint32_t template_off,
                     int32_t suffixlen, int32_t flags)
{
    if (!template_off || template_off >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    char *t = (char *)(ctx->memory + template_off);
    extern int oflags_fb_to_lx_fwd(int);
    int hflags = oflags_fb_to_lx_fwd(flags);
    int hfd = mkostemps(t, suffixlen, hflags);
    if (hfd < 0) return yos_errno_neg(ctx, errno);
    int wfd = yos_fd_alloc(ctx, hfd);
    if (wfd < 0) { yos_plat_close(hfd); return yos_errno_neg(ctx, EMFILE); }
    return wfd;
}

#include <sys/stat.h>
#include "impl/io/cv_stat.h"
/* yos_cv_stat_fbi() in impl/io/cv_stat.c is the authoritative writer.
 * Both this file and impl/io/fifo.c call it; field offsets were
 * extracted with tools/struct-offsets.py stat sys/stat.h. */

int32_t yos_fstat(struct yos_exec_ctx *ctx, int32_t wfd, uint32_t statbuf_off)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    struct stat h;
    memset(&h, 0, sizeof h);
    if (yos_plat_fstat(hfd, &h) < 0) return yos_errno_neg(ctx, errno);
    if (ytrace_default_enabled()) {
        const char *kind = "?";
        if (S_ISREG(h.st_mode))  kind = "REG";
        else if (S_ISDIR(h.st_mode)) kind = "DIR";
        else if (S_ISCHR(h.st_mode)) kind = "CHR/tty?";
        else if (S_ISBLK(h.st_mode)) kind = "BLK";
        else if (S_ISFIFO(h.st_mode)) kind = "FIFO";
        else if (S_ISSOCK(h.st_mode)) kind = "SOCK";
        ydebug("fstat(wfd=%d hfd=%d) mode=0%o (%s)\n",
               wfd, hfd, (unsigned)h.st_mode, kind);
    }
    yos_cv_stat_fbi(ctx->memory + statbuf_off, &h);
    return 0;
}

/* fmtcheck(user_fmt, default_fmt) — FreeBSD libc helper that returns
 * `user_fmt` if its conversion specifiers are CLASS-compatible with
 * `default_fmt`, else `default_fmt`. glibc has no equivalent.
 *
 * The FreeBSD test exercises 30+ subtle cases (e.g. `%qd` is the BSD
 * synonym of `%llx`, `%D` of `%ld`, width-modifier `*` introduces an
 * extra int arg, etc.). Reimplementing it correctly from scratch is
 * a nontrivial state machine; we link against the FreeBSD source
 * directly via a thin wrapper. The .c file lives under build-tools/
 * freebsd/.../lib/libc/gen/fmtcheck.c (BSD-2-Clause). */
extern const char *yos_fmtcheck_freebsd(const char *f1, const char *f2);

uint32_t yos_fmtcheck(struct yos_exec_ctx *ctx, uint32_t user_off,
                      uint32_t default_off)
{
    const char *u = user_off    ? (const char *)(ctx->memory + user_off)    : NULL;
    const char *d = default_off ? (const char *)(ctx->memory + default_off) : NULL;
    const char *r = yos_fmtcheck_freebsd(u, d);
    /* FreeBSD always returns one of the two input pointers (or NULL
     * if user is NULL); convert that host pointer back to wasm. */
    if (r == u) return user_off;
    if (r == d) return default_off;
    return default_off;
}

int32_t yos_fsync(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    return yos_errno_check(ctx, fsync(hfd));
}

int32_t yos_fdatasync(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    return yos_errno_check(ctx, fdatasync(hfd));
}

int32_t yos_fchdir(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);

    /* Verify it's actually a directory before committing — fchdir on
     * a non-dir fd must return ENOTDIR. */
    struct stat st;
    if (yos_plat_fstat(hfd, &st) < 0) return yos_errno_neg(ctx, errno);
    if (!S_ISDIR(st.st_mode)) return yos_errno_neg(ctx, ENOTDIR);

    /* Update ctx->cwd from the path recorded at open-time. yos's
     * fd table tracks (wfd → host_fd, absolute_path); every open /
     * openat / opendir populates it. We DELIBERATELY skip host
     * fchdir() — yos's pthread-per-ctx model shares host cwd across
     * guests, and a real fchdir would silently move every other
     * guest's relative paths. Same compromise yos_chdir made.
     *
     * If no path was recorded (e.g. fd came from socket, pipe,
     * fcntl-dup, accept) we have nothing to set cwd to — fchdir on
     * a non-recorded fd returns 0 but leaves ctx->cwd unchanged. */
    if (wfd >= 0 && wfd < YOS_FD_MAX && ctx->fd_paths[wfd]) {
        strncpy(ctx->cwd, ctx->fd_paths[wfd], PATH_MAX - 1);
        ctx->cwd[PATH_MAX - 1] = '\0';
        ydebug("fchdir(wfd=%d hfd=%d) → ctx->cwd=\"%s\"\n",
               wfd, hfd, ctx->cwd);
    } else {
        ydebug("fchdir(wfd=%d hfd=%d) — no recorded path; cwd unchanged\n",
               wfd, hfd);
    }
    return 0;
}

/* ── pread / pwrite (host has same signature, just need fd remap) ── */

int32_t yos_pread(struct yos_exec_ctx *ctx, int32_t wfd, uint32_t buf,
                  uint32_t count, int64_t off)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    void *p = posix_wptr_range(ctx, buf, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    ssize_t r = pread(hfd, p, count, (off_t)off);
    return yos_errno_check(ctx, (int32_t)r);
}

int32_t yos_pwrite(struct yos_exec_ctx *ctx, int32_t wfd, uint32_t buf,
                   uint32_t count, int64_t off)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    void *p = posix_wptr_range(ctx, buf, count);
    if (!p) return yos_errno_neg(ctx, EFAULT);
    ssize_t r = pwrite(hfd, p, count, (off_t)off);
    return yos_errno_check(ctx, (int32_t)r);
}

/* ── trivial host passthroughs (no fd, no memory) ─────────────────── */

int32_t yos_getuid (struct yos_exec_ctx *ctx) { (void)ctx; return (int32_t)getuid();  }
int32_t yos_geteuid(struct yos_exec_ctx *ctx) { (void)ctx; return (int32_t)geteuid(); }
int32_t yos_getgid (struct yos_exec_ctx *ctx) { (void)ctx; return (int32_t)getgid();  }
int32_t yos_getegid(struct yos_exec_ctx *ctx) { (void)ctx; return (int32_t)getegid(); }

/* issetugid: BSD/macOS only — Linux glibc doesn't ship it. Returns
 * non-zero if the process is running with elevated privileges that
 * make it unsafe to honour environment variables. Under yos a wasm
 * guest is always running as the user that launched it — never
 * privileged — so return 0. openssl's ossl_safe_getenv and openssh's
 * ssh_get_progname/etc. use this to decide whether to trust $PATH,
 * $HOME, etc. (we DO want them to trust). */
int32_t yos_issetugid(struct yos_exec_ctx *ctx) { (void)ctx; return 0; }

uint32_t yos_umask(struct yos_exec_ctx *ctx, uint32_t mask)
{
    /* Per-ctx umask: store in ctx, return previous. The host process
     * umask is forced to 0 at startup (main.c) so file-creating
     * bridges apply masking in software via ctx->umask — that lets
     * concurrent yos guests each have their own umask without
     * stomping the shared host-process value. */
    uint32_t prev = (uint32_t)ctx->umask;
    ctx->umask = (unsigned short)(mask & 0777);
    return prev;
}

int32_t yos_sync(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    sync();
    return 0;
}

/* sysconf returns `long` on the host (64-bit on macOS/Linux x86_64).
 * wasm32 long is 32-bit; the auto-passthrough bridge truncates the
 * top 32 bits and turns LONG_MAX (which darwin returns for unbounded
 * limits like _SC_OPEN_MAX) into -1, breaking shells and runtimes
 * that probe sysconf at startup.
 *
 * Two-step fix here: clamp the host result into int32 range, and
 * map -1+errno through the standard errno helper so the guest sees
 * a real ENOSYS for unsupported names. The FreeBSD _SC_* constants
 * mostly match darwin's (both are BSD-derived) so we forward the
 * name as-is; if Linux glibc renumbering shows up, add a remap
 * table here. */
int32_t yos_sysconf(struct yos_exec_ctx *ctx, int32_t name)
{
    /* The guest's _SC_* numbers are FreeBSD's (sysroot sys/unistd.h)
     * and DO NOT match the host's: e.g. _SC_NPROCESSORS_ONLN is 58 on
     * FreeBSD but 84 on glibc — passing 58 through asked the host for
     * an unrelated limit, and fzy sized its worker pool from the
     * garbage (pthread_join on never-created threads). Translate the
     * names we can express with the host's own symbolic constants;
     * anything unmapped is reported honestly as "no limit" instead of
     * silently querying the wrong knob. */
    int host_name;
    switch (name) {
        case   1: host_name = _SC_ARG_MAX;            break;
        case   2: host_name = _SC_CHILD_MAX;          break;
        case   3: host_name = _SC_CLK_TCK;            break;
        case   4: host_name = _SC_NGROUPS_MAX;        break;
        case   5: host_name = _SC_OPEN_MAX;           break;
        case  15: host_name = _SC_LINE_MAX;           break;
        case  47: host_name = _SC_PAGESIZE;           break;
        case  56: host_name = _SC_IOV_MAX;            break;
        case  57: host_name = _SC_NPROCESSORS_CONF;   break;
        case  58: host_name = _SC_NPROCESSORS_ONLN;   break;
        case  70: host_name = _SC_GETGR_R_SIZE_MAX;   break;
        case  71: host_name = _SC_GETPW_R_SIZE_MAX;   break;
        case  72: host_name = _SC_HOST_NAME_MAX;      break;
        case 101: host_name = _SC_TTY_NAME_MAX;       break;
        case 120: host_name = _SC_SYMLOOP_MAX;        break;
        case 121: host_name = _SC_PHYS_PAGES;         break;
        default:
            ydebug("sysconf: unmapped FreeBSD name %d -> -1 (no limit)\n",
                   (int)name);
            return -1;
    }
    errno = 0;
    long r = sysconf(host_name);
    if (r < 0) {
        /* sysconf returns -1 with errno=0 to mean "unlimited / not
         * specifically configured", and -1 with errno != 0 for a
         * real error. POSIX says callers must check errno to
         * distinguish; wasm guests do the same, so propagate. */
        if (errno != 0) return yos_errno_neg(ctx, errno);
        return -1;
    }
    if (r > 0x7fffffffL) return 0x7fffffff;
    return (int32_t)r;
}

/* ── signals ──────────────────────────────────────────────────────── */

int32_t yos_raise(struct yos_exec_ctx *ctx, int32_t sig)
{
    /* raise() == kill(getpid(), sig). Going through host raise()
     * delivers a real host SIGUSR1/etc. which yos doesn't route to
     * the wasm-side handler (only the few signals installed by
     * yos_install_host_signal_handlers get forwarded). Route through
     * yos_kill instead so the per-ctx sig_handlers[] table is
     * consulted and yos_signal_pump fires the registered handler. */
    if (!ctx || !ctx->proc) return yos_errno_neg(ctx, EINVAL);
    extern int32_t yos_kill(struct yos_exec_ctx *, int32_t, int32_t);
    return yos_kill(ctx, ctx->proc->pid, sig);
}

int32_t yos_killpg(struct yos_exec_ctx *ctx, int32_t pgrp, int32_t sig)
{
    (void)ctx;
    return yos_errno_check(ctx, killpg(pgrp, sig));
}

/* ── sbrk: thin proxy to brk-style heap. yos_brk lives in impl/mem.c
 * and tracks ctx->heap_end. sbrk(0) reports current break, sbrk(N)
 * advances and returns the OLD break. */

extern int32_t yos_brk(struct yos_exec_ctx *ctx, uint32_t addr);

uint32_t yos_sbrk(struct yos_exec_ctx *ctx, int32_t incr)
{
    uint32_t old = ctx->heap_end;
    if (incr == 0) return old;
    int32_t r = yos_brk(ctx, old + (uint32_t)incr);
    if (r < 0) return (uint32_t)-1;
    return old;
}

/* ── socketpair — host has identical signature; bridge can't render
 * the int sv[2] arg auto-style, do it here. */

/* FreeBSD socket type flags:
 *   SOCK_CLOEXEC  = 0x10000000
 *   SOCK_NONBLOCK = 0x20000000
 * Linux socket type flags:
 *   SOCK_CLOEXEC  = 0x80000  (octal 02000000)
 *   SOCK_NONBLOCK = 0x800    (octal 04000)
 * Bottom byte (SOCK_STREAM=1, SOCK_DGRAM=2, …) matches.
 *
 * Without translation, libuv's `socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC,
 * 0, sv)` fails with EINVAL on the Linux host — the spawn pipe is
 * never created — and every later `write()` to the (nonexistent) pipe
 * EPIPEs the RPC channel between the TUI parent and the embedded
 * server. nvim's TUI logs `chan_close_with_error: stream write
 * failed` and silently throws away every keystroke. */
static int sock_type_fb_to_lx(int t)
{
    int base = t & 0xff;       /* SOCK_STREAM/DGRAM/RAW/SEQPACKET — same. */
    int out = base;
    if (t & 0x10000000) out |= SOCK_CLOEXEC;
    if (t & 0x20000000) out |= SOCK_NONBLOCK;
    return out;
}
int sock_type_fb_to_lx_fwd(int t) { return sock_type_fb_to_lx(t); }

int32_t yos_socketpair(struct yos_exec_ctx *ctx, int32_t domain,
                       int32_t type, int32_t protocol, uint32_t sv_off)
{
    /* sv_off → int sv[2]; 8 bytes total. Wrap-safe range check. */
    if (!posix_wptr_range(ctx, sv_off, 8)) return yos_errno_neg(ctx, EFAULT);
    int hfds[2];
    /* Detect the FreeBSD high-bit flags before they get masked away
     * by sock_type_fb_to_lx (which on darwin maps both to 0 because
     * the platform has no SOCK_NONBLOCK/SOCK_CLOEXEC). Apply the
     * equivalent semantics via fcntl below. tmp/nvim-runtime-issues.md
     * covers why this matters for libuv's IPC channel. */
    int want_nonblock = !!(type & 0x20000000);
    int want_cloexec  = !!(type & 0x10000000);
    if (socketpair(domain, sock_type_fb_to_lx(type), protocol, hfds) < 0)
        return yos_errno_neg(ctx, errno);
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
    int32_t wa = yos_fd_alloc(ctx, hfds[0]);
    if (wa < 0) { close(hfds[0]); close(hfds[1]); return yos_errno_neg(ctx, EMFILE); }
    int32_t wb = yos_fd_alloc(ctx, hfds[1]);
    if (wb < 0) { yos_fd_close(ctx, wa); close(hfds[1]); return yos_errno_neg(ctx, EMFILE); }
    int32_t *sv = (int32_t *)(ctx->memory + sv_off);
    sv[0] = wa;
    sv[1] = wb;
    return 0;
}

/* ── termios — FreeBSD wasm32 layout ↔ Linux glibc layout ──────────
 *
 * FreeBSD struct termios (44 B):
 *   c_iflag/oflag/cflag/lflag (4×4 B), c_cc[NCCS=20] @16, c_ispeed @36,
 *   c_ospeed @40
 * Linux glibc struct termios (60 B):
 *   c_iflag/oflag/cflag/lflag (4×4 B), c_line @16, c_cc[NCCS=32] @17,
 *   c_ispeed @52, c_ospeed @56
 *
 * Flag bits and control-character indices differ; we translate only
 * the ones nvim/libuv actually exercises (the four flag words plus
 * the c_cc subset that controls raw mode). Anything else stays
 * zero — that's safe for tcgetattr→tcsetattr round trips because
 * libuv only mutates a small subset before writing back. */

/* FreeBSD termios layout (offsets/sizes per
 * build-linux/src/yos/codegen/guest-api-i386-freebsd.yaml). 44 bytes. */
#define YOS_FBSD_TERMIOS_SIZE   44
#define YOS_FBSD_NCCS           20

/* FreeBSD c_cc[] indices (sys/_termios.h). */
enum { YOS_FB_VEOF=0, YOS_FB_VEOL=1, YOS_FB_VEOL2=2, YOS_FB_VERASE=3,
       YOS_FB_VWERASE=4, YOS_FB_VKILL=5, YOS_FB_VREPRINT=6,
       YOS_FB_VINTR=8, YOS_FB_VQUIT=9, YOS_FB_VSUSP=10, YOS_FB_VDSUSP=11,
       YOS_FB_VSTART=12, YOS_FB_VSTOP=13, YOS_FB_VLNEXT=14,
       YOS_FB_VDISCARD=15, YOS_FB_VMIN=16, YOS_FB_VTIME=17,
       YOS_FB_VSTATUS=18 };


/* c_cc[] index translation. fb_idx → linux_idx (or -1 if not on Linux). */
int cc_fb_to_lx(int fb_idx)
{
    switch (fb_idx) {
    case YOS_FB_VEOF:    return VEOF;
    case YOS_FB_VEOL:    return VEOL;
    case YOS_FB_VEOL2:   return VEOL2;
    case YOS_FB_VERASE:  return VERASE;
    case YOS_FB_VWERASE: return VWERASE;
    case YOS_FB_VKILL:   return VKILL;
    case YOS_FB_VREPRINT:return VREPRINT;
    case YOS_FB_VINTR:   return VINTR;
    case YOS_FB_VQUIT:   return VQUIT;
    case YOS_FB_VSUSP:   return VSUSP;
    case YOS_FB_VSTART:  return VSTART;
    case YOS_FB_VSTOP:   return VSTOP;
    case YOS_FB_VLNEXT:  return VLNEXT;
    case YOS_FB_VDISCARD:return VDISCARD;
    case YOS_FB_VMIN:    return VMIN;
    case YOS_FB_VTIME:   return VTIME;
    default:             return -1;
    }
}


/* Fill a host termios with the defaults a real PTY master/slave gets
 * after openpt(): canonical mode, echo on, common control chars,
 * 38400 baud. This is what zsh's tcgetattr sees on a working PTY and
 * lets the shell turn on its interactive features (prompt, echo,
 * line editing). The wasm guest may then tcsetattr to switch the
 * line discipline; we silently accept those and remember the last
 * struct so subsequent tcgetattr round-trips are stable. */
/* Per-ctx (ctx->fake_pty_termios is opaque bytes wide enough for
 * struct termios — see types.h). Used to be a process-wide pair of
 * statics; two telnet sessions both calling tcsetattr would clobber
 * each other's PTY line-discipline state. */

static void fake_pty_termios_defaults(struct termios *t)
{
    /* Synthesise the cooked-mode termios a real PTY reports after
     * posix_openpt(): ICANON + ECHO on, common control chars, 38400
     * baud. This is what zsh and telnetd both expect from a TTY.
     *
     * NB: telnetd's net-output path uses ESC → IAC DM (synch) framing
     * when it sees an ESC in a stream marked TTY. That's standard
     * RFC 854 / 855 — real telnet clients UNSTUFF IAC DM correctly
     * and display ESC as the escape byte. A naive raw recv that
     * consumes IAC as a 3-byte command will lose the byte after DM —
     * that's a client-side framing bug, not a telnetd bug. */
    memset(t, 0, sizeof *t);
    t->c_iflag = ICRNL | IXON | BRKINT;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD | HUPCL;
    t->c_lflag = ECHO | ECHOE | ECHOK | ECHONL | ICANON | ISIG | IEXTEN;
    t->c_cc[VINTR]    = 003;  /* ^C  */
    t->c_cc[VQUIT]    = 034;  /* ^\  */
    t->c_cc[VERASE]   = 0177; /* DEL */
    t->c_cc[VKILL]    = 025;  /* ^U  */
    t->c_cc[VEOF]     = 004;  /* ^D  */
    t->c_cc[VSTART]   = 021;  /* ^Q  */
    t->c_cc[VSTOP]    = 023;  /* ^S  */
    t->c_cc[VSUSP]    = 032;  /* ^Z  */
    t->c_cc[VMIN]     = 1;
    t->c_cc[VTIME]    = 0;
    cfsetispeed(t, B38400);
    cfsetospeed(t, B38400);
}

int32_t yos_tcgetattr(struct yos_exec_ctx *ctx, int32_t wfd, uint32_t t_off)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return yos_errno_neg(ctx, EFAULT);
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);

    /* Synthesise termios for fake PTYs (socketpair host fds — host
     * tcgetattr returns ENOTSUP/ENOTTY on them). Without this zsh
     * picks the no-tty code path and runs without a prompt. */
    extern int yos_pty_is_pty_fd(int hfd);
    if (yos_pty_is_pty_fd(hfd)) {
        if (!ctx->fake_pty_termios_init) {
            fake_pty_termios_defaults((struct termios *)ctx->fake_pty_termios);
            ctx->fake_pty_termios_init = 1;
        }
        termios_lx_to_fb(ctx->memory + t_off, (struct termios *)ctx->fake_pty_termios);
        return 0;
    }

    struct termios h;
    if (tcgetattr(hfd, &h) < 0) return yos_errno_neg(ctx, errno);
    termios_lx_to_fb(ctx->memory + t_off, &h);
    return 0;
}

int32_t yos_tcsetattr(struct yos_exec_ctx *ctx, int32_t wfd,
                      int32_t actions, uint32_t t_off)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return yos_errno_neg(ctx, EFAULT);
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);

    /* Fake-PTY: store the requested termios in our shared synthetic
     * slot so a subsequent guest tcgetattr round-trip returns what
     * was just set. We don't actually enforce line discipline at the
     * host socket level (it's a socket, no kernel TTY behind it) —
     * the guest is responsible for its own raw/cooked switching. */
    extern int yos_pty_is_pty_fd(int hfd);
    extern int yos_pty_set_onlcr(int hfd, int on);
    if (yos_pty_is_pty_fd(hfd)) {
        if (!ctx->fake_pty_termios_init) {
            fake_pty_termios_defaults((struct termios *)ctx->fake_pty_termios);
            ctx->fake_pty_termios_init = 1;
        }
        termios_fb_to_lx((struct termios *)ctx->fake_pty_termios, ctx->memory + t_off);
        /* Sync ONLCR to the pty-entry so master reads emit CRLF when
         * the guest leaves the slave in cooked mode (default) and
         * raw LF when the guest cleared the bit (cfmakeraw etc.). */
        yos_pty_set_onlcr(hfd, !!(((struct termios *)ctx->fake_pty_termios)->c_oflag & ONLCR));
        (void)actions;
        return 0;
    }

    /* FreeBSD TCSANOW=0, TCSADRAIN=1, TCSAFLUSH=2 — same on Linux, no
     * remap needed. */
    struct termios h;
    /* Read current host state first so any flag bit we don't translate
     * is preserved across a guest tcgetattr+tcsetattr round trip. */
    if (tcgetattr(hfd, &h) < 0) return yos_errno_neg(ctx, errno);
    /* Now overlay the guest's desired flags. termios_fb_to_lx zeros
     * the host struct and writes the flags we know — the cfsetispeed
     * /cfsetospeed inside it pull in the speed bits. */
    termios_fb_to_lx(&h, ctx->memory + t_off);
    if (tcsetattr(hfd, actions, &h) < 0) return yos_errno_neg(ctx, errno);
    return 0;
}

/* cfmakeraw — operate directly on the wasm-side FreeBSD termios. The
 * auto-bridge passed it to host glibc which scrambles the 44-byte
 * struct because glibc writes Linux's 60-byte layout. */
void yos_cfmakeraw(struct yos_exec_ctx *ctx, uint32_t t_off)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return;
    uint8_t *w = ctx->memory + t_off;
    uint32_t iflag = *(uint32_t *)(w +  0);
    uint32_t oflag = *(uint32_t *)(w +  4);
    uint32_t cflag = *(uint32_t *)(w +  8);
    uint32_t lflag = *(uint32_t *)(w + 12);
    /* Match cfmakeraw(3) — clear flags that interpret input/output. */
    /* iflag: clear IGNBRK, BRKINT, PARMRK, ISTRIP, INLCR, IGNCR, ICRNL, IXON */
    iflag &= ~(uint32_t)(0x0001 | 0x0002 | 0x0008 | 0x0020 |
                         0x0040 | 0x0080 | 0x0100 | 0x0200);
    /* oflag: clear OPOST */
    oflag &= ~(uint32_t)0x0001;
    /* lflag: clear ECHO, ECHONL, ICANON, ISIG, IEXTEN */
    lflag &= ~(uint32_t)(0x0008 | 0x0010 | 0x0100 | 0x0080 | 0x0400);
    /* cflag: clear CSIZE | PARENB, set CS8 */
    cflag &= ~(uint32_t)(0x0300 | 0x1000);
    cflag |=  (uint32_t)0x0300;  /* CS8 */
    *(uint32_t *)(w +  0) = iflag;
    *(uint32_t *)(w +  4) = oflag;
    *(uint32_t *)(w +  8) = cflag;
    *(uint32_t *)(w + 12) = lflag;
    /* c_cc: VMIN=16, VTIME=17 in FreeBSD. */
    w[16 + 16] = 1;  /* VMIN */
    w[16 + 17] = 0;  /* VTIME */
}

int32_t yos_cfsetispeed(struct yos_exec_ctx *ctx, uint32_t t_off, uint32_t speed)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return yos_errno_neg(ctx, EFAULT);
    *(uint32_t *)(ctx->memory + t_off + 36) = speed;
    return 0;
}

int32_t yos_cfsetospeed(struct yos_exec_ctx *ctx, uint32_t t_off, uint32_t speed)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return yos_errno_neg(ctx, EFAULT);
    *(uint32_t *)(ctx->memory + t_off + 40) = speed;
    return 0;
}

uint32_t yos_cfgetispeed(struct yos_exec_ctx *ctx, uint32_t t_off)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return 0;
    return *(uint32_t *)(ctx->memory + t_off + 36);
}

uint32_t yos_cfgetospeed(struct yos_exec_ctx *ctx, uint32_t t_off)
{
    if (!posix_wptr_range(ctx, t_off, YOS_FBSD_TERMIOS_SIZE)) return 0;
    return *(uint32_t *)(ctx->memory + t_off + 40);
}

/* ── utimes / futimes / lutimes ──────────────────────────────────────
 *
 * struct timeval is { time_t tv_sec; suseconds_t tv_usec; }. On
 * FreeBSD-i386 wasm32 both fields are 32-bit (8 bytes total). On host
 * x86_64 Linux tv_sec is 64-bit (16 bytes total). The wasm guest
 * passes a wasm offset to a 2-element timeval array (16 bytes); we
 * widen each scalar to host shape before calling host libc.
 *
 * Auto-bridge stubs these because struct timeval is in the
 * struct_convert table for in/out scalar fields but not for the
 * 2-element array form these fns use. Hand-bridge: read 4 little-
 * endian uint32s from wasm, build 2 host timevals, call.
 */
#include <sys/time.h>

static void wasm_timeval2_to_host(const uint8_t *w, struct timeval h[2])
{
    for (int i = 0; i < 2; i++) {
        uint32_t s, u;
        memcpy(&s, w + i*8 + 0, 4);
        memcpy(&u, w + i*8 + 4, 4);
        h[i].tv_sec  = (time_t)(int32_t)s;   /* sign-extend */
        h[i].tv_usec = (suseconds_t)(int32_t)u;
    }
}

int32_t yos_utimes(struct yos_exec_ctx *ctx, uint32_t path_off, uint32_t times_off)
{
    if (!path_off || path_off >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    const char *path_in = (const char *)(ctx->memory + path_off);
    const char *path = yos_path_resolve(ctx, path_in);
    struct timeval *p = NULL, h[2];
    if (times_off) {
        /* sizeof(wasm timeval[2]) = 16; overflow-safe via posix_wptr_range. */
        if (!posix_wptr_range(ctx, times_off, 16)) return yos_errno_neg(ctx, EFAULT);
        wasm_timeval2_to_host(ctx->memory + times_off, h);
        p = h;
    }
    if (utimes(path, p) < 0) return yos_errno_neg(ctx, errno);
    return 0;
}

int32_t yos_futimes(struct yos_exec_ctx *ctx, int32_t wfd, uint32_t times_off)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    struct timeval *p = NULL, h[2];
    if (times_off) {
        if (!posix_wptr_range(ctx, times_off, 16)) return yos_errno_neg(ctx, EFAULT);
        wasm_timeval2_to_host(ctx->memory + times_off, h);
        p = h;
    }
    if (futimes(hfd, p) < 0) return yos_errno_neg(ctx, errno);
    return 0;
}

int32_t yos_lutimes(struct yos_exec_ctx *ctx, uint32_t path_off, uint32_t times_off)
{
    if (!path_off || path_off >= ctx->memory_size) return yos_errno_neg(ctx, EFAULT);
    const char *path_in = (const char *)(ctx->memory + path_off);
    const char *path = yos_path_resolve(ctx, path_in);
    struct timeval *p = NULL, h[2];
    if (times_off) {
        if (!posix_wptr_range(ctx, times_off, 16)) return yos_errno_neg(ctx, EFAULT);
        wasm_timeval2_to_host(ctx->memory + times_off, h);
        p = h;
    }
    if (lutimes(path, p) < 0) return yos_errno_neg(ctx, errno);
    return 0;
}

/* __xuname — FreeBSD's uname(2) backend. The inline `uname()` in
 * <sys/utsname.h> calls __xuname(SYS_NMLN=256, struct utsname*).
 * struct utsname on wasm is 5 fields × 256 bytes = 1280 bytes.
 *
 * We pretend to be FreeBSD on wasm32 (the wasm guest's whole point).
 * sysname=FreeBSD, machine=wasm32. Rest is just pleasant defaults
 * pulled from host uname so build scripts get a sensible kernel
 * version when they grep release. */
#include <sys/utsname.h>
int32_t yos___xuname(struct yos_exec_ctx *ctx, int32_t namesz, uint32_t buf_off)
{
    /* Reject zero / non-positive AND clamp namesz against an obviously-
     * absurd ceiling so namesz*5 can't wrap uint32_t. Real FreeBSD
     * passes SYS_NMLN=256; anything above 64 KiB per field is junk. */
    if (namesz <= 0 || namesz > 65536) return yos_errno_neg(ctx, EFAULT);
    /* 64-bit total — wrap-safe range check via posix_wptr_range. */
    uint64_t total = (uint64_t)namesz * 5;
    char *base = (char *)posix_wptr_range(ctx, buf_off, total);
    if (!base) return yos_errno_neg(ctx, EFAULT);
    char *fields[5] = {
        base + 0u * (uint32_t)namesz,  /* sysname */
        base + 1u * (uint32_t)namesz,  /* nodename */
        base + 2u * (uint32_t)namesz,  /* release */
        base + 3u * (uint32_t)namesz,  /* version */
        base + 4u * (uint32_t)namesz,  /* machine */
    };
    /* Zero everything first so any short string is NUL-terminated. */
    memset(base, 0, (size_t)total);

    struct utsname host;
    if (uname(&host) < 0) return yos_errno_neg(ctx, errno);

    /* Present a FreeBSD wasm32 face regardless of host. */
    strncpy(fields[0], "FreeBSD", (size_t)namesz - 1);
    /* nodename = host's hostname (real). */
    strncpy(fields[1], host.nodename, (size_t)namesz - 1);
    /* release = "14.4-yos" so build scripts that test FreeBSD>=12 work. */
    strncpy(fields[2], "14.4-yos", (size_t)namesz - 1);
    /* version = a free-form build-time string. */
    snprintf(fields[3], (size_t)namesz, "FreeBSD 14.4-yos wasm32 host=%s",
             host.release);
    /* machine = wasm32. */
    strncpy(fields[4], "wasm32", (size_t)namesz - 1);
    return 0;
}

/* if_indextoname — return a wasm offset to a buffer holding the
 * interface name. Auto-bridge stubbed because the return value
 * `char *` aliases the second arg. We let host libc fill the
 * caller's buffer and return its wasm offset on success. */
#include <net/if.h>
uint32_t yos_if_indextoname(struct yos_exec_ctx *ctx,
                            uint32_t ifindex, uint32_t name_off)
{
    char *buf = (char *)posix_wptr_range(ctx, name_off, IF_NAMESIZE);
    if (!buf) return 0;
    if (!if_indextoname(ifindex, buf)) return 0;
    return name_off;
}

/* accept4 — like accept(2) but with flags. Same fd virtualisation
 * as accept; FreeBSD SOCK_CLOEXEC etc. translate to host equivalents.
 * Auto-bridge classified as "unportable Linux extension"; on Linux it
 * IS available (glibc 2.10+). Stub on darwin until a fcntl-based
 * fallback lands. */

#include <grp.h>

/* ── getgroups(int gidsetsize, gid_t grouplist[]) ────────────────────
 *
 * gid_t is 32-bit on both FreeBSD-i386 and Linux x86_64, so the per-
 * element copy is straight uint32. Bounds-check the wasm out-array;
 * POSIX `gidsetsize=0` means "tell me how many groups, don't write
 * anything". */
int32_t yos_getgroups(struct yos_exec_ctx *ctx, int32_t gidsetsize,
                      uint32_t list_off)
{
    if (gidsetsize == 0) {
        int n = getgroups(0, NULL);
        if (n < 0) return yos_errno_neg(ctx, errno);
        return n;
    }
    if (gidsetsize < 0) return yos_errno_neg(ctx, EINVAL);
    if (!list_off ||
        (uint64_t)list_off + (uint64_t)gidsetsize * 4u > ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);

    gid_t *host = malloc((size_t)gidsetsize * sizeof(gid_t));
    if (!host) return yos_errno_neg(ctx, ENOMEM);

    int n = getgroups(gidsetsize, host);
    if (n < 0) {
        int saved = errno;
        free(host);
        return yos_errno_neg(ctx, saved);
    }
    uint32_t *out = (uint32_t *)(ctx->memory + list_off);
    for (int i = 0; i < n; i++) out[i] = (uint32_t)host[i];
    free(host);
    return n;
}

/* ── alphasort / versionsort — scandir(3) comparators ────────────────
 *
 * scandir collects directory entries into an array and runs qsort with
 * a caller-supplied comparator. alphasort sorts by strcoll on d_name;
 * versionsort uses strverscmp (GNU extension, also in glibc).
 *
 * Arguments are `const struct dirent **`: wasm offsets at which
 * another wasm offset (the dirent pointer) lives. Reach through once
 * to get the dirent base, then read d_name at offset 24 (FreeBSD-i386
 * struct dirent layout: 8 + 8 + 2 + 1 + 1 + 2 + 2 = 24, then 256-byte
 * d_name).
 *
 * Bounds-check both levels of indirection. Returns 0 on bad input —
 * collapses any pair we can't read into "equal", which is safe for a
 * comparator: qsort still terminates.
 */
#define YOS_FBSD_DIRENT_NAME_OFFSET 24
#define YOS_FBSD_DIRENT_MIN_SIZE    (YOS_FBSD_DIRENT_NAME_OFFSET + 1)

static const char *dirent_name_from_pp(struct yos_exec_ctx *ctx, uint32_t pp)
{
    if (!pp || pp + 4 > ctx->memory_size) return NULL;
    uint32_t ent = *(uint32_t *)(ctx->memory + pp);
    if (!ent || (uint64_t)ent + YOS_FBSD_DIRENT_MIN_SIZE > ctx->memory_size)
        return NULL;
    return (const char *)(ctx->memory + ent + YOS_FBSD_DIRENT_NAME_OFFSET);
}

int32_t yos_alphasort(struct yos_exec_ctx *ctx, uint32_t a_pp, uint32_t b_pp)
{
    const char *a = dirent_name_from_pp(ctx, a_pp);
    const char *b = dirent_name_from_pp(ctx, b_pp);
    if (!a || !b) return 0;
    return strcoll(a, b);
}

int32_t yos_versionsort(struct yos_exec_ctx *ctx, uint32_t a_pp, uint32_t b_pp)
{
    const char *a = dirent_name_from_pp(ctx, a_pp);
    const char *b = dirent_name_from_pp(ctx, b_pp);
    if (!a || !b) return 0;
    /* strverscmp is glibc; on darwin libSystem doesn't ship it. Fall
     * back to strcoll there — close enough that callers (find, ls
     * -v) still get a deterministic order. */
#if defined(__GLIBC__)
    return strverscmp(a, b);
#else
    return strcoll(a, b);
#endif
}

/* ── setproctitle(const char *fmt, ...) ──────────────────────────────
 *
 * No observable process title on wasm: we don't fork host children
 * that show up in ps. The function exists as a libc-level no-op
 * success — daemons (sshd) call it during startup, and refusing here
 * makes them fail to come up.
 *
 * Variadic — bridge.py skips it as "variadic-skipped" and emits a
 * stub. We re-bind here with a custom impl that accepts (fmt, va_ptr)
 * and ignores both. */
void yos_setproctitle(struct yos_exec_ctx *ctx, uint32_t fmt, uint32_t va_ptr)
{
    (void)ctx; (void)fmt; (void)va_ptr;
}

/* ── ___mb_cur_max(void) ──────────────────────────────────────────────
 *
 * FreeBSD-internal accessor for MB_CUR_MAX (the maximum number of
 * bytes any multibyte character can have in the current locale).
 * yos doesn't actually run a multibyte locale on the wasm side —
 * setlocale forwards to host but we don't propagate __runes-style
 * state into the guest — so the safe answer is 1 (single-byte
 * single-char encoding).
 *
 * Returning -ENOSYS here makes zsh's locale init log a noisy warning
 * and fall back to ASCII anyway; returning 1 silently does the same
 * thing without the warning. */
/* Name has triple underscore — bridge.py mangles to yos____<rest>
 * (the `yos_` prefix + the literal `___mb_cur_max` symbol name). */
int32_t yos____mb_cur_max(struct yos_exec_ctx *ctx)
{
    (void)ctx;
    return 1;
}

/* ── __swbuf(int c, FILE *fp) ─────────────────────────────────────────
 *
 * FreeBSD-internal stdio buffer-spill function. The `putc(c, fp)`
 * macro inlines a fast path that writes into the FILE's buffer; when
 * the buffer is full (or fp is unbuffered) the macro falls back to
 * `__swbuf(c, fp)` which flushes + writes c, returning c on success
 * or EOF on error.
 *
 * yos's FILE* table doesn't expose a buffer to the guest — every
 * fputc/putc goes through yos_fputc which talks straight to the
 * host fd. So __swbuf is functionally identical to yos_fputc for our
 * purposes. */
/* Name has double underscore — bridge.py mangles to yos___<rest>. */
int32_t yos___swbuf(struct yos_exec_ctx *ctx, int32_t c, uint32_t fp)
{
    extern int32_t yos_fputc(struct yos_exec_ctx *, int32_t, uint32_t);
    return yos_fputc(ctx, c, fp);
}

/* ── fstatfs(int fd, struct statfs *buf) ──────────────────────────────
 *
 * FreeBSD's struct statfs is 2344 bytes — much larger than Linux's
 * ~64-byte equivalent because it carries f_fstypename[16],
 * f_mntonname[1024], f_mntfromname[1024]. The bridge can't be a
 * mechanical struct copy.
 *
 * Real-world consumers (zsh's `df`, `du`) check f_bsize, f_blocks,
 * f_bfree, f_bavail, f_files, f_ffree, and treat the mount-path
 * strings as informational. We zero the FreeBSD struct, fd-translate
 * the wasm fd through fd_map, fstatfs the host, and copy the numeric
 * fields into the FreeBSD layout. Mount-path strings stay zero
 * (callers that need them open /proc/mounts directly anyway). */
/* `struct statfs` header location differs: Linux glibc puts it in
 * <sys/vfs.h>; macOS / FreeBSD / OpenBSD put it in <sys/mount.h>
 * (which on those platforms is the canonical home).
 *
 * The field set we read below — f_bsize, f_blocks, f_bfree, f_bavail,
 * f_files, f_ffree, f_type — is the intersection that exists on both
 * (with darwin's f_type meaning a darwin-specific fs-type enum rather
 * than Linux's __SWORD_TYPE; the guest treats f_type opaquely so
 * either is fine). */
#if defined(__linux__)
#  include <sys/vfs.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  include <sys/param.h>
#  include <sys/mount.h>
#else
#  include <sys/statfs.h>   /* POSIX-2024 fallback */
#endif

/* FreeBSD statfs field offsets (from yaml extraction). All u64 on
 * wasm32 except f_version/f_type (u32) at the head. */
#define FB_STATFS_F_VERSION_OFF   0
#define FB_STATFS_F_TYPE_OFF      4
#define FB_STATFS_F_FLAGS_OFF     8
#define FB_STATFS_F_BSIZE_OFF    16
#define FB_STATFS_F_IOSIZE_OFF   24
#define FB_STATFS_F_BLOCKS_OFF   32
#define FB_STATFS_F_BFREE_OFF    40
#define FB_STATFS_F_BAVAIL_OFF   48
#define FB_STATFS_F_FILES_OFF    56
#define FB_STATFS_F_FFREE_OFF    64
#define FB_STATFS_SIZE          2344
#define FB_STATFS_F_VERSION     0x20140518u  /* FreeBSD STATFS_VERSION */

int32_t yos_fstatfs(struct yos_exec_ctx *ctx, int32_t wfd, uint32_t buf_off)
{
    extern int yos_fd_get(struct yos_exec_ctx *, int);
    if (!buf_off || (uint64_t)buf_off + FB_STATFS_SIZE > ctx->memory_size)
        return yos_errno_neg(ctx, EFAULT);
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);

    struct statfs h;
    if (fstatfs(hfd, &h) < 0) return yos_errno_neg(ctx, errno);

    uint8_t *w = ctx->memory + buf_off;
    memset(w, 0, FB_STATFS_SIZE);
    *(uint32_t *)(w + FB_STATFS_F_VERSION_OFF) = FB_STATFS_F_VERSION;
    *(uint32_t *)(w + FB_STATFS_F_TYPE_OFF)    = (uint32_t)h.f_type;
    *(uint64_t *)(w + FB_STATFS_F_FLAGS_OFF)   = 0;  /* mount flags differ; leave 0 */
    *(uint64_t *)(w + FB_STATFS_F_BSIZE_OFF)   = (uint64_t)h.f_bsize;
    *(uint64_t *)(w + FB_STATFS_F_IOSIZE_OFF)  = (uint64_t)h.f_bsize;  /* iosize == bsize on most filesystems */
    *(uint64_t *)(w + FB_STATFS_F_BLOCKS_OFF)  = (uint64_t)h.f_blocks;
    *(uint64_t *)(w + FB_STATFS_F_BFREE_OFF)   = (uint64_t)h.f_bfree;
    *(uint64_t *)(w + FB_STATFS_F_BAVAIL_OFF)  = (uint64_t)h.f_bavail;
    *(uint64_t *)(w + FB_STATFS_F_FILES_OFF)   = (uint64_t)h.f_files;
    *(uint64_t *)(w + FB_STATFS_F_FFREE_OFF)   = (uint64_t)h.f_ffree;
    return 0;
}

/* strmode(mode_t mode, char *p) — FreeBSD libc helper that renders a
 * `mode_t` as the 11-char "drwxrwxrwx " column ls -l prints in the
 * leftmost slot. Stubbed by codegen (no glibc equivalent), so without
 * this every ls -l line started at the link-count column and looked
 * misaligned and missing the mode/perm info.
 *
 * Layout (writes exactly 11 chars, no NUL):
 *   [0]    file type:  - (regular) d (dir) l (symlink) c (char dev)
 *                       b (block dev) p (fifo) s (socket) w (whiteout)
 *   [1-3]  owner perms with setuid encoding (S/s)
 *   [4-6]  group perms with setgid encoding (S/s)
 *   [7-9]  other perms with sticky encoding (T/t)
 *   [10]   ' ' — placeholder for the "extended attribute" column
 *                FreeBSD uses ('+' when ACL present). We don't track
 *                ACLs, so always a space — same convention glibc's
 *                non-existent strmode would have used.
 *
 * Pure / stateless — no host call needed; the host's mode_t bits are
 * defined by POSIX and match the FreeBSD values we'd be matching
 * against. (Specifically: S_IRUSR..S_IXOTH = 0700..0001, S_ISUID=04000,
 * S_ISGID=02000, S_ISVTX=01000 — identical on Linux, darwin, FreeBSD.)
 * S_IFMT values differ between hosts but we receive the FreeBSD-shape
 * mode_t from the wasm guest, so test against FreeBSD's S_IF* macros
 * spelled inline (the host header's may not match). */
#define FB_S_IFMT   0170000
#define FB_S_IFIFO  0010000
#define FB_S_IFCHR  0020000
#define FB_S_IFDIR  0040000
#define FB_S_IFBLK  0060000
#define FB_S_IFREG  0100000
#define FB_S_IFLNK  0120000
#define FB_S_IFSOCK 0140000
#define FB_S_IFWHT  0160000
#define FB_S_ISUID  0004000
#define FB_S_ISGID  0002000
#define FB_S_ISVTX  0001000
#define FB_S_IRUSR  0000400
#define FB_S_IWUSR  0000200
#define FB_S_IXUSR  0000100
#define FB_S_IRGRP  0000040
#define FB_S_IWGRP  0000020
#define FB_S_IXGRP  0000010
#define FB_S_IROTH  0000004
#define FB_S_IWOTH  0000002
#define FB_S_IXOTH  0000001

void yos_strmode(struct yos_exec_ctx *ctx, uint32_t mode_in, uint32_t p_off)
{
    /* 12-byte output buffer: 10 mode chars + extended-attr char + NUL. */
    char *p = (char *)posix_wptr_range(ctx, p_off, 12);
    if (!p) return;
    unsigned mode = (unsigned)mode_in;

    switch (mode & FB_S_IFMT) {
    case FB_S_IFDIR:  p[0] = 'd'; break;
    case FB_S_IFCHR:  p[0] = 'c'; break;
    case FB_S_IFBLK:  p[0] = 'b'; break;
    case FB_S_IFREG:  p[0] = '-'; break;
    case FB_S_IFLNK:  p[0] = 'l'; break;
    case FB_S_IFSOCK: p[0] = 's'; break;
    case FB_S_IFIFO:  p[0] = 'p'; break;
    case FB_S_IFWHT:  p[0] = 'w'; break;
    default:          p[0] = '?'; break;
    }

    p[1] = (mode & FB_S_IRUSR) ? 'r' : '-';
    p[2] = (mode & FB_S_IWUSR) ? 'w' : '-';
    switch (mode & (FB_S_IXUSR | FB_S_ISUID)) {
    case 0:                         p[3] = '-'; break;
    case FB_S_IXUSR:                p[3] = 'x'; break;
    case FB_S_ISUID:                p[3] = 'S'; break;
    case FB_S_IXUSR | FB_S_ISUID:   p[3] = 's'; break;
    }

    p[4] = (mode & FB_S_IRGRP) ? 'r' : '-';
    p[5] = (mode & FB_S_IWGRP) ? 'w' : '-';
    switch (mode & (FB_S_IXGRP | FB_S_ISGID)) {
    case 0:                         p[6] = '-'; break;
    case FB_S_IXGRP:                p[6] = 'x'; break;
    case FB_S_ISGID:                p[6] = 'S'; break;
    case FB_S_IXGRP | FB_S_ISGID:   p[6] = 's'; break;
    }

    p[7] = (mode & FB_S_IROTH) ? 'r' : '-';
    p[8] = (mode & FB_S_IWOTH) ? 'w' : '-';
    switch (mode & (FB_S_IXOTH | FB_S_ISVTX)) {
    case 0:                         p[9] = '-'; break;
    case FB_S_IXOTH:                p[9] = 'x'; break;
    case FB_S_ISVTX:                p[9] = 'T'; break;
    case FB_S_IXOTH | FB_S_ISVTX:   p[9] = 't'; break;
    }

    p[10] = ' ';
    p[11] = '\0';
}

/* explicit_bzero(ptr, n) — zero out memory in a way the compiler may
 * not elide. Used by ssh / sshd / ssh-keygen to wipe key material
 * after use. The host-API extractor misses the declaration on darwin
 * (it lives behind a feature-test the extractor doesn't define), so
 * codegen leaves a no-op void stub that drops the call on the floor.
 * That doesn't crash anything but it silently defeats the secret-
 * scrubbing the caller asked for. Implement it as a memset through a
 * volatile pointer so the optimiser can't see the result is dead.
 */
void yos_explicit_bzero(struct yos_exec_ctx *ctx, uint32_t buf_off,
                        uint32_t n)
{
    if (!n) return;
    /* Wrap-safe range validation — pre-fix used uint32 buf_off+n which
     * wrapped to a small value for large inputs and let the zero loop
     * stomp outside wasm memory. */
    volatile uint8_t *p = (volatile uint8_t *)posix_wptr_range(ctx, buf_off, n);
    if (!p) return;
    while (n--) *p++ = 0;
}

/* getservbyname / getservbyport — services-DB lookup.
 *
 * The codegen leaves these as a returns-0 stub ("complex arg/return
 * types") because the host `struct servent` is pointer-heavy (s_name,
 * s_aliases, s_proto are all host char* the auto-bridge can't translate
 * to wasm offsets). Hand-bridge: call host getservby*, then marshal
 * the result into a per-ctx wasm slab that mirrors the FreeBSD-i386
 * layout (16 bytes: s_name, s_aliases, s_port, s_proto).
 *
 * ssh / sshd resolve symbolic Port and ListenAddress entries via these
 * calls. A stub silently coerces every "Port ssh" or "-p ssh" to the
 * compiled-in fallback 22, masking real services-file misconfiguration.
 */
#include <netdb.h>

#define WASM_SERVENT_SZ 16u

extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);

/* Copy a NUL-terminated host string into the wasm-side servent slab at
 * `*cursor` and return its wasm offset (or 0 if no room / NULL). Bumps
 * *cursor on a successful pack. Replaces a GCC statement-expression
 * macro that didn't compile on MSVC. */
static uint32_t yos_servent_pack_str(const char *s, uint8_t *base,
                                     uint32_t slab_off, uint32_t slab_sz,
                                     uint32_t *cursor)
{
    if (!s) return 0;
    size_t n = strlen(s) + 1;
    if (*cursor + n > slab_sz) return 0;
    memcpy(base + *cursor, s, n);
    uint32_t off = slab_off + *cursor;
    *cursor += (uint32_t)n;
    return off;
}

static uint32_t pack_servent(struct yos_exec_ctx *ctx,
                             struct yos_exec_ctx *anchor_ctx,
                             struct servent *se)
{
    (void)anchor_ctx;
    if (!se) return 0;
    /* Per-call slab. ssh holds the result only across one or two
     * field reads, so reusing a single buffer per ctx is safe. We
     * size for: 16-byte servent + s_name + s_proto + null-terminated
     * aliases array of 4-byte wasm offsets + each alias string.
     * 1 KiB is more than any /etc/services entry needs. */
    static uint32_t slab_off; /* cached across calls (per-process) */
    static const uint32_t SLAB_SZ = 1024;
    if (!slab_off) {
        slab_off = yos_malloc(ctx, SLAB_SZ);
        if (!slab_off) return 0;
    }
    uint8_t *base = ctx->memory + slab_off;
    /* Layout: [0..16) = servent, [16..) = packed strings + aliases array. */
    uint32_t cursor = WASM_SERVENT_SZ;

    /* Helper: copy NUL-terminated string `s` into the slab at `*cursor`
     * and return its wasm offset, or 0 if NULL/no room. Bumps *cursor on
     * success. Written as a regular function (not a GCC statement-
     * expression macro) so MSVC compiles it. */
    #define SLAB_PACK(s)  yos_servent_pack_str((s), base, slab_off, SLAB_SZ, &cursor)

    uint32_t name_off  = SLAB_PACK(se->s_name);
    uint32_t proto_off = SLAB_PACK(se->s_proto);

    /* Aliases array: NULL-terminated array of char*; pack each alias
     * string, then write a wasm-offset array. Most lookups have no
     * aliases — that case just writes a single 0 pointer. */
    uint32_t aliases_off = 0;
    if (se->s_aliases) {
        int n = 0;
        for (char **a = se->s_aliases; *a; a++) n++;
        uint32_t arr_bytes = (uint32_t)(n + 1) * 4;
        if (cursor + arr_bytes <= SLAB_SZ) {
            aliases_off = slab_off + cursor;
            cursor += arr_bytes;
            for (int i = 0; i < n; i++) {
                uint32_t a_off = SLAB_PACK(se->s_aliases[i]);
                *(uint32_t *)(base + (aliases_off - slab_off) + (uint32_t)i*4) = a_off;
            }
            *(uint32_t *)(base + (aliases_off - slab_off) + (uint32_t)n*4) = 0;
        }
    }
    #undef SLAB_PACK

    /* s_port is already in network byte order on both host and FreeBSD;
     * no bswap. FreeBSD's s_port is `int` (4 bytes), zero-extended. */
    *(uint32_t *)(base +  0) = name_off;
    *(uint32_t *)(base +  4) = aliases_off;
    *(int32_t  *)(base +  8) = (int32_t)se->s_port;
    *(uint32_t *)(base + 12) = proto_off;
    return slab_off;
}

uint32_t yos_getservbyname(struct yos_exec_ctx *ctx, uint32_t name_off,
                           uint32_t proto_off)
{
    if (!name_off || name_off >= ctx->memory_size) return 0;
    const char *name  = (const char *)(ctx->memory + name_off);
    const char *proto = proto_off && proto_off < ctx->memory_size
                        ? (const char *)(ctx->memory + proto_off) : NULL;
    struct servent *se = getservbyname(name, proto);
    return pack_servent(ctx, ctx, se);
}

uint32_t yos_getservbyport(struct yos_exec_ctx *ctx, int32_t port_net,
                           uint32_t proto_off)
{
    const char *proto = proto_off && proto_off < ctx->memory_size
                        ? (const char *)(ctx->memory + proto_off) : NULL;
    struct servent *se = getservbyport((int)port_net, proto);
    return pack_servent(ctx, ctx, se);
}

/* timingsafe_bcmp(3) / timingsafe_memcmp(3) — OpenBSD libc primitives
 * ssh uses for MAC verification (constant-time, no timing side
 * channel). Not in glibc; not in darwin host libc either except for
 * timingsafe_bcmp which is in libSystem. The codegen stubs both with
 * ENOSYS / return -1, so every MAC check on Linux fails — visible as
 *   "ssh_dispatch_run_fatal: Connection to UNKNOWN port 0:
 *    message authentication code incorrect"
 * the first time the client tries to decrypt a server packet, and
 * as ssh-keygen's
 *   "Couldn't parse signature: missing header"
 * when checking a sig file's "-----BEGIN SSH SIGNATURE-----" magic.
 *
 * Hand-bridge both as small constant-time loops over wasm memory. */
int32_t yos_timingsafe_bcmp(struct yos_exec_ctx *ctx, uint32_t a_off,
                            uint32_t b_off, uint32_t n)
{
    if (n == 0) return 0;
    const uint8_t *a = (const uint8_t *)posix_wptr_range(ctx, a_off, n);
    const uint8_t *b = (const uint8_t *)posix_wptr_range(ctx, b_off, n);
    if (!a || !b) return 1;
    uint8_t r = 0;
    for (uint32_t i = 0; i < n; i++) r |= (uint8_t)(a[i] ^ b[i]);
    /* Contract: 0 iff equal, non-zero iff different. Don't compress
     * the running OR — the optimiser would early-exit on first diff. */
    return r != 0;
}

int32_t yos_timingsafe_memcmp(struct yos_exec_ctx *ctx, uint32_t a_off,
                              uint32_t b_off, uint32_t n)
{
    if (n == 0) return 0;
    const uint8_t *a = (const uint8_t *)posix_wptr_range(ctx, a_off, n);
    const uint8_t *b = (const uint8_t *)posix_wptr_range(ctx, b_off, n);
    if (!a || !b) return 0;
    /* OpenBSD-style constant-time memcmp: accumulate the first-differing
     * byte's sign into `res`; mask off subsequent updates with `done`.
     * Loop runs the full length on every call — no early exit. */
    int res = 0, done = 0;
    for (uint32_t i = 0; i < n; i++) {
        int diff = (int)a[i] - (int)b[i];
        int mask = done - 1;        /* all-ones while done==0, zero after */
        res |= diff & mask;
        done |= ((diff != 0) ? 1 : 0);
    }
    return res;
}

/* readpassphrase(3) — BSD libc function ssh / sshd / ssh-add use to
 * prompt the user for a key passphrase. Not in glibc; on darwin it's
 * in libSystem but the host-API extractor doesn't pick it up either,
 * so it lands as an unresolved env import the first time ssh meets
 * an encrypted host-key entry. Hand-bridge the OpenBSD-derived
 * implementation: open /dev/tty, save termios, disable echo, read a
 * line, restore termios, return buf.
 */
#include <ctype.h>

#define FBSD_RPP_ECHO_OFF    0x00
#define FBSD_RPP_ECHO_ON     0x01
#define FBSD_RPP_REQUIRE_TTY 0x02
#define FBSD_RPP_FORCELOWER  0x04
#define FBSD_RPP_FORCEUPPER  0x08
#define FBSD_RPP_SEVENBIT    0x10
#define FBSD_RPP_STDIN       0x20

uint32_t yos_readpassphrase(struct yos_exec_ctx *ctx,
                            uint32_t prompt_off, uint32_t buf_off,
                            uint32_t bufsize, int32_t flags)
{
    if (bufsize == 0) return 0;
    char *buf = (char *)posix_wptr_range(ctx, buf_off, bufsize);
    if (!buf) return 0;
    const char *prompt =
        (prompt_off && prompt_off < ctx->memory_size)
        ? (const char *)(ctx->memory + prompt_off) : "";

    /* Pick input/output fds. Default is /dev/tty (so the read works
     * even when stdin is a pipe — which is how ssh invokes us inside
     * a fork+execve chain). RPP_STDIN forces the stdin fallback. */
    int input  = -1;
    int output = -1;
    int input_opened = 0;
    if (!(flags & FBSD_RPP_STDIN)) {
        input = open("/dev/tty", O_RDWR | O_CLOEXEC);
        if (input >= 0) {
            output = input;
            input_opened = 1;
        }
    }
    if (input < 0) {
        if (flags & FBSD_RPP_REQUIRE_TTY) {
            errno = ENOTTY;
            return 0;
        }
        /* Fall back to host stderr/stdin via the guest's wfd 0/2 → host
         * fd mapping. yos remaps these at startup to host fd >= 3
         * dups of the original 0/1/2 so close in the guest can't
         * trample our prompt sink. */
        input  = ctx->fd_map[0] >= 0 ? ctx->fd_map[0] : 0;
        output = ctx->fd_map[2] >= 0 ? ctx->fd_map[2] : 2;
    }

    /* Disable echo on the input descriptor while we read. On a fd
     * that isn't a tty (RPP_STDIN through a pipe), tcgetattr fails
     * and we just read as-is. */
    struct termios save_t, new_t;
    int restore = 0;
    if (tcgetattr(input, &save_t) == 0) {
        new_t = save_t;
        if (!(flags & FBSD_RPP_ECHO_ON))
            new_t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL);
        new_t.c_lflag |= ICANON;
        new_t.c_iflag |= ICRNL;
        if (tcsetattr(input, TCSAFLUSH, &new_t) == 0)
            restore = 1;
    }

    /* Emit the prompt. */
    if (prompt && prompt[0])
        (void)!write(output, prompt, strlen(prompt));

    /* Read a line, char by char, until newline / EOF / buf full. */
    size_t i = 0;
    char c;
    for (;;) {
        ssize_t n = read(input, &c, 1);
        if (n <= 0) break;
        if (c == '\n' || c == '\r') break;
        if (flags & FBSD_RPP_SEVENBIT) c &= 0x7f;
        if (flags & FBSD_RPP_FORCELOWER) c = (char)tolower((unsigned char)c);
        if (flags & FBSD_RPP_FORCEUPPER) c = (char)toupper((unsigned char)c);
        if (i + 1 < bufsize) buf[i++] = c;
    }
    buf[i] = '\0';

    /* Restore termios and echo the newline the user couldn't see. */
    if (restore) {
        (void)tcsetattr(input, TCSAFLUSH, &save_t);
        if (!(flags & FBSD_RPP_ECHO_ON))
            (void)!write(output, "\n", 1);
    }
    if (input_opened) close(input);
    return buf_off;
}
