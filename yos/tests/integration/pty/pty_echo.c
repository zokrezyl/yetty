/*
 * pty_echo.c — minimal wasm program that reads stdin and echoes to
 *              stdout. Used by tests/integration/pty/test_*.py to
 *              verify yos's TTY input path end-to-end through a real
 *              pty, the way nvim's TUI parent reads from the user's
 *              terminal.
 *
 * Two read modes selected by argv[1]:
 *
 *   "blocking" — call read(0, buf, 1) directly. Tests the bare
 *                read syscall path without kqueue. Should always
 *                deliver bytes the moment the pty has them.
 *
 *   "kqueue"   — register fd 0 on a kqueue with EVFILT_READ, then
 *                kevent() poll, then read. This is the path nvim's
 *                TUI uses (via libuv's uv_tty_init / uv_read_start).
 *                If kqueue doesn't fire on the pty slave, this hangs
 *                until the test driver SIGKILLs us.
 *
 * Echoes each byte. Special handling:
 *   'q'   → write "QUIT\n" then exit 0.
 *   0x03  → write "CTRL-C\n" then exit 0. (lets the Ctrl-C test
 *           distinguish "byte arrived" from "process killed by SIGINT".)
 *   any   → write the byte back surrounded by '<' '>' so the test
 *           sees the byte even if the pty is in cooked mode and would
 *           otherwise echo locally.
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;
typedef int            int32_t;
typedef short          int16_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#define EVFILT_READ        (-1)
#define EV_ADD             0x0001

struct fb_kevent {
    uint32_t ident;
    int16_t  filter;
    uint16_t flags;
    uint32_t fflags;
    uint32_t _pad12;
    int64_t  data;
    uint32_t udata;
    uint32_t _pad[9];
};
struct fb_timespec {
    int64_t  tv_sec;
    int32_t  tv_nsec;
};

__attribute__((import_module("env"), import_name("read")))
ssize_t read(int, void *, size_t);
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);
__attribute__((import_module("env"), import_name("kqueue")))
int kqueue(void);
__attribute__((import_module("env"), import_name("kevent")))
int kevent(int kq, const struct fb_kevent *changelist, int nchanges,
           struct fb_kevent *eventlist, int nevents,
           const struct fb_timespec *timeout);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

/* Comparison helper — single-letter mode probe in argv[1]. */
static int streq_short(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

/* argv[1] selects mode. clang/wasm-ld passes argc/argv via _start
 * indirection — we just read argv off the wasm stack the way
 * test_fork_basic etc. do (they don't, they use _start(void), but
 * yos passes argv via a dedicated bridge). For simplicity we don't
 * use argv here — instead the mode is selected by reading the FIRST
 * byte from stdin: if it is 'k' the program switches to kqueue mode,
 * else blocking. The test driver writes that selector byte first. */

static void echo_one(unsigned char c) {
    if (c == 'q') {
        say("QUIT\n");
        _exit(0);
    }
    if (c == 0x03) {            /* Ctrl-C */
        say("CTRL-C\n");
        _exit(0);
    }
    /* Wrap in <...> so the test sees the byte distinct from any pty
     * local echo. */
    char buf[3] = { '<', (char)c, '>' };
    write(1, buf, 3);
}

static void blocking_loop(void) {
    say("READY-blocking\n");
    unsigned char c;
    while (read(0, &c, 1) == 1) {
        echo_one(c);
    }
    _exit(0);
}

static void kqueue_loop(void) {
    int kq = kqueue();
    if (kq < 0) { say("kqueue failed\n"); _exit(2); }

    struct fb_kevent change = { 0 };
    change.ident  = 0;
    change.filter = EVFILT_READ;
    change.flags  = EV_ADD;
    if (kevent(kq, &change, 1, (struct fb_kevent *)0, 0,
               (struct fb_timespec *)0) < 0) {
        say("kevent EV_ADD failed\n"); _exit(3);
    }
    say("READY-kqueue\n");

    for (;;) {
        struct fb_kevent fired = { 0 };
        int n = kevent(kq, (const struct fb_kevent *)0, 0, &fired, 1,
                       (const struct fb_timespec *)0);
        if (n < 0) { say("kevent poll error\n"); _exit(4); }
        if (n == 0) continue;
        unsigned char buf[64];
        ssize_t r = read(0, buf, sizeof buf);
        if (r <= 0) _exit(0);
        for (ssize_t i = 0; i < r; i++) echo_one(buf[i]);
    }
}

void _start(void) {
    /* Read mode selector byte. The test driver sends 'B' for blocking
     * or 'K' for kqueue, then a separator character, then the actual
     * test bytes. */
    unsigned char sel;
    if (read(0, &sel, 1) != 1) {
        say("no mode selector\n");
        _exit(1);
    }
    if (sel == 'K' || sel == 'k') {
        kqueue_loop();
    } else {
        blocking_loop();
    }
}
