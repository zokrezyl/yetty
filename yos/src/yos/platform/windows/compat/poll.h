/* Minimal <poll.h> shim for MinGW-w64.
 *
 * Provides struct pollfd / POLLIN / poll(). Backed by WSAPoll for
 * socket fds. ydev callers use it on socketpair-backed self-pipes
 * (created via the yos_winshim.h pipe() rewrite), where WSAPoll is
 * the right thing. */
#ifndef YOS_WIN_COMPAT_POLL_H
#define YOS_WIN_COMPAT_POLL_H

#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

/* Winsock declares POLLIN as POLLRDNORM|POLLRDBAND (0x0300), POLLOUT
 * as 0x0010, etc. — values that disagree with FreeBSD and Linux. yos's
 * wasm guest is FreeBSD-shaped, so the bridge in impl/io/dir.c passes
 * FreeBSD POLLIN=0x0001 / POLLOUT=0x0004 through `events`. We force
 * the FreeBSD/Linux values here so callers see consistent constants
 * across platforms; the compat poll() body in compat_libc.c translates
 * to Windows poll codes only where it actually calls WSAPoll. */
#undef POLLIN
#undef POLLPRI
#undef POLLOUT
#undef POLLERR
#undef POLLHUP
#undef POLLNVAL
#undef POLLRDNORM
#undef POLLRDBAND
#undef POLLWRNORM
#undef POLLWRBAND
#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020
#define POLLRDNORM  0x0040
#define POLLRDBAND  0x0080
#define POLLWRNORM  0x0100
#define POLLWRBAND  0x0200

/* MinGW's winsock2.h declares `struct pollfd` whenever
 * _WIN32_WINNT >= 0x600 (the default on modern MinGW). We pull
 * winsock2 above so the struct is in scope here; no redeclaration. */

typedef unsigned long nfds_t;

/* Real body in compat_libc.c — handles a mix of Winsock SOCKETs and
 * CRT file descriptors / pipes by routing each side appropriately. */
extern int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms);

#endif /* YOS_WIN_COMPAT_POLL_H */
