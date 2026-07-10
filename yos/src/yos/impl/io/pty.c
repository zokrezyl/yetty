#include "platform.h"   /* yos_plat_read / write / isatty / close */
/* impl/pty.c — pseudo-terminal bridges with sandbox fallback.
 *
 * On Linux/macOS desktop, posix_openpt() opens /dev/ptmx and returns
 * a real PTY master. Subsequent grantpt + unlockpt + ptsname all
 * work against that fd, and open(ptsname(master)) yields the slave.
 *
 * tvOS / iOS app sandboxes deny open("/dev/ptmx") with EPERM, so the
 * real call collapses; telnetd then prints "All network ports in
 * use." and exits before any traffic flows. Emulate the chain with
 * an AF_UNIX SOCK_STREAM socketpair:
 *
 *   master_hfd = sp[0], slave_hfd = sp[1]
 *   ptsname(master_hfd) → "/dev/yos-pts/N"  (synthesised path)
 *   open("/dev/yos-pts/N") → dup(slave_hfd) (via yos_pty_try_open)
 *
 * Master/slave then exchange bytes through the socketpair the same
 * way they would through a real PTY's character device. login_tty()
 * on the slave dups it onto stdin/stdout/stderr; the controlling-tty
 * ioctl is virtualised in vfs.c so the no-tty hardware doesn't fail
 * the chain.
 *
 * Limitations:
 *   - No line discipline. tcgetattr/tcsetattr on a socket return
 *     ENOTTY. zsh runs in raw-input mode — fine for telnetd which
 *     does its own IAC echo / line negotiation.
 *   - TIOCGWINSZ / TIOCSWINSZ return EINVAL on the socket. Apps
 *     default to 80x24.
 */

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#if defined(__APPLE__)
#  define _DARWIN_C_SOURCE
#endif
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/errno_helpers.h"

extern int yos_fd_alloc(struct yos_exec_ctx *, int);
extern int yos_fd_get  (struct yos_exec_ctx *, int);

/* Fake-PTY registry. Keyed on master_hfd value AND master inode/dev
 * (via fstat) so subsequent ptsname / ptsname_r calls match even when
 * the caller asks via a dup of master. */
struct pty_entry {
    int   master_hfd;
    ino_t master_ino;
    dev_t master_dev;
    int   slave_hfd;
    char *slave_path;     /* "/dev/yos-pts/N" */
    /* TIOCPKT (packet mode) state. telnetd sets it on the master via
     * ioctl(master, TIOCPKT, &on=1) at startup. In packet mode each
     * master read returns a leading status byte (TIOCPKT_FLUSHWRITE
     * etc.) before the actual data. Our socketpair has no such
     * framing; we synthesise the leading 0x00 status byte in the
     * read bridge so telnetd's main loop doesn't interpret the FIRST
     * REAL DATA BYTE as a packet flag (which made every output char
     * vanish and the FLUSHWRITE bit emit spurious IAC DM). */
    int   packet_mode;
    /* ONLCR (slave c_oflag): translate LF → CRLF on the master-side
     * read path. A real PTY does this in the kernel TTY layer; our
     * socketpair has no such layer, so we emulate. Default ON to
     * match the cooked-mode default a fresh openpt(2) hands back —
     * the line-editor of an interactive shell will tcsetattr(raw)
     * to disable, and external commands (ps, ls, echo) write LF
     * relying on the kernel to emit CRLF on their behalf. */
    int   onlcr;
    struct pty_entry *next;
};

static pthread_mutex_t g_pty_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pty_entry *g_pty_head;
static int g_pty_next_id = 1;

/* Match by master fd's inode/dev so multiple host dups of the same
 * master socket resolve to the same entry. */
static struct pty_entry *pty_lookup_by_master_locked(int master_hfd) {
    struct stat ms;
    if (yos_plat_fstat(master_hfd, &ms) < 0) return NULL;
    for (struct pty_entry *e = g_pty_head; e; e = e->next)
        if (e->master_ino == ms.st_ino && e->master_dev == ms.st_dev)
            return e;
    return NULL;
}

static struct pty_entry *pty_lookup_by_path_locked(const char *path) {
    for (struct pty_entry *e = g_pty_head; e; e = e->next)
        if (strcmp(e->slave_path, path) == 0) return e;
    return NULL;
}

/* Internal: find the entry whose master socket shares its inode/dev
 * with the given host fd. Returns NULL on miss. Must be called with
 * g_pty_lock held. */
static struct pty_entry *pty_find_master_locked(int hfd) {
    struct stat ms;
    if (yos_plat_fstat(hfd, &ms) < 0) return NULL;
    for (struct pty_entry *e = g_pty_head; e; e = e->next)
        if (e->master_ino == ms.st_ino && e->master_dev == ms.st_dev)
            return e;
    return NULL;
}

/* TIOCPKT switch. Returns 1 if hfd matched a master and the flag was
 * stored (caller's ioctl bridge returns 0); 0 on miss. */
int yos_pty_set_packet_mode(int hfd, int on) {
    pthread_mutex_lock(&g_pty_lock);
    struct pty_entry *e = pty_find_master_locked(hfd);
    if (e) {
        e->packet_mode = on ? 1 : 0;
        ydebug("pty: TIOCPKT(%d) on master hfd=%d (fake)\n", on, hfd);
    }
    int hit = (e != NULL);
    pthread_mutex_unlock(&g_pty_lock);
    return hit;
}

/* ONLCR setter — called by posix.c::yos_tcsetattr when a fake-PTY
 * slave has its termios changed. Returns 1 if hfd matched (caller
 * uses this signal to know the fd was ours), 0 on miss. */
int yos_pty_set_onlcr(int hfd, int on) {
    pthread_mutex_lock(&g_pty_lock);
    struct pty_entry *e = pty_find_master_locked(hfd);
    /* yos_pty_set_onlcr is called with EITHER end's fd — try master
     * first, then walk for a slave-fd match. */
    if (!e) {
        struct stat ss;
        if (yos_plat_fstat(hfd, &ss) == 0) {
            for (struct pty_entry *p = g_pty_head; p; p = p->next) {
                struct stat ps;
                if (yos_plat_fstat(p->slave_hfd, &ps) == 0 &&
                    ps.st_ino == ss.st_ino && ps.st_dev == ss.st_dev) {
                    e = p;
                    break;
                }
            }
        }
    }
    if (e) e->onlcr = on ? 1 : 0;
    int hit = (e != NULL);
    pthread_mutex_unlock(&g_pty_lock);
    return hit;
}

/* LF → CRLF expansion. Reads up to `cap_in` bytes into a scratch buf,
 * writes to `out` expanding bare \n to \r\n. Caller-supplied scratch
 * must be at least cap_in bytes (we read at most cap_in, may write up
 * to 2*cap_in to `out` so caller must size accordingly). Returns the
 * number of bytes written to `out`. */
static ssize_t pty_read_expand_onlcr(int hfd, char *out, size_t out_cap)
{
    /* Read at most out_cap/2 host bytes so worst-case 1-byte-LF input
     * still fits expanded as CRLF. */
    size_t host_cap = out_cap / 2;
    if (host_cap == 0) { errno = EINVAL; return -1; }
    char scratch[4096];
    if (host_cap > sizeof scratch) host_cap = sizeof scratch;
    ssize_t n = yos_plat_read(hfd, scratch, host_cap);
    if (n <= 0) return n;
    size_t o = 0;
    char prev = 0;
    for (ssize_t i = 0; i < n; i++) {
        char c = scratch[i];
        if (c == '\n' && prev != '\r') {
            out[o++] = '\r';
        }
        out[o++] = c;
        prev = c;
    }
    return (ssize_t)o;
}

/* If hfd is a fake-master in packet mode AND `buf` has room for an
 * extra leading byte, do the actual read into `buf+1` and prepend
 * 0x00 (no flags set). Also expands LF → CRLF when the slave-side
 * ONLCR flag is set (which is the cooked-mode default a fresh
 * openpt(2) returns — only cleared by tcsetattr(raw)). Real PTYs
 * do this in the kernel TTY layer; our socketpair doesn't, so the
 * translation lands here on the master-read path.
 *
 * Returns the number of bytes written into buf (including the
 * leading status byte and any expanded CRs), -1 with errno on error,
 * or -2 if hfd isn't a fake-PTY master (caller falls through). */
ssize_t yos_pty_packet_read(int hfd, void *buf, size_t cap) {
    pthread_mutex_lock(&g_pty_lock);
    struct pty_entry *e = pty_find_master_locked(hfd);
    int pkt   = (e && e->packet_mode);
    int onlcr = (e && e->onlcr);
    int is_fake_master = (e != NULL);
    pthread_mutex_unlock(&g_pty_lock);
    if (!is_fake_master) return -2;
    if (cap < 4) { errno = EINVAL; return -1; }

    char *p = (char *)buf;
    size_t header = pkt ? 1 : 0;
    if (header) p[0] = 0;  /* packet-mode status byte */

    if (onlcr) {
        ssize_t n = pty_read_expand_onlcr(hfd, p + header, cap - header);
        if (n < 0) return -1;
        return (ssize_t)(header + (size_t)n);
    }

    /* Raw-mode read — no LF/CRLF transform. */
    ssize_t n = yos_plat_read(hfd, p + header, cap - header);
    if (n < 0) return -1;
    return (ssize_t)(header + (size_t)n);
}

/* Probe whether `hfd` is one of the fake PTY pair endpoints (either
 * the master end held in fd_map after posix_openpt, or any dup of the
 * slave end). Used by yos_isatty / yos_tcgetattr / yos_tcsetattr to
 * synthesise terminal semantics that the underlying socketpair fds
 * don't have. Match via fstat ino+dev — multiple host dups of the
 * same socket share the inode/dev pair. */
int yos_pty_is_pty_fd(int hfd) {
    struct stat ms;
    if (yos_plat_fstat(hfd, &ms) < 0) return 0;
    pthread_mutex_lock(&g_pty_lock);
    for (struct pty_entry *e = g_pty_head; e; e = e->next) {
        struct stat ss;
        if (e->master_ino == ms.st_ino && e->master_dev == ms.st_dev) {
            pthread_mutex_unlock(&g_pty_lock);
            return 1;
        }
        if (yos_plat_fstat(e->slave_hfd, &ss) == 0 &&
            ss.st_ino == ms.st_ino && ss.st_dev == ms.st_dev) {
            pthread_mutex_unlock(&g_pty_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_pty_lock);
    return 0;
}

/* Called from yos_open: returns a fresh dup of the slave end on hit,
 * -1 with errno=0 on miss (caller falls through to host open). */
int yos_pty_try_open(const char *path, int flags) {
    if (strncmp(path, "/dev/yos-pts/", 13) != 0) {
        errno = 0;
        return -1;
    }
    pthread_mutex_lock(&g_pty_lock);
    struct pty_entry *e = pty_lookup_by_path_locked(path);
    if (!e) { pthread_mutex_unlock(&g_pty_lock); errno = ENOENT; return -1; }
    int dupcmd = (flags & O_CLOEXEC) ? F_DUPFD_CLOEXEC : F_DUPFD;
    int dup = fcntl(e->slave_hfd, dupcmd, 0);
    pthread_mutex_unlock(&g_pty_lock);
    return dup;
}

int32_t yos_posix_openpt(struct yos_exec_ctx *ctx, int32_t fb_flags)
{
    int host_flags = O_RDWR;
    if (fb_flags & 0x8000) host_flags |= O_NOCTTY;
    int hfd = posix_openpt(host_flags);
    if (hfd >= 0) {
        int wfd = yos_fd_alloc(ctx, hfd);
        if (wfd < 0) { yos_plat_close(hfd); return yos_errno_neg(ctx, ENFILE); }
        ydebug("posix_openpt(0x%x→0x%x) = wfd=%d hfd=%d (real)\n",
               fb_flags, host_flags, wfd, hfd);
        return wfd;
    }
    if (errno != EPERM && errno != EACCES && errno != ENOENT)
        return yos_errno_neg(ctx, errno);

    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0)
        return yos_errno_neg(ctx, errno);

    struct pty_entry *e = (struct pty_entry *)calloc(1, sizeof *e);
    if (!e) { close(sp[0]); close(sp[1]); return yos_errno_neg(ctx, ENOMEM); }

    struct stat ms;
    if (yos_plat_fstat(sp[0], &ms) < 0) {
        int saved = errno;
        free(e); close(sp[0]); close(sp[1]);
        return yos_errno_neg(ctx, saved);
    }
    e->master_hfd = sp[0];
    e->master_ino = ms.st_ino;
    e->master_dev = ms.st_dev;
    e->slave_hfd  = sp[1];
    e->onlcr      = 1;  /* cooked-mode default; cleared by tcsetattr(raw) */

    pthread_mutex_lock(&g_pty_lock);
    int id = g_pty_next_id++;
    pthread_mutex_unlock(&g_pty_lock);
    char buf[64];
    snprintf(buf, sizeof buf, "/dev/yos-pts/%d", id);
    e->slave_path = strdup(buf);

    pthread_mutex_lock(&g_pty_lock);
    e->next = g_pty_head;
    g_pty_head = e;
    pthread_mutex_unlock(&g_pty_lock);

    int wfd = yos_fd_alloc(ctx, sp[0]);
    if (wfd < 0) { close(sp[0]); return yos_errno_neg(ctx, ENFILE); }
    ydebug("posix_openpt(0x%x→0x%x) = wfd=%d hfd=%d (fake, slave=%s)\n",
           fb_flags, host_flags, wfd, sp[0], e->slave_path);
    return wfd;
}

int32_t yos_grantpt(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int rc = grantpt(hfd);
    if (rc == 0) return 0;
    /* On the socketpair-emulated master, grantpt returns ENOTTY. We
     * silently accept — there's nothing to grant on a socket. */
    if (errno == ENOTTY || errno == EINVAL || errno == EOPNOTSUPP ||
        errno == ENOTSUP || errno == ENOTSOCK) {
        ydebug("grantpt(hfd=%d) = 0 (fake)\n", hfd);
        return 0;
    }
    return yos_errno_neg(ctx, errno);
}

int32_t yos_unlockpt(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int rc = unlockpt(hfd);
    if (rc == 0) return 0;
    if (errno == ENOTTY || errno == EINVAL || errno == EOPNOTSUPP ||
        errno == ENOTSUP || errno == ENOTSOCK) {
        ydebug("unlockpt(hfd=%d) = 0 (fake)\n", hfd);
        return 0;
    }
    return yos_errno_neg(ctx, errno);
}

#define YOS_PTSNAME_BUF 256
uint32_t yos_ptsname(struct yos_exec_ctx *ctx, int32_t wfd)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_null(ctx, EBADF);

    char *hs = ptsname(hfd);
    char fake_path[YOS_PTSNAME_BUF];
    if (!hs) {
        pthread_mutex_lock(&g_pty_lock);
        struct pty_entry *e = pty_lookup_by_master_locked(hfd);
        if (e) {
            strncpy(fake_path, e->slave_path, sizeof fake_path - 1);
            fake_path[sizeof fake_path - 1] = 0;
            hs = fake_path;
        }
        pthread_mutex_unlock(&g_pty_lock);
    }
    if (!hs) return yos_errno_null(ctx, ENOTTY);

    static _Thread_local uint32_t buf_off = 0;
    extern uint32_t yos_malloc(struct yos_exec_ctx *, uint32_t);
    if (!buf_off) buf_off = yos_malloc(ctx, YOS_PTSNAME_BUF);
    if (!buf_off) return yos_errno_null(ctx, ENOMEM);
    size_t n = strlen(hs);
    if (n >= YOS_PTSNAME_BUF) n = YOS_PTSNAME_BUF - 1;
    memcpy(ctx->memory + buf_off, hs, n);
    ctx->memory[buf_off + n] = 0;
    ydebug("ptsname(wfd=%d hfd=%d) = wasm_off=0x%x \"%s\"\n",
           wfd, hfd, buf_off, hs);
    return buf_off;
}

int32_t yos_ptsname_r(struct yos_exec_ctx *ctx, int32_t wfd,
                      uint32_t buf_off, uint32_t buflen)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return EBADF;
    /* uint32 wrap-safe range check. */
    if (!buf_off || buflen == 0 ||
        (uint64_t)buf_off + (uint64_t)buflen > (uint64_t)ctx->memory_size)
        return EFAULT;
    char *guest = (char *)(ctx->memory + buf_off);
    int rc = ptsname_r(hfd, guest, (size_t)buflen);
    if (rc == 0) return 0;
    /* Fake fallback. */
    pthread_mutex_lock(&g_pty_lock);
    struct pty_entry *e = pty_lookup_by_master_locked(hfd);
    if (e) {
        size_t n = strlen(e->slave_path);
        if (n >= buflen) { pthread_mutex_unlock(&g_pty_lock); return ERANGE; }
        memcpy(guest, e->slave_path, n + 1);
        pthread_mutex_unlock(&g_pty_lock);
        return 0;
    }
    pthread_mutex_unlock(&g_pty_lock);
    return ENOTTY;
}
