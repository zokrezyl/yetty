/* <termios.h> compat — MSVC has none. Windows console mode is not POSIX
 * termios; the yos host slice (impl/libc/posix-windows.c) converts wasm
 * guest termios calls into best-effort no-ops, but the struct + flag
 * macros still need to exist so generated bridges parse. */
#ifndef YOS_WIN_COMPAT_TERMIOS_H
#define YOS_WIN_COMPAT_TERMIOS_H

#include <sys/types.h>
#include <stdint.h>

typedef unsigned int   tcflag_t;
typedef unsigned char  cc_t;
typedef unsigned int   speed_t;

#define NCCS  20

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

/* c_iflag bits */
#define IGNBRK   0x000001
#define BRKINT   0x000002
#define IGNPAR   0x000004
#define PARMRK   0x000008
#define INPCK    0x000010
#define ISTRIP   0x000020
#define INLCR    0x000040
#define IGNCR    0x000080
#define ICRNL    0x000100
#define IUCLC    0x000200
#define IXON     0x000400
#define IXANY    0x000800
#define IXOFF    0x001000
#define IMAXBEL  0x002000
#define IUTF8    0x004000

/* c_oflag bits */
#define OPOST    0x000001
#define OLCUC    0x000002
#define ONLCR    0x000004
#define OCRNL    0x000008
#define ONOCR    0x000010
#define ONLRET   0x000020

/* c_cflag bits */
#define CS5      0x0000
#define CS6      0x0010
#define CS7      0x0020
#define CS8      0x0030
#define CSIZE    0x0030
#define CSTOPB   0x0040
#define CREAD    0x0080
#define PARENB   0x0100
#define PARODD   0x0200
#define HUPCL    0x0400
#define CLOCAL   0x0800

/* c_lflag bits */
#define ISIG     0x000001
#define ICANON   0x000002
#define ECHO     0x000008
#define ECHOE    0x000010
#define ECHOK    0x000020
#define ECHONL   0x000040
#define NOFLSH   0x000080
#define TOSTOP   0x000100
#define ECHOCTL  0x000200
#define ECHOPRT  0x000400
#define ECHOKE   0x000800
#define FLUSHO   0x001000
#define PENDIN   0x004000
#define IEXTEN   0x008000
#define EXTPROC  0x010000

/* control characters (indexes into c_cc) */
#define VEOF     0
#define VEOL     1
#define VEOL2    2
#define VERASE   3
#define VWERASE  4
#define VKILL    5
#define VREPRINT 6
#define VINTR    8
#define VQUIT    9
#define VSUSP    10
#define VDSUSP   11
#define VSTART   12
#define VSTOP    13
#define VLNEXT   14
#define VDISCARD 15
#define VMIN     16
#define VTIME    17
#define VSTATUS  18

/* tcsetattr "when" values */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* baud rates */
#define B0        0
#define B50       50
#define B75       75
#define B110      110
#define B134      134
#define B150      150
#define B200      200
#define B300      300
#define B600      600
#define B1200     1200
#define B1800     1800
#define B2400     2400
#define B4800     4800
#define B9600     9600
#define B19200    19200
#define B38400    38400
#define B57600    57600
#define B115200   115200
#define B230400   230400

/* tcflush queue selector */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow action */
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

#ifdef __cplusplus
extern "C" {
#endif

extern int     tcgetattr(int fd, struct termios *t);
extern int     tcsetattr(int fd, int when, const struct termios *t);
extern int     tcsendbreak(int fd, int dur);
extern int     tcdrain(int fd);
extern int     tcflush(int fd, int q);
extern int     tcflow(int fd, int a);
extern speed_t cfgetispeed(const struct termios *t);
extern speed_t cfgetospeed(const struct termios *t);
extern int     cfsetispeed(struct termios *t, speed_t s);
extern int     cfsetospeed(struct termios *t, speed_t s);
extern int     cfsetspeed(struct termios *t, speed_t s);
extern void    cfmakeraw(struct termios *t);

#ifdef __cplusplus
}
#endif

#endif /* YOS_WIN_COMPAT_TERMIOS_H */
