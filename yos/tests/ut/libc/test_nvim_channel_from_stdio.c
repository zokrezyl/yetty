/*
 * test_nvim_channel_from_stdio.c — exact reproducer for the nvim
 *                                  embedded-server failure on darwin.
 *
 * WHAT this verifies (one big test, in nvim's order):
 *
 *   This walks the precise sequence nvim's channel_from_stdio() +
 *   uv_pipe_open() + uv_read_start() execute on the IPC socketpair end
 *   that the parent dup2'd onto stdin before forking. From
 *   tmp/nvim-runtime-issues.md the receive_msgpack callback fires with
 *   eof=true ~200 ms after startup and the server exits 1; the
 *   decisive observation is that the saved IPC fd never appears in any
 *   kevent changelist. The leading hypotheses are all bridge corners:
 *
 *     a) F_DUPFD_CLOEXEC returns -1 → saved fd is bogus.
 *     b) uv_pipe_open's F_SETFL|O_NONBLOCK silently drops the bit.
 *     c) F_GETFL after F_SETFL doesn't reflect O_NONBLOCK (oflags
 *        remap loses bits in either direction).
 *     d) getsockopt(SO_TYPE) doesn't return SOCK_STREAM → libuv's
 *        uv_guess_handle classifies as UV_FILE and routes the fd to
 *        an idle handler that never calls uv__io_start → no kqueue.
 *     e) isatty() lies on a socketpair end → uv_guess_handle picks
 *        UV_TTY by mistake.
 *     f) kqueue + EVFILT_READ never fires for a socketpair fd.
 *     g) read() after kevent fire returns 0 (peer-closed) instead of
 *        the data we just wrote.
 *
 *   Each step exits with a UNIQUE code so a failing run pinpoints the
 *   broken bridge layer from the exit code alone, without having to
 *   inspect stdout. All steps print a one-word marker so a passing run
 *   produces a single ordered transcript.
 *
 * WHY this matters:
 *   The seven narrow tests added alongside this one each pin one fd
 *   bridge primitive in isolation. This test composes them in nvim's
 *   actual order — that ordering, plus the F_DUPFD_CLOEXEC step that
 *   creates a fresh host fd which then has to satisfy every subsequent
 *   probe, is what the embedded server runs through. If this test
 *   passes on darwin, the bug is above us in libuv (most likely
 *   uv_guess_handle picking UV_FILE); if it fails, the broken layer
 *   is in our bridge and the exit code says which one.
 *
 * Expected: exit 0, stdout contains "channel_from_stdio ok".
 *
 * Exit code map (each implies the marker BEFORE it printed and the
 * one AFTER it did NOT):
 *    1  socketpair() failed
 *    2  F_DUPFD_CLOEXEC failed         (hypothesis a)
 *    3  saved fd below floor 3
 *    4  initial F_GETFL failed
 *    5  F_SETFL with O_NONBLOCK failed (hypothesis b)
 *    6  F_GETFL re-read failed
 *    7  O_NONBLOCK not visible after re-read (hypothesis c)
 *    8  getsockopt(SO_TYPE) failed
 *    9  SO_TYPE wrong value            (hypothesis d)
 *   10  isatty true on socket          (hypothesis e)
 *   11  kqueue() failed
 *   12  kevent EV_ADD failed
 *   13  write to peer failed
 *   14  kevent poll errored
 *   15  kevent poll timed out          (hypothesis f)
 *   16  fired ident or filter wrong
 *   17  read returned 0 (eof; peer end gone — hypothesis g)
 *   18  read returned <0 (errno path)
 *   19  read payload mismatch
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;
typedef int            int32_t;
typedef short          int16_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned int   socklen_t;

/* FreeBSD-flavored constants. */
#define AF_UNIX            1
#define SOCK_STREAM        1
#define SOL_SOCKET         0xffff
#define SO_TYPE            0x1008

#define FB_F_GETFL         3
#define FB_F_SETFL         4
#define FB_F_DUPFD_CLOEXEC 17
#define FB_O_NONBLOCK      0x00000004

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

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int, int, int, int *);
__attribute__((import_module("env"), import_name("fcntl")))
int fcntl(int, int, ...);
__attribute__((import_module("env"), import_name("getsockopt")))
int getsockopt(int, int, int, void *, socklen_t *);
__attribute__((import_module("env"), import_name("isatty")))
int isatty(int);
__attribute__((import_module("env"), import_name("kqueue")))
int kqueue(void);
__attribute__((import_module("env"), import_name("kevent")))
int kevent(int kq, const struct fb_kevent *changelist, int nchanges,
           struct fb_kevent *eventlist, int nevents,
           const struct fb_timespec *timeout);
__attribute__((import_module("env"), import_name("read")))
ssize_t read(int, void *, size_t);
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);
__attribute__((import_module("env"), import_name("close")))
int close(int);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    /* ── Setup: socketpair stands in for the IPC channel. sv[0] is
     *    the "parent end"; sv[1] is the "child end" that nvim's
     *    pre-exec dup2'd onto stdin. The bridge knows nothing about
     *    fd numbers 0/1/2 vs other slots — using sv[1] directly is
     *    representative. */
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0 || sv[0] < 0 || sv[1] < 0) {
        say("socketpair failed\n");
        _exit(1);
    }
    say("socketpair\n");

    /* ── Step 1: F_DUPFD_CLOEXEC, mirroring channel_from_stdio's
     *    `stdin_dup_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3)`. */
    int saved_fd = fcntl(sv[1], FB_F_DUPFD_CLOEXEC, 3);
    if (saved_fd < 0) {
        say("F_DUPFD_CLOEXEC failed\n");
        _exit(2);
    }
    if (saved_fd < 3) {
        say("dup_fd below floor\n");
        _exit(3);
    }
    say("dup_cloexec\n");

    /* ── Step 2: uv_pipe_open's read-modify-write to set NONBLOCK.
     *    Crucial that F_GETFL→F_SETFL→F_GETFL preserves the bit so
     *    libuv's later F_GETFL probes see a non-blocking fd. */
    int fl = fcntl(saved_fd, FB_F_GETFL, 0);
    if (fl < 0) {
        say("F_GETFL #1 failed\n");
        _exit(4);
    }
    if (fcntl(saved_fd, FB_F_SETFL, fl | FB_O_NONBLOCK) < 0) {
        say("F_SETFL O_NONBLOCK failed\n");
        _exit(5);
    }
    int fl2 = fcntl(saved_fd, FB_F_GETFL, 0);
    if (fl2 < 0) {
        say("F_GETFL #2 failed\n");
        _exit(6);
    }
    if ((fl2 & FB_O_NONBLOCK) == 0) {
        say("O_NONBLOCK lost on re-read\n");
        _exit(7);
    }
    say("nonblock_set\n");

    /* ── Step 3: uv_guess_handle probes — libuv decides UV_NAMED_PIPE
     *    vs UV_FILE vs UV_TTY based on these. */
    int t = -1;
    socklen_t len = sizeof(t);
    if (getsockopt(saved_fd, SOL_SOCKET, SO_TYPE, &t, &len) < 0) {
        say("getsockopt SO_TYPE failed\n");
        _exit(8);
    }
    if (t != SOCK_STREAM) {
        say("SO_TYPE wrong\n");
        _exit(9);
    }
    if (isatty(saved_fd) != 0) {
        say("isatty true on socket\n");
        _exit(10);
    }
    say("guess_handle\n");

    /* ── Step 4: uv_read_start — register on kqueue. */
    int kq = kqueue();
    if (kq < 0) {
        say("kqueue failed\n");
        _exit(11);
    }
    struct fb_kevent change = { 0 };
    change.ident  = (uint32_t)saved_fd;
    change.filter = EVFILT_READ;
    change.flags  = EV_ADD;
    change.udata  = 0xb0bacafe;
    if (kevent(kq, &change, 1, (struct fb_kevent *)0, 0,
               (struct fb_timespec *)0) < 0) {
        say("kevent EV_ADD failed\n");
        _exit(12);
    }
    say("kevent_register\n");

    /* ── Step 5: peer writes one byte; kevent poll must fire. */
    if (write(sv[0], "y", 1) != 1) {
        say("peer write failed\n");
        _exit(13);
    }
    struct fb_kevent fired = { 0 };
    struct fb_timespec ts = { .tv_sec = 0, .tv_nsec = 200000000 };  /* 200 ms */
    int n = kevent(kq, (const struct fb_kevent *)0, 0, &fired, 1, &ts);
    if (n < 0) {
        say("kevent poll errored\n");
        _exit(14);
    }
    if (n == 0) {
        say("kevent poll timed out\n");
        _exit(15);
    }
    if (fired.ident != (uint32_t)saved_fd || fired.filter != EVFILT_READ) {
        say("fired ident/filter wrong\n");
        _exit(16);
    }
    say("kevent_fired\n");

    /* ── Step 6: read on the saved fd — must return the byte, NOT 0
     *    (which is the "ch 1 was closed by the client" path that kills
     *    nvim's embedded server). */
    char buf[4] = { 0, 0, 0, 0 };
    ssize_t r = read(saved_fd, buf, sizeof(buf));
    if (r == 0) {
        say("read returned 0 (eof; peer end gone)\n");
        _exit(17);
    }
    if (r < 0) {
        say("read errored\n");
        _exit(18);
    }
    if (r != 1 || buf[0] != 'y') {
        say("read payload mismatch\n");
        _exit(19);
    }
    say("channel_from_stdio ok\n");

    close(saved_fd);
    close(sv[0]);
    close(sv[1]);
    close(kq);
    _exit(0);
}
