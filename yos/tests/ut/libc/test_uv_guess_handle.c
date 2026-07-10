/*
 * test_uv_guess_handle.c — reproduce libuv's uv_guess_handle algorithm
 *                          on a socketpair end and pin every probe.
 *
 * WHAT this verifies (in libuv's exact order, build-linux/wasm-pkgs/
 * libuv-1.48.0/src/src/unix/tty.c:356 uv_guess_handle):
 *
 *   1. isatty(fd) == 0          (else UV_TTY)
 *   2. fstat(fd, &st) == 0      (else UV_UNKNOWN_HANDLE — assert)
 *   3. S_ISSOCK(st.st_mode)     (else falls through wrong branch)
 *   4. getsockname succeeds AND wasm sa_family == AF_UNIX (else
 *                              libuv returns UV_UNKNOWN_HANDLE,
 *                              nvim's stream_init asserts)
 *   5. getsockopt(SO_TYPE) returns SOCK_STREAM
 *
 *   Together these are the exact set of probes uv_guess_handle uses to
 *   classify our IPC socketpair end as UV_NAMED_PIPE. Each probe is
 *   one bridge function in src/yos/impl/posix.c. If any returns the
 *   wrong value on darwin, libuv mis-classifies the fd, the embedded
 *   server's read pipeline never wires up, and we get the
 *   "ch 1 was closed by the client" failure described in
 *   tmp/nvim-runtime-issues.md.
 *
 * WHY the getsockname probe is the headline:
 *
 *   yos_getsockname in src/yos/impl/posix.c:96 reads the host's
 *   sockaddr family as `host_buf[0] | (host_buf[1] << 8)` — i.e.
 *   little-endian uint16 from offsets 0..1. That is correct for Linux
 *   (sa_family is uint16_t at offset 0) but WRONG for darwin/FreeBSD
 *   hosts where the BSD layout puts sa_len (uint8) at offset 0 and
 *   sa_family (uint8) at offset 1. On a socketpair end, host_buf[0]
 *   is sa_len (e.g. 16) and host_buf[1] is AF_UNIX (1). The buggy
 *   read yields host_fam = 16 | (1<<8) = 272; the bridge then writes
 *   `host_fam & 0xff = 16` into the wasm guest's sa_family slot.
 *   libuv reads sa_family at offset 1 (FreeBSD layout) and sees 16,
 *   not AF_UNIX → returns UV_UNKNOWN_HANDLE. This test asserts the
 *   sa_family byte the wasm guest sees is exactly AF_UNIX (1).
 *
 * Expected: exit 0, stdout contains "uv_guess_handle ok".
 *
 * Exit codes:
 *    1  socketpair() failed
 *    2  isatty true on socket          (libuv would return UV_TTY)
 *    3  fstat() returned non-zero      (libuv → UV_UNKNOWN_HANDLE)
 *    4  st.st_mode not S_IFSOCK
 *    5  getsockname returned non-zero  (libuv → UV_UNKNOWN_HANDLE)
 *    6  wasm sa_family != AF_UNIX      (THE SMOKING GUN, see above)
 *    7  getsockopt SO_TYPE non-zero
 *    8  SO_TYPE value != SOCK_STREAM
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned int   socklen_t;

#define AF_UNIX           1
#define SOCK_STREAM       1
#define SOL_SOCKET        0xffff
#define SO_TYPE           0x1008

/* FreeBSD st_mode bits (sys/stat.h). */
#define FB_S_IFMT         0170000
#define FB_S_IFSOCK       0140000

/* FreeBSD i386 struct stat layout (build-darwin/sysroot/usr/include/
 * sys/stat.h, the headers nvim/libuv was built against):
 *   off  0  st_dev      u64
 *   off  8  st_ino      u64
 *   off 16  st_nlink    u64
 *   off 24  st_mode     u16   <-- what we check
 *   ... (192-byte body before __spare)
 *
 * The bridge fills this via cv_stat_h2w in src/yos/impl/posix.c —
 * which on darwin had a stale 72-byte layout writing st_mode at
 * offset 8. That mismatch silently broke uv_guess_handle on every
 * SOCK fd (mode=0 → S_ISSOCK false → no kqueue register → embedded
 * server died after ~200 ms). Both sides now agree at offset 24. */
#define WASM32_STAT_SIZE     192
#define WASM32_STAT_MODE_OFF 24

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int, int, int, int *);

/* fstat is bound by the codegen as env.fstat with signature i(ii):
 * int fstat(int fd, void *statbuf). It is NOT in yos_imports.h
 * (filtered out of the auto-decl set), so declare here. */
__attribute__((import_module("env"), import_name("fstat")))
int fstat(int fd, void *statbuf);

/* getsockname signature i(iii): int getsockname(int fd, void *addr,
 * socklen_t *addrlen). Same situation as fstat — bound at runtime,
 * not in yos_imports.h. */
__attribute__((import_module("env"), import_name("getsockname")))
int getsockname(int fd, void *addr, socklen_t *addrlen);

__attribute__((import_module("env"), import_name("getsockopt")))
int getsockopt(int, int, int, void *, socklen_t *);

__attribute__((import_module("env"), import_name("isatty")))
int isatty(int);

__attribute__((import_module("env"), import_name("close")))
int close(int);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        say("socketpair failed\n");
        _exit(1);
    }

    /* 1. isatty */
    if (isatty(sv[0]) != 0) {
        say("isatty true on socketpair end\n");
        _exit(2);
    }
    say("isatty\n");

    /* 2. fstat — must succeed AND st_mode must report a socket. */
    unsigned char stbuf[WASM32_STAT_SIZE];
    for (int i = 0; i < WASM32_STAT_SIZE; i++) stbuf[i] = 0xa5;  /* poison */
    if (fstat(sv[0], stbuf) != 0) {
        say("fstat failed\n");
        _exit(3);
    }
    uint16_t mode = (uint16_t)(stbuf[WASM32_STAT_MODE_OFF] |
                               (stbuf[WASM32_STAT_MODE_OFF + 1] << 8));
    if ((mode & FB_S_IFMT) != FB_S_IFSOCK) {
        say("st_mode not S_IFSOCK\n");
        _exit(4);
    }
    say("fstat\n");

    /* 3. getsockname — must succeed AND wasm sa_family at offset 1
     *    must be AF_UNIX (the smoking gun for the BSD-host bug in
     *    yos_getsockname). The bridge writes:
     *      addr[0] = sa_len, addr[1] = sa_family, addr[2..] = sa_data
     *    matching FreeBSD's struct sockaddr layout. */
    unsigned char addrbuf[128];
    for (int i = 0; i < 128; i++) addrbuf[i] = 0xa5;
    socklen_t alen = sizeof(addrbuf);
    if (getsockname(sv[0], addrbuf, &alen) != 0) {
        say("getsockname failed\n");
        _exit(5);
    }
    /* sa_family lives at offset 1 in FreeBSD struct sockaddr. */
    unsigned char fam = addrbuf[1];
    if (fam != AF_UNIX) {
        /* Print what we got so a debugging human knows whether the
         * bridge gave us sa_len at this slot (BSD-host bug) or
         * something else entirely. */
        char m[64] = "wasm sa_family wrong: got=";
        /* tiny u8-to-decimal */
        char digits[4]; int dn = 0; unsigned x = fam;
        if (x == 0) { digits[dn++] = '0'; }
        while (x) { digits[dn++] = '0' + (x % 10); x /= 10; }
        int p = 0; while (m[p]) p++;
        for (int i = dn - 1; i >= 0; i--) m[p++] = digits[i];
        m[p++] = '\n'; m[p] = 0;
        say(m);
        _exit(6);
    }
    say("getsockname\n");

    /* 4. getsockopt SO_TYPE — must report SOCK_STREAM. */
    int t = -1;
    socklen_t tlen = sizeof(t);
    if (getsockopt(sv[0], SOL_SOCKET, SO_TYPE, &t, &tlen) != 0) {
        say("getsockopt SO_TYPE failed\n");
        _exit(7);
    }
    if (t != SOCK_STREAM) {
        say("SO_TYPE != SOCK_STREAM\n");
        _exit(8);
    }
    say("so_type\n");

    say("uv_guess_handle ok\n");

    close(sv[0]);
    close(sv[1]);
    _exit(0);
}
