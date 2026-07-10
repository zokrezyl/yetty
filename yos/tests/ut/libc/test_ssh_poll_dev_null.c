/*
 * test_ssh_poll_dev_null.c — polling /dev/null must not return POLLNVAL.
 *
 * WHAT this verifies:
 *   On darwin (and BSD generally), host poll() returns POLLNVAL when
 *   asked about a fd that opens /dev/null — those character devices
 *   are not "pollable" the way sockets / pipes / ttys are. Linux poll
 *   instead returns POLLIN | POLLOUT immediately. yos's wasm guest is
 *   FreeBSD-shaped but the contract it expects matches Linux. yos_poll
 *   detects /dev/null (and regular files) via fstat and synthesises
 *   the Linux answer.
 *
 * WHY this matters:
 *   ssh dup(STDIN_FILENO) into its own fd table early in startup and
 *   then polls that fd on every protocol-dispatch tick. When ssh is
 *   invoked from a context where stdin is /dev/null (cron, Claude's
 *   non-interactive shell, `ssh host cmd < /dev/null`), darwin's
 *   POLLNVAL trips ssh's
 *       "channel %d: invalid rfd pollfd[%u].fd ..."
 *   fatal — `ssh nixem ls` exits 255 with no output. A regular file
 *   stdin (`ssh host cmd <file`) hits the same path.
 *
 * Expected: exit 0, stdout contains "poll-dev-null ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;

#define POLLIN       0x0001
#define POLLOUT      0x0004
#define POLLNVAL     0x0020

#define O_RDONLY     0x0000

struct pollfd { int fd; short events; short revents; };
struct timespec { int tv_sec; int tv_nsec; };

__attribute__((import_module("env"), import_name("open")))
int open(const char *path, int flags, ...);

__attribute__((import_module("env"), import_name("close")))
int close(int fd);

__attribute__((import_module("env"), import_name("dup")))
int dup(int fd);

__attribute__((import_module("env"), import_name("ppoll")))
int ppoll(struct pollfd *fds, unsigned nfds,
          const struct timespec *timeout, const void *sigmask);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) { say("FAIL: open /dev/null\n"); _exit(1); }

    /* Direct poll on /dev/null with zero timeout. Must report POLLIN
     * ready (EOF) and never POLLNVAL. */
    struct pollfd pfd = { fd, POLLIN, 0 };
    struct timespec zero = { 0, 0 };
    int r = ppoll(&pfd, 1, &zero, 0);
    if (r < 0) { say("FAIL: ppoll rc<0\n"); _exit(2); }
    if (pfd.revents & POLLNVAL) {
        say("FAIL: /dev/null reported POLLNVAL\n");
        _exit(3);
    }
    if (!(pfd.revents & POLLIN)) {
        say("FAIL: /dev/null should be POLLIN-ready\n");
        _exit(4);
    }

    /* dup it (the way ssh dups stdin) — the synthesised behaviour must
     * follow the underlying inode, not the fd number. */
    int fd2 = dup(fd);
    if (fd2 < 0) { say("FAIL: dup\n"); _exit(5); }
    struct pollfd pfd2 = { fd2, POLLIN, 0 };
    r = ppoll(&pfd2, 1, &zero, 0);
    if (r < 0 || (pfd2.revents & POLLNVAL) || !(pfd2.revents & POLLIN)) {
        say("FAIL: dup of /dev/null mishandled\n");
        _exit(6);
    }

    /* Poll mix: a real (socketpair) end with the /dev/null end. We
     * don't have socketpair imported here — just verify multi-fd poll
     * with two /dev/null fds works. */
    struct pollfd many[2] = { {fd, POLLIN, 0}, {fd2, POLLIN | POLLOUT, 0} };
    r = ppoll(many, 2, &zero, 0);
    if (r < 2) { say("FAIL: multi-fd poll didn't report both\n"); _exit(7); }
    if (many[0].revents & POLLNVAL) { say("FAIL: many[0] POLLNVAL\n"); _exit(8); }
    if (many[1].revents & POLLNVAL) { say("FAIL: many[1] POLLNVAL\n"); _exit(9); }

    close(fd); close(fd2);
    say("poll-dev-null ok\n");
    _exit(0);
}
