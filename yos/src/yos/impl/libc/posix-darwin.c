/* impl/libc/posix-darwin.c — darwin-host POSIX bridges with bodies
 * that diverge from Linux. Covers macOS, iOS, tvOS, iOS Simulator.
 *
 * Contains:
 *   - freebsd_sockaddr_to_host  — no-op; darwin's sockaddr layout
 *                                  matches FreeBSD's verbatim
 *                                  (sa_len at byte 0, sa_family at 1).
 *   - read_host_sa_family       — darwin/BSD reads sa_family from
 *                                  byte 1 (not byte 0|1 little-endian).
 *   - termios_fb_to_lx,
 *     termios_lx_to_fb          — identity passthrough for the flag
 *                                  words; darwin's termios bit positions
 *                                  match FreeBSD's. Only the c_cc index
 *                                  translation runs.
 *   - yos_accept4               — ENOSYS; darwin has no accept4(2).
 *                                  Callers fall back to accept(2) +
 *                                  fcntl for the flags.
 *
 * NO #ifdef inside this file — meson selects it only on darwin hosts.
 * linux uses posix-linux.c.
 */

#include "yos/types.h"
#include "impl/errno_helpers.h"
#include "impl/libc/posix-internal.h"

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>

#define YOS_FBSD_NCCS         20
#define YOS_FBSD_TERMIOS_SIZE 44

uint16_t read_host_sa_family(const uint8_t *host_buf)
{
    return (uint16_t)host_buf[1];
}

void freebsd_sockaddr_to_host(uint8_t *buf, socklen_t len)
{
    (void)buf; (void)len;
    /* darwin / *BSD: layout already matches FreeBSD wasm guest
     * (sa_len uint8 @0, sa_family uint8 @1). No rewrite needed. */
}

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
    /* BSD lineage: bit positions match the FreeBSD wasm guest. The
     * map_flags table-translation that the Linux slice does would
     * actively SCRAMBLE darwin's flags (cfmakeraw's ISIG-clear would
     * write Linux ISIG=0x1, leaving darwin's ISIG=0x80 untouched and
     * the PTY stuck in cooked mode). Pass straight through. */
    h->c_iflag = iflag;
    h->c_oflag = oflag;
    h->c_cflag = cflag;
    h->c_lflag = lflag;
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
    *(uint32_t *)(w +  0) = (uint32_t)h->c_iflag;
    *(uint32_t *)(w +  4) = (uint32_t)h->c_oflag;
    *(uint32_t *)(w +  8) = (uint32_t)h->c_cflag;
    *(uint32_t *)(w + 12) = (uint32_t)h->c_lflag;
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
    (void)ctx; (void)wfd; (void)addr_off; (void)addrlen_off; (void)flags;
    return yos_errno_neg(ctx, ENOSYS);
}
