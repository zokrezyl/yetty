/*
 * test_fork_channel_from_stdio.c — fork-aware reproducer for the nvim
 *                                  embedded-server failure on darwin.
 *
 * WHAT this verifies:
 *
 *   The parent creates a socketpair (the IPC channel), forks, and the
 *   CHILD walks the same channel_from_stdio + uv_pipe_open + uv_read_start
 *   sequence as test_nvim_channel_from_stdio.c — but in a forked
 *   context, with its own fd_map (built by yos_fd_fork_dup) and the
 *   IPC fd inherited via a pre-fork dup2 onto a stdin-like slot.
 *   Parent writes one byte on its end; child must see EVFILT_READ
 *   fire and read the byte. Child's exit code propagates.
 *
 * WHY this matters:
 *
 *   The unforked end-to-end test (test_nvim_channel_from_stdio.c)
 *   passes on darwin — proving the bridge primitives composed in
 *   nvim's order on a fresh socketpair work in a single process. The
 *   actual nvim failure (tmp/nvim-runtime-issues.md) lives across the
 *   fork+exec boundary: the embedded server is a forked child whose
 *   fd_map was built by yos_fd_fork_dup (dup'ing each parent fd via
 *   F_DUPFD/F_DUPFD_CLOEXEC). The decisive trace observation —
 *   `wfd=17 hfd=35 never registered on any kqueue` — happened in
 *   that child context.
 *
 *   This test exercises the same cross-context state machine without
 *   needing an execve target: the `dup2(IPC_end, slot)` pre-fork
 *   stands in for "parent dup2'd IPC end onto stdin before execve",
 *   and yos_fd_fork_dup is the same code path the real nvim child
 *   goes through. If this test fails where test_nvim_channel_from_stdio
 *   passes, the broken layer is in the fork-time fd_map handoff.
 *
 *   Exit codes match test_nvim_channel_from_stdio.c (1..19); see
 *   that file for the per-step hypothesis mapping. Codes 30+ are
 *   parent-side failures.
 *
 * Expected: exit 0, stdout contains "fork channel ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef long long      int64_t;
typedef int            int32_t;
typedef short          int16_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned int   socklen_t;

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
    int64_t tv_sec;
    int32_t tv_nsec;
};

/* The slot we dup the IPC end onto pre-fork. Standing in for fd 0
 * (stdin) without clobbering the test runner's actual stdin. The
 * bridge does not special-case fd numbers, so any free slot works.
 * Picked high enough to be clear of pipe()/socketpair() allocations. */
#define INHERITED_SLOT     20

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int, int, int, int *);
__attribute__((import_module("env"), import_name("fcntl")))
int fcntl(int, int, ...);
__attribute__((import_module("env"), import_name("dup2")))
int dup2(int, int);
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
__attribute__((import_module("env"), import_name("fork")))
int fork(void);
__attribute__((import_module("env"), import_name("waitpid")))
int waitpid(int, int *, int);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(2, s, slen(s)); }

/* Run the channel_from_stdio dance as the embedded child would:
 * the IPC end is at INHERITED_SLOT after the pre-fork dup2. Returns
 * 0 on success, the matching test_nvim_channel_from_stdio exit code
 * on failure. */
static int child_dance(void) {
    /* 1. F_DUPFD_CLOEXEC stash. */
    int saved = fcntl(INHERITED_SLOT, FB_F_DUPFD_CLOEXEC, 3);
    if (saved < 0) { say("child: F_DUPFD_CLOEXEC failed\n"); return 2; }
    if (saved < 3) { say("child: dup_fd below floor\n"); return 3; }

    /* 2. dup2(stderr, INHERITED_SLOT) — same redirect nvim does
     *    on fd 0/1. The saved fd must remain functional after this. */
    if (dup2(2, INHERITED_SLOT) < 0) {
        say("child: dup2 stderr->slot failed\n"); return 30;
    }

    /* 3. uv_pipe_open's NONBLOCK dance. */
    int fl = fcntl(saved, FB_F_GETFL, 0);
    if (fl < 0) { say("child: F_GETFL #1 failed\n"); return 4; }
    if (fcntl(saved, FB_F_SETFL, fl | FB_O_NONBLOCK) < 0) {
        say("child: F_SETFL O_NONBLOCK failed\n"); return 5;
    }
    int fl2 = fcntl(saved, FB_F_GETFL, 0);
    if (fl2 < 0) { say("child: F_GETFL #2 failed\n"); return 6; }
    if ((fl2 & FB_O_NONBLOCK) == 0) {
        say("child: O_NONBLOCK lost on re-read\n"); return 7;
    }

    /* 4. uv_guess_handle probes. */
    int t = -1;
    socklen_t len = sizeof(t);
    if (getsockopt(saved, SOL_SOCKET, SO_TYPE, &t, &len) < 0) {
        say("child: getsockopt failed\n"); return 8;
    }
    if (t != SOCK_STREAM) { say("child: SO_TYPE wrong\n"); return 9; }
    if (isatty(saved) != 0) { say("child: isatty true on socket\n"); return 10; }

    /* 5. uv_read_start. */
    int kq = kqueue();
    if (kq < 0) { say("child: kqueue failed\n"); return 11; }
    struct fb_kevent change = { 0 };
    change.ident  = (uint32_t)saved;
    change.filter = EVFILT_READ;
    change.flags  = EV_ADD;
    change.udata  = 0xfeedface;
    if (kevent(kq, &change, 1, (struct fb_kevent *)0, 0,
               (struct fb_timespec *)0) < 0) {
        say("child: kevent EV_ADD failed\n"); return 12;
    }

    /* 6. Poll for readiness. Parent already wrote (or will write
     *    promptly); kqueue is level-triggered, so a 1 s timeout is
     *    plenty. */
    struct fb_kevent fired = { 0 };
    struct fb_timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    int n = kevent(kq, (const struct fb_kevent *)0, 0, &fired, 1, &ts);
    if (n < 0) { say("child: kevent poll errored\n"); return 14; }
    if (n == 0) { say("child: kevent poll timed out\n"); return 15; }
    if (fired.ident != (uint32_t)saved || fired.filter != EVFILT_READ) {
        say("child: fired ident/filter wrong\n"); return 16;
    }

    /* 7. Read the byte. */
    char buf[4] = { 0, 0, 0, 0 };
    ssize_t r = read(saved, buf, sizeof(buf));
    if (r == 0) { say("child: read returned 0 (eof)\n"); return 17; }
    if (r < 0)  { say("child: read errored\n"); return 18; }
    if (r != 1 || buf[0] != 'z') {
        say("child: read payload mismatch\n"); return 19;
    }
    return 0;
}

void _start(void) {
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        say("socketpair failed\n");
        _exit(31);
    }

    /* Pre-fork dup2: place the child-side IPC end on a fixed slot.
     * Mirrors what nvim's parent does when it sets up the embedded
     * server: dup2(child_ipc_end, STDIN_FILENO) before execve so the
     * inherited stdin is the IPC pipe. We use INHERITED_SLOT instead
     * of 0 to avoid clobbering the host runner's stdin. */
    if (dup2(sv[1], INHERITED_SLOT) < 0) {
        say("dup2 sv[1] -> slot failed\n");
        _exit(32);
    }

    int pid = fork();
    if (pid < 0) { say("fork failed\n"); _exit(33); }

    if (pid == 0) {
        /* Child: close the parent end of the socketpair. The
         * INHERITED_SLOT entry still references the same open file
         * description (dup2 created an independent fd). */
        close(sv[0]);
        close(sv[1]);
        int rc = child_dance();
        _exit(rc);
    }

    /* Parent: write one byte on its end. The kernel buffers it in
     * the socketpair regardless of whether the child has registered
     * on its kqueue yet (kqueue is level-triggered for sockets). */
    close(sv[1]);            /* parent doesn't need the child end */
    if (write(sv[0], "z", 1) != 1) {
        say("parent write failed\n");
        _exit(34);
    }

    int status = 0;
    int rpid = waitpid(pid, &status, 0);
    if (rpid != pid) {
        say("parent: waitpid wrong pid\n");
        _exit(35);
    }

    int child_exit = (status >> 8) & 0xff;
    if (child_exit == 0) {
        write(1, "fork channel ok\n", 16);
    }
    _exit(child_exit);
}
