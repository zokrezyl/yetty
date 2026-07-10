/* <sys/ioctl.h> compat. yos's host ioctl bridges on Windows route
 * to the io-windows.c slice; the constants below cover the macros
 * the cross-platform sources reference. */
#ifndef YOS_WIN_COMPAT_SYS_IOCTL_H
#define YOS_WIN_COMPAT_SYS_IOCTL_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#ifndef TIOCGWINSZ
#define TIOCGWINSZ  0x5413
#endif
#ifndef TIOCSWINSZ
#define TIOCSWINSZ  0x5414
#endif
#ifndef TIOCGPGRP
#define TIOCGPGRP   0x540f
#endif
#ifndef TIOCSPGRP
#define TIOCSPGRP   0x5410
#endif
#ifndef TIOCSCTTY
#define TIOCSCTTY   0x540e
#endif
#ifndef TIOCNOTTY
#define TIOCNOTTY   0x5422
#endif
#ifndef FIONREAD
#define FIONREAD    0x4004667f
#endif
#ifndef FIONBIO
#define FIONBIO     0x5421
#endif
#ifndef TIOCPKT
#define TIOCPKT     0x5420
#endif

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* ioctl(): real body in compat_libc.c — handles FIONBIO and tracks the
 * O_NONBLOCK bit per-fd so fcntl(F_GETFL) reflects it. */
extern int ioctl(int fd, unsigned long request, ...);

#endif
