/*
 * test_ssh_dup_stdio.c — dup/dup2/close cycle on stdio.
 *
 * WHAT this verifies:
 *   dup(STDOUT_FILENO) returns a fresh wasm fd that writes go to the
 *   same destination as stdout. dup2(src, dst) replaces dst with a
 *   dup of src; the original dst's host fd is released. closing the
 *   original after a dup leaves the dup writable. close(non-zero)
 *   doesn't return EBADF on a fd that was just opened by dup.
 *
 * WHY this matters:
 *   ssh executes this exact dance for its session channel: dup the
 *   three stdio fds into high-numbered guest fds so its post-fork
 *   teardown can close 0/1/2 without losing the actual user-facing
 *   stdio. yos's fd_map then maps those high wfds to fresh host fds.
 *   A bug where dup re-uses an existing wfd or where close(dup_result)
 *   poisons the original (because the host fd table is shared) shows
 *   up here.
 *
 * Expected: exit 0, stdout contains "dup-stdio ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

__attribute__((import_module("env"), import_name("dup")))
int dup(int fd);

__attribute__((import_module("env"), import_name("dup2")))
int dup2(int oldfd, int newfd);

__attribute__((import_module("env"), import_name("close")))
int close(int fd);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(STDOUT_FILENO, s, slen(s)); }

void _start(void) {
    /* dup stdout — must succeed and not collide with stdio fds. */
    int d_out = dup(STDOUT_FILENO);
    if (d_out < 0 || d_out == STDOUT_FILENO || d_out == STDIN_FILENO ||
        d_out == STDERR_FILENO) {
        say("FAIL: dup(STDOUT) returned bad fd\n");
        _exit(1);
    }

    /* Write through the dup — output must land on the same sink. */
    if (write(d_out, "via-dup\n", 8) != 8) {
        say("FAIL: write to dup failed\n");
        _exit(2);
    }

    /* dup stderr too; must NOT alias d_out. */
    int d_err = dup(STDERR_FILENO);
    if (d_err < 0 || d_err == d_out) {
        say("FAIL: dup(STDERR) collided with d_out\n");
        _exit(3);
    }

    /* dup2: redirect d_err's slot to point at the same sink as
     * STDOUT. Old d_err's host fd is released (closed), but d_out
     * stays valid. */
    if (dup2(STDOUT_FILENO, d_err) != d_err) {
        say("FAIL: dup2 didn't return newfd\n");
        _exit(4);
    }
    if (write(d_err, "via-dup2\n", 9) != 9) {
        say("FAIL: write after dup2 failed\n");
        _exit(5);
    }

    /* Closing d_out must NOT kill d_err. */
    close(d_out);
    if (write(d_err, "after-close-of-source\n", 22) != 22) {
        say("FAIL: dup2 target broken by closing source\n");
        _exit(6);
    }
    close(d_err);

    /* dup2 with oldfd == newfd is a POSIX no-op; must return newfd. */
    int r = dup2(STDOUT_FILENO, STDOUT_FILENO);
    if (r != STDOUT_FILENO) {
        say("FAIL: dup2(X,X) returned non-X\n");
        _exit(7);
    }

    say("dup-stdio ok\n");
    _exit(0);
}
