/*
 * pty_echo_fork.c — fork(), then in PARENT register stdin on a kqueue
 *                   and read bytes through it.
 *
 * Mirrors nvim's structure: a forked layout where the surviving
 * "TUI parent" reads keystrokes from stdin via libuv's kqueue. nvim
 * was observed registering fd 0 on kqueue post-fork but kevent never
 * firing for it — this test is the minimal reproducer.
 *
 * Selector first byte: ignored — always uses kqueue.
 *
 * The CHILD just exits 0 immediately so waitpid below succeeds. The
 * meaningful work all happens in the PARENT. Echoes each byte
 * <wrapped> like pty_echo. 'q' or 0x03 terminates.
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
struct fb_timespec { int64_t tv_sec; int32_t tv_nsec; };

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
__attribute__((import_module("env"), import_name("fork")))
int fork(void);
__attribute__((import_module("env"), import_name("waitpid")))
int waitpid(int, int *, int);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

static void echo_one(unsigned char c) {
    if (c == 'q')    { say("QUIT\n");   _exit(0); }
    if (c == 0x03)   { say("CTRL-C\n"); _exit(0); }
    char buf[3] = { '<', (char)c, '>' };
    write(1, buf, 3);
}

void _start(void) {
    /* Drop the selector byte from the test driver. */
    unsigned char sel;
    read(0, &sel, 1);

    /* Register fd 0 on the kqueue BEFORE forking. This is the order
     * nvim's TUI uses (rstream_init_fd → uv_read_start happens during
     * tinput_init in the TUI parent, then later the embedded server
     * is forked). If the parent's kqueue registration survives the
     * fork and still fires for fd 0, this test passes. */
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

    int pid = fork();
    if (pid < 0) { say("fork failed\n"); _exit(1); }
    if (pid == 0) {
        /* Child does nothing — exits to free up the parent's
         * waitpid path and to mirror nvim's embedded-server
         * existence (though it does much more there). */
        _exit(0);
    }

    /* Reap the child so it doesn't sit as a zombie while the
     * parent's input loop runs. */
    int status = 0;
    waitpid(pid, &status, 0);

    say("READY-fork-kqueue\n");

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
