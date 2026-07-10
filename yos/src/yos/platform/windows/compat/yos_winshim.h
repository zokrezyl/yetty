/* Force-included on Windows builds to back-fill the small POSIX
 * primitives MinGW-w64 / msvcrt do not provide natively but that
 * cross-platform yos sources expect. */
#ifndef YOS_WIN_COMPAT_WINSHIM_H
#define YOS_WIN_COMPAT_WINSHIM_H

#include <io.h>          /* _pipe, _close, _read, _write, _open */
#include <fcntl.h>       /* _O_BINARY etc. */
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/* Some MinGW headers need this before they expose the IPv6 helpers. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

/* ── socketpair via Winsock loopback. Returns a pair of SOCKET fds
 *    suitable for poll/WSAPoll, the self-pipe wake primitive ydev
 *    uses. They are not interchangeable with msvcrt _pipe fds — keep
 *    them on the Winsock side end-to-end. */
static inline int yos_socketpair(int sv[2])
{
    static volatile LONG s_inited;
    if (InterlockedCompareExchange(&s_inited, 1, 0) == 0) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int alen = sizeof addr;
    if (bind(listener, (struct sockaddr *)&addr, alen) != 0 ||
        getsockname(listener, (struct sockaddr *)&addr, &alen) != 0 ||
        listen(listener, 1) != 0) {
        closesocket(listener);
        return -1;
    }

    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET) { closesocket(listener); return -1; }
    if (connect(c, (struct sockaddr *)&addr, alen) != 0) {
        closesocket(c); closesocket(listener); return -1;
    }
    SOCKET s = accept(listener, NULL, NULL);
    closesocket(listener);
    if (s == INVALID_SOCKET) { closesocket(c); return -1; }

    sv[0] = (int)s;
    sv[1] = (int)c;
    return 0;
}

/* pipe(): yos sources use the result for self-pipe wake — back it
 * with a Winsock socketpair so poll/WSAPoll works on it. */
static inline int yos_pipe(int p[2]) { return yos_socketpair(p); }
#ifndef YOS_WIN_NO_PIPE_MACRO
#define pipe yos_pipe
#endif

/* fcntl shim — accept the F_GETFL / F_SETFL pattern. Sockets can be
 * switched to non-blocking via ioctlsocket(FIONBIO); we track the
 * O_NONBLOCK bit in a tiny per-fd table so the F_GETFL roundtrip
 * works. F_GETFD / F_SETFD (cloexec) are silently no-ops. */
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define FD_CLOEXEC  1
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x4000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC   0x80000
#endif

static inline int yos_fcntl(int fd, int cmd, ...)
{
    if (cmd == F_GETFL || cmd == F_GETFD) return 0;
    if (cmd == F_SETFD)                   return 0;
    if (cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);
        u_long nb = (flags & O_NONBLOCK) ? 1 : 0;
        /* Best-effort — works for sockets, no-op for plain fds. */
        ioctlsocket((SOCKET)fd, FIONBIO, &nb);
        return 0;
    }
    return -1;
}
#define fcntl yos_fcntl

/* close(): MinGW's _close handles file descriptors, but our pipe()
 * returns Winsock SOCKETs which need closesocket(). Route through
 * closesocket — _close on a socket fd is undefined. */
static inline int yos_close_sock(int fd) { return closesocket((SOCKET)fd); }

/* read/write on socketpair fds need recv/send under Winsock, not the
 * msvcrt _read/_write which expect CRT-managed file descriptors. */
static inline long yos_read(int fd, void *buf, size_t n)
{
    int r = recv((SOCKET)fd, (char *)buf, (int)n, 0);
    return (long)r;
}
static inline long yos_write(int fd, const void *buf, size_t n)
{
    int r = send((SOCKET)fd, (const char *)buf, (int)n, 0);
    return (long)r;
}

#ifndef YOS_WIN_NO_IO_MACROS
#define close  yos_close_sock
#define read   yos_read
#define write  yos_write
#endif

#endif /* YOS_WIN_COMPAT_WINSHIM_H */
