/*
 * test_kqueue_evfilt_read.c — kqueue + EVFILT_READ on a socketpair end.
 *
 * WHAT this verifies:
 *   The wasm guest creates a kqueue (env.kqueue), creates a socketpair,
 *   builds a 64-byte FreeBSD-flavored kevent struct that registers
 *   EV_ADD|EVFILT_READ on one end, calls kevent() in apply-changes mode
 *   (nevents=0), writes one byte on the other end, then calls kevent()
 *   in poll mode (nevents=1, 100ms timeout). The poll must return 1
 *   event with ident == watched_fd, filter == EVFILT_READ.
 *
 * WHY this matters:
 *   The decisive observation in tmp/nvim-runtime-issues.md is that
 *   nvim's IPC stdin (a dup'd socketpair end) never appears in the
 *   kevent changelist on darwin — uv_read_start either short-circuits
 *   or its EV_ADD never reaches the bridge. This test provides the
 *   minimal end-to-end proof that EVFILT_READ on a SOCK_STREAM fd
 *   works through our bridge: kqueue translates to host epoll/kqueue,
 *   the FreeBSD-shape kevent struct is unmarshalled correctly, and the
 *   ready notification round-trips back as a 64-byte kevent the guest
 *   can read. If this test passes but nvim still doesn't register, the
 *   bug is in libuv's UV_FILE/UV_NAMED_PIPE classification (covered by
 *   test_socketpair_sotype_isatty.c). If it fails, the bridge itself
 *   is broken on darwin.
 *
 * Expected: exit 0, stdout contains "kqueue ok", stdout contains "kevent fired".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef long long     int64_t;
typedef int           int32_t;
typedef short         int16_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uintptr_t;

/* FreeBSD kevent constants (same numeric values on darwin). */
#define AF_UNIX           1
#define SOCK_STREAM       1

#define EVFILT_READ       (-1)
#define EV_ADD            0x0001

/* FreeBSD struct kevent layout — 64 bytes on wasm32. Mirrors the
 * KE_*_OFF table in src/yos/impl/kqueue.c. Hand-rolled to keep the
 * test header-free. */
struct fb_kevent {
    uint32_t ident;       /* +0  */
    int16_t  filter;      /* +4  */
    uint16_t flags;       /* +6  */
    uint32_t fflags;      /* +8  */
    uint32_t _pad12;      /* +12 */
    int64_t  data;        /* +16 */
    uint32_t udata;       /* +24 */
    uint32_t _pad[9];     /* +28..63 */
};

struct fb_timespec {
    int64_t  tv_sec;
    int32_t  tv_nsec;
};

__attribute__((import_module("env"), import_name("kqueue")))
int kqueue(void);

__attribute__((import_module("env"), import_name("kevent")))
int kevent(int kq, const struct fb_kevent *changelist, int nchanges,
           struct fb_kevent *eventlist, int nevents,
           const struct fb_timespec *timeout);

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int, int, int, int *);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("close")))
int close(int);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int kq = kqueue();
    if (kq < 0) { say("kqueue failed\n"); _exit(1); }
    say("kqueue ok\n");

    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        say("socketpair failed\n");
        _exit(2);
    }

    /* Register EVFILT_READ on sv[0]. Zero-initialise; only ident,
     * filter, flags matter for this case. */
    struct fb_kevent change = { 0 };
    change.ident  = (uint32_t)sv[0];
    change.filter = EVFILT_READ;
    change.flags  = EV_ADD;
    change.udata  = 0xdeadbeef;

    int r = kevent(kq, &change, 1, (struct fb_kevent *)0, 0,
                   (struct fb_timespec *)0);
    if (r < 0) {
        say("kevent register failed\n");
        _exit(3);
    }

    /* Push one byte across the pair so EVFILT_READ becomes ready. */
    if (write(sv[1], "x", 1) != 1) {
        say("write to peer failed\n");
        _exit(4);
    }

    /* Poll, 100 ms timeout — way more than enough on a loopback pair. */
    struct fb_kevent fired = { 0 };
    struct fb_timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
    r = kevent(kq, (const struct fb_kevent *)0, 0, &fired, 1, &ts);
    if (r < 0) {
        say("kevent poll failed\n");
        _exit(5);
    }
    if (r == 0) {
        say("kevent poll timed out\n");
        _exit(6);
    }
    if (fired.ident != (uint32_t)sv[0]) {
        say("fired ident mismatch\n");
        _exit(7);
    }
    if (fired.filter != EVFILT_READ) {
        say("fired filter mismatch\n");
        _exit(8);
    }
    say("kevent fired\n");

    close(sv[0]);
    close(sv[1]);
    close(kq);
    _exit(0);
}
