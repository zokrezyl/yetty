/* impl/libc/posix-windows.c — Windows host POSIX bridges.
 *
 * MinGW-w64 / msvcrt ship a partial POSIX surface; Windows native
 * sockets via winsock2 use a Linux-style two-byte sa_family at offset
 * 0 (no sa_len byte), so we use the Linux strip-sa_len behaviour.
 *
 * termios: Windows console mode is not termios. Wasm callers that
 * need cooked-mode toggling won't get the real thing here; we keep
 * the byte-shuffling layout-compatible with the FreeBSD wasm side so
 * tcgetattr/tcsetattr round-trip the data but the host kernel never
 * sees it. termios_fb_to_lx zeroes the host struct; termios_lx_to_fb
 * reads back what little we have from a scratch all-zero state.
 *
 * accept4 is a Linux-ism; on Windows we emulate the NONBLOCK flag via
 * ioctlsocket FIONBIO after a plain accept().
 *
 * NO #ifdef in this file — meson selects it only on windows hosts.
 */

#include "yos/types.h"
#include "impl/errno_helpers.h"
#include "impl/libc/posix-internal.h"

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

extern int32_t yos_fd_get  (struct yos_exec_ctx *ctx, int32_t wfd);
extern int32_t yos_fd_alloc(struct yos_exec_ctx *ctx, int host_fd);

uint16_t read_host_sa_family(const uint8_t *host_buf);

static void host_sockaddr_to_freebsd(uint8_t *out, const struct sockaddr *src,
                                     socklen_t len)
{
    memcpy(out, src, len);
    if (len >= 2) {
        uint16_t fam = read_host_sa_family(out);
        out[0] = (uint8_t)len;
        out[1] = (uint8_t)(fam & 0xff);
    }
}

uint16_t read_host_sa_family(const uint8_t *host_buf)
{
    /* Winsock: same as Linux — uint16 little-endian at offset 0|1. */
    return (uint16_t)(host_buf[0] | (host_buf[1] << 8));
}

void freebsd_sockaddr_to_host(uint8_t *buf, socklen_t len)
{
    if (len >= 2) {
        uint8_t sa_family_byte = buf[1];
        buf[0] = sa_family_byte;
        buf[1] = 0;
    }
}

/* MinGW provides struct termios in <termios.h> via pthread / mintty
 * shims sometimes, but the safe assumption for a pure Windows build
 * is that termios is unavailable. The codegen-produced posix.c
 * declares the converter signatures with `struct termios *` from the
 * host's <termios.h>; we therefore avoid touching it here and accept
 * the converters operate on opaque memory. */
#define YOS_FBSD_NCCS         20
#define YOS_FBSD_TERMIOS_SIZE 44

void termios_fb_to_lx(struct termios *h, const uint8_t *w)
{
    (void)w;
    memset(h, 0, sizeof *h);
}

void termios_lx_to_fb(uint8_t *w, const struct termios *h)
{
    (void)h;
    memset(w, 0, YOS_FBSD_TERMIOS_SIZE);
}

int32_t yos_accept4(struct yos_exec_ctx *ctx, int32_t wfd,
                    uint32_t addr_off, uint32_t addrlen_off, int32_t flags)
{
    int hfd = yos_fd_get(ctx, wfd);
    if (hfd < 0) return yos_errno_neg(ctx, EBADF);

    struct sockaddr_storage ss;
    int ss_len = (int)sizeof ss;
    struct sockaddr *addr = NULL;
    int *guest_lenp = NULL;
    int guest_len = 0;

    if (addr_off || addrlen_off) {
        if (!addr_off || !addrlen_off ||
            addr_off >= ctx->memory_size ||
            addrlen_off > ctx->memory_size - sizeof(int)) {
            return yos_errno_neg(ctx, EFAULT);
        }
        guest_lenp = (int *)(ctx->memory + addrlen_off);
        guest_len = *guest_lenp;
        if (guest_len < 0) return yos_errno_neg(ctx, EINVAL);
        if ((uint32_t)guest_len > ctx->memory_size - addr_off)
            return yos_errno_neg(ctx, EFAULT);
        addr = (struct sockaddr *)&ss;
    }

    SOCKET hsock = (SOCKET)hfd;
    SOCKET new_sock = accept(hsock, addr, addr ? &ss_len : NULL);
    if (new_sock == INVALID_SOCKET) {
        /* Preserve the underlying Winsock error category — collapsing
         * everything to EINVAL hides EWOULDBLOCK (which non-blocking
         * event loops MUST distinguish), ECONNABORTED, EBADF /
         * ENOTSOCK / EMFILE etc. */
        extern int yos_wsa_to_errno(int wsa);
        return yos_errno_neg(ctx, yos_wsa_to_errno(WSAGetLastError()));
    }

    if (flags & 0x20000000 /* FreeBSD SOCK_NONBLOCK */) {
        u_long nb = 1;
        ioctlsocket(new_sock, FIONBIO, &nb);
    }
    /* SOCK_CLOEXEC is Linux-only; Windows handle inheritance is
     * controlled per-CreateProcess via STARTUPINFO, so we drop the
     * bit silently. */

    int new_wfd = yos_fd_alloc(ctx, (int)new_sock);
    if (new_wfd < 0) {
        /* yos_fd_alloc only auto-closes the host fd on the EMFILE table-
         * full path; here new_sock is a SOCKET (not a CRT fd) and the
         * generic alloc path doesn't know to call closesocket. Release
         * it ourselves to keep accept under fd pressure from leaking
         * kernel socket objects. */
        closesocket(new_sock);
        return yos_errno_neg(ctx, EMFILE);
    }

    if (addr) {
        int copy_len = ss_len < guest_len ? ss_len : guest_len;
        if (copy_len > 0) {
            host_sockaddr_to_freebsd(ctx->memory + addr_off,
                                     (const struct sockaddr *)&ss,
                                     (socklen_t)copy_len);
        }
        *guest_lenp = ss_len;
    }
    return new_wfd;
}
