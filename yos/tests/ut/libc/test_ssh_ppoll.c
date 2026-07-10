/*
 * test_ssh_ppoll.c — ppoll(2) timeout + readability detection.
 *
 * WHAT this verifies:
 *   ppoll on a fresh socketpair fd reports POLLIN once the peer
 *   writes a byte, and reports a clean timeout when neither end
 *   moves. Bridge lives in impl/io/dir.c; converts the FreeBSD-shape
 *   timespec to a host poll() timeout in ms and forwards through
 *   yos_poll's fd-map translation.
 *
 * WHY this matters:
 *   ssh's dispatch loop calls ppoll(pfds, 1, NULL, NULL) for every
 *   server read. Pre-fix, codegen left ppoll as an ENOSYS stub on
 *   darwin / BSD hosts (Linux-only syscall classification), so the
 *   first server message after the handshake produced
 *   "ssh_dispatch_run_fatal: Connection lost" with no further
 *   diagnostic. Pinning ppoll here keeps that bridge alive across
 *   future regen passes.
 *
 * Expected: exit 0, stdout contains "ppoll ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef int            pid_t;

#define AF_UNIX     1
#define SOCK_STREAM 1

#define POLLIN      0x0001

struct timespec { int tv_sec; int tv_nsec; };  /* FreeBSD-i386 layout */

struct pollfd { int fd; short events; short revents; };

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int domain, int type, int proto, int sv[2]);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("close")))
int close(int fd);

__attribute__((import_module("env"), import_name("ppoll")))
int ppoll(struct pollfd *fds, unsigned nfds,
          const struct timespec *timeout,
          const void *sigmask);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        say("ppoll FAIL: socketpair\n");
        _exit(1);
    }

    /* Zero-timeout poll on a quiet fd: must return 0 (no events), no
     * spurious POLLIN. */
    struct pollfd pfd = { sv[0], POLLIN, 0 };
    struct timespec zero = { 0, 0 };
    int r = ppoll(&pfd, 1, &zero, 0);
    if (r != 0 || pfd.revents != 0) {
        say("ppoll FAIL: quiet fd should report 0\n");
        _exit(2);
    }

    /* Now push a byte through the peer end; poll the read end and
     * expect POLLIN. NULL timeout = block until ready (which it is). */
    if (write(sv[1], "x", 1) != 1) {
        say("ppoll FAIL: write to peer\n");
        _exit(3);
    }
    pfd.revents = 0;
    r = ppoll(&pfd, 1, 0, 0);
    if (r != 1 || !(pfd.revents & POLLIN)) {
        say("ppoll FAIL: no POLLIN after write\n");
        _exit(4);
    }

    close(sv[0]); close(sv[1]);
    say("ppoll ok\n");
    _exit(0);
}
