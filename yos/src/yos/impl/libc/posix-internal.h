#ifndef YOS_POSIX_INTERNAL_H
#define YOS_POSIX_INTERNAL_H

/* impl/libc/posix-internal.h — declarations shared between posix.c
 * and the per-platform posix-{linux,darwin}.c slices. Not public yos
 * API; not visible outside impl/libc/.
 *
 * The slices own the platform-divergent bodies; posix.c owns the
 * shared shape (constants, the FreeBSD wasm-side enums, cc_fb_to_lx
 * which reads each host's <termios.h> NCCS macros itself). */

#include "yos/types.h"
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>

/* ── socket address conversion ─────────────────────────────────────
 *
 * Linux puts sa_family in two bytes at offset 0/1 (little-endian
 * uint16). BSD-lineage hosts (darwin, FreeBSD, *BSD) put sa_len at
 * byte 0 and sa_family at byte 1.
 *
 * read_host_sa_family():    decode the host's sa_family from the
 *                            on-the-wire layout.
 * freebsd_sockaddr_to_host: in-place rewrite a FreeBSD-shape buffer
 *                            (sa_len at 0, sa_family at 1) into the
 *                            host's expected shape (Linux: zero the
 *                            sa_len byte; BSD: nothing to do).
 */
uint16_t read_host_sa_family(const uint8_t *host_buf);
void     freebsd_sockaddr_to_host(uint8_t *buf, socklen_t len);

/* ── termios layout converters ────────────────────────────────────
 *
 * FreeBSD c_iflag/c_oflag/c_cflag/c_lflag bit positions ≠ Linux's,
 * but match darwin's verbatim (both BSD lineage). The Linux slice
 * runs the bits through a per-flag map table; the darwin slice
 * does identity passthrough. */
void termios_fb_to_lx(struct termios *h, const uint8_t *w);
void termios_lx_to_fb(uint8_t *w, const struct termios *h);

/* c_cc[] index translation — FreeBSD VEOF/VINTR/... index → host's
 * c_cc[] index. Defined in posix.c; reads each host's <termios.h>
 * VEOF/VINTR/... macros, so the result is automatically right per
 * platform. */
int cc_fb_to_lx(int fb_idx);

/* ── accept4 ─── Linux native, ENOSYS on darwin. ─────────────────── */
int32_t yos_accept4(struct yos_exec_ctx *ctx, int32_t wfd,
                    uint32_t addr_off, uint32_t addrlen_off, int32_t flags);

#endif /* YOS_POSIX_INTERNAL_H */
