/* impl/libc/posix-linux.c — Linux-host POSIX bridges with body-level
 * divergence from BSD-lineage hosts.
 *
 * Contains:
 *   - freebsd_sockaddr_to_host  — strip the BSD sa_len byte so the
 *                                  host's uint16-at-offset-0 sa_family
 *                                  reader sees the right value.
 *   - read_host_sa_family       — Linux uses uint16_t little-endian at
 *                                  byte 0|1.
 *   - termios_fb_to_lx,
 *     termios_lx_to_fb          — FreeBSD ↔ Linux termios bit layout
 *                                  is genuinely different; convert via
 *                                  per-flag map tables.
 *   - yos_accept4               — Linux has accept4(2) natively.
 *
 * NO #ifdef inside this file — meson selects it only on linux hosts.
 * darwin uses posix-darwin.c (identity passthroughs / no-ops / ENOSYS).
 */

#define _GNU_SOURCE
#include "yos/types.h"
#include "impl/errno_helpers.h"
#include "impl/libc/posix-internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

extern int32_t yos_fd_get(struct yos_exec_ctx *ctx, int32_t wfd);
extern int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);

uint16_t read_host_sa_family(const uint8_t *host_buf)
{
    return (uint16_t)(host_buf[0] | (host_buf[1] << 8));
}

void freebsd_sockaddr_to_host(uint8_t *buf, socklen_t len)
{
    if (len >= 2) {
        uint8_t sin_family_byte = buf[1];
        /* Linux: sa_family at offset 0/1 as uint16 little-endian. Put
         * the FreeBSD sa_family byte into the low half, zero the high. */
        buf[0] = sin_family_byte;
        buf[1] = 0;
    }
}

/* ── termios flag bitmap conversion ──────────────────────────────────
 *
 * FreeBSD c_iflag bits (sys/_termios.h):
 *   IGNBRK 0x0001, BRKINT 0x0002, IGNPAR 0x0004, PARMRK 0x0008,
 *   INPCK 0x0010, ISTRIP 0x0020, INLCR 0x0040, IGNCR 0x0080,
 *   ICRNL 0x0100, IXON 0x0200, IXOFF 0x0400, IXANY 0x0800,
 *   IMAXBEL 0x2000
 * Linux iflag (bits/termios-c_iflag.h):
 *   IGNBRK 0001, BRKINT 0002, IGNPAR 0004, PARMRK 0010,
 *   INPCK 0020, ISTRIP 0040, INLCR 0100, IGNCR 0200,
 *   ICRNL 0400, IUCLC 01000, IXON 02000, IXANY 04000,
 *   IXOFF 010000, IMAXBEL 020000
 * — first six bits identical, then offsets diverge. Same shape for the
 *   other three flag words. */

struct flag_map { uint32_t fb; uint32_t lx; };

static const struct flag_map iflag_map[] = {
    {0x0001, 0000001}, /* IGNBRK */
    {0x0002, 0000002}, /* BRKINT */
    {0x0004, 0000004}, /* IGNPAR */
    {0x0008, 0000010}, /* PARMRK */
    {0x0010, 0000020}, /* INPCK  */
    {0x0020, 0000040}, /* ISTRIP */
    {0x0040, 0000100}, /* INLCR  */
    {0x0080, 0000200}, /* IGNCR  */
    {0x0100, 0000400}, /* ICRNL  */
    {0x0200, 0002000}, /* IXON   */
    {0x0400, 0010000}, /* IXOFF  */
    {0x0800, 0004000}, /* IXANY  */
    {0x2000, 0020000}, /* IMAXBEL*/
};

static const struct flag_map oflag_map[] = {
    {0x0001, 0000001}, /* OPOST */
    {0x0002, 0000004}, /* ONLCR */
    {0x0004, 0000040}, /* OXTABS / XTABS */
    {0x0008, 0000020}, /* ONOEOT / OFILL — best-effort */
    {0x0010, 0000010}, /* OCRNL  */
    {0x0020, 0000100}, /* ONOCR  */
    {0x0040, 0000200}, /* ONLRET */
};

static const struct flag_map cflag_map[] = {
    {0x0100, 0000020}, /* CS6 */
    {0x0200, 0000040}, /* CS7 */
    {0x0300, 0000060}, /* CS8 */
    {0x0400, 0000100}, /* CSTOPB */
    {0x0800, 0000200}, /* CREAD  */
    {0x1000, 0000400}, /* PARENB */
    {0x2000, 0001000}, /* PARODD */
    {0x4000, 0002000}, /* HUPCL  */
    {0x8000, 0004000}, /* CLOCAL */
};

static const struct flag_map lflag_map[] = {
    {0x00000001, 0004000}, /* ECHOKE */
    {0x00000002, 0000020}, /* ECHOE  */
    {0x00000004, 0000040}, /* ECHOK  */
    {0x00000008, 0000010}, /* ECHO   */
    {0x00000010, 0000100}, /* ECHONL */
    {0x00000020, 0002000}, /* ECHOPRT*/
    {0x00000040, 0001000}, /* ECHOCTL*/
    {0x00000080, 0000001}, /* ISIG   */
    {0x00000100, 0000002}, /* ICANON */
    {0x00000400, 0100000}, /* IEXTEN */
    {0x00000800, 0200000}, /* EXTPROC*/
    {0x00400000, 0000400}, /* TOSTOP */
    {0x00800000, 0010000}, /* FLUSHO */
    {0x20000000, 0040000}, /* PENDIN */
    {0x80000000u,0000200}, /* NOFLSH */
};

static uint32_t map_flags(uint32_t v, const struct flag_map *m, size_t n,
                          int fb_to_lx)
{
    uint32_t r = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t src = fb_to_lx ? m[i].fb : m[i].lx;
        uint32_t dst = fb_to_lx ? m[i].lx : m[i].fb;
        if (v & src) r |= dst;
    }
    return r;
}

/* FreeBSD-side c_cc[] index count. Mirrors YOS_FBSD_NCCS in posix.c. */
#define YOS_FBSD_NCCS         20
#define YOS_FBSD_TERMIOS_SIZE 44

void termios_fb_to_lx(struct termios *h, const uint8_t *w)
{
    uint32_t iflag = *(uint32_t *)(w +  0);
    uint32_t oflag = *(uint32_t *)(w +  4);
    uint32_t cflag = *(uint32_t *)(w +  8);
    uint32_t lflag = *(uint32_t *)(w + 12);
    const uint8_t *cc = w + 16;
    uint32_t ispeed = *(uint32_t *)(w + 36);
    uint32_t ospeed = *(uint32_t *)(w + 40);

    memset(h, 0, sizeof *h);
    h->c_iflag = map_flags(iflag, iflag_map,
                           sizeof iflag_map/sizeof iflag_map[0], 1);
    h->c_oflag = map_flags(oflag, oflag_map,
                           sizeof oflag_map/sizeof oflag_map[0], 1);
    h->c_cflag = map_flags(cflag, cflag_map,
                           sizeof cflag_map/sizeof cflag_map[0], 1);
    h->c_lflag = map_flags(lflag, lflag_map,
                           sizeof lflag_map/sizeof lflag_map[0], 1);
    for (int i = 0; i < YOS_FBSD_NCCS; i++) {
        int li = cc_fb_to_lx(i);
        if (li >= 0 && li < NCCS) h->c_cc[li] = cc[i];
    }
    cfsetispeed(h, ispeed);
    cfsetospeed(h, ospeed);
}

void termios_lx_to_fb(uint8_t *w, const struct termios *h)
{
    memset(w, 0, YOS_FBSD_TERMIOS_SIZE);
    *(uint32_t *)(w +  0) = map_flags(h->c_iflag, iflag_map,
                                      sizeof iflag_map/sizeof iflag_map[0], 0);
    *(uint32_t *)(w +  4) = map_flags(h->c_oflag, oflag_map,
                                      sizeof oflag_map/sizeof oflag_map[0], 0);
    *(uint32_t *)(w +  8) = map_flags(h->c_cflag, cflag_map,
                                      sizeof cflag_map/sizeof cflag_map[0], 0);
    *(uint32_t *)(w + 12) = map_flags(h->c_lflag, lflag_map,
                                      sizeof lflag_map/sizeof lflag_map[0], 0);
    uint8_t *cc = w + 16;
    for (int i = 0; i < YOS_FBSD_NCCS; i++) {
        int li = cc_fb_to_lx(i);
        if (li >= 0 && li < NCCS) cc[i] = h->c_cc[li];
    }
    *(uint32_t *)(w + 36) = (uint32_t)cfgetispeed(h);
    *(uint32_t *)(w + 40) = (uint32_t)cfgetospeed(h);
}

int32_t yos_accept4(struct yos_exec_ctx *ctx, int32_t wfd,
                    uint32_t addr_off, uint32_t addrlen_off, int32_t flags)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);
    int hflags = 0;
    if (flags & 0x10000000) hflags |= SOCK_CLOEXEC;   /* FreeBSD SOCK_CLOEXEC */
    if (flags & 0x20000000) hflags |= SOCK_NONBLOCK;  /* FreeBSD SOCK_NONBLOCK */
    int new_hfd = accept4(hfd, NULL, NULL, hflags);
    (void)addr_off; (void)addrlen_off;
    if (new_hfd < 0) return yos_errno_neg(ctx, errno);
    int new_wfd = yos_fd_alloc(ctx, new_hfd);
    if (new_wfd < 0) { close(new_hfd); return yos_errno_neg(ctx, EMFILE); }
    return new_wfd;
}
