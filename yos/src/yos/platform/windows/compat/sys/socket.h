/* <sys/socket.h> compat — forward to winsock2.h which provides the
 * struct sockaddr / socklen_t / msghdr surface. */
#ifndef YOS_WIN_COMPAT_SYS_SOCKET_H
#define YOS_WIN_COMPAT_SYS_SOCKET_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#ifndef socklen_t
typedef int socklen_t;
#endif

/* Linux-only socket-creation flags. Windows accept()/socket() does not
 * apply atomic CLOEXEC / NONBLOCK on creation — callers should follow
 * up with ioctlsocket(FIONBIO) for non-blocking. We expose the bits
 * with values that don't collide with SOCK_STREAM/SOCK_DGRAM/etc. so
 * the bridge code can strip them before forwarding. */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC   0x80000
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK  0x800
#endif

/* MSG_* flags Linux defines that winsock lacks. The bridge layer
 * strips them before calling send/recv when not supported. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL   0x4000
#endif
#ifndef MSG_CMSG_CLOEXEC
#define MSG_CMSG_CLOEXEC 0x40000000
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT   0x40
#endif
#ifndef MSG_MORE
#define MSG_MORE       0x8000
#endif
#ifndef MSG_FASTOPEN
#define MSG_FASTOPEN   0x20000000
#endif

/* SO_REUSEPORT — Linux-specific, no direct Windows equivalent. */
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

/* msghdr / cmsghdr live in winsock2.h via WSABUF + WSAMSG already, but
 * the POSIX-shaped variants are also exposed via ws2tcpip.h on recent
 * SDKs. Define only the SCM_* constants and the CMSG_* macros (which
 * winsock provides under WSA_-prefixed forms). */
#ifndef SCM_RIGHTS
#define SCM_RIGHTS      1
#define SCM_CREDENTIALS 2
#define SCM_TIMESTAMP   3
#endif
#ifndef CMSG_ALIGN
#define CMSG_ALIGN(len)  (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#endif

#endif
