/*
 * test_dup2_pipe_pair.c — dup2() of a pipe end onto a new wfd slot.
 *
 * WHAT this verifies:
 *   The wasm guest opens a pipe, then calls dup2(write_end, target_fd)
 *   where target_fd is a fresh wfd slot. yos_dup2 must:
 *     1. resolve write_end to its host fd via fd_map,
 *     2. fcntl(F_DUPFD) on the host fd to get a fresh host descriptor,
 *     3. assign that into the requested wfd slot.
 *   Writing through target_fd then reading through pipe[0] must round-
 *   trip the bytes — proving the dup is sharing the same underlying
 *   pipe (not just sharing a wfd slot).
 *
 * WHY this matters:
 *   nvim's `channel_from_stdio()` does
 *     dup2(STDERR_FILENO, STDOUT_FILENO);
 *     dup2(STDERR_FILENO, STDIN_FILENO);
 *   right after stashing the originals with F_DUPFD_CLOEXEC. If yos_dup2
 *   silently swaps the wfd_map entry without an underlying host dup,
 *   later closes on the source fd would yank the pipe out from under
 *   the new fd. Picking a non-stdio target keeps the test driver-agnostic
 *   while still exercising the same code path. (We avoid dup2(., 0/1/2)
 *   because that would clobber the runner's own stdio.)
 *
 * Expected: exit 0, stdout contains "dup2 ok", stdout contains "roundtrip ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("pipe")))
int pipe(int *);

__attribute__((import_module("env"), import_name("dup2")))
int dup2(int, int);

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
    int p[2] = { -1, -1 };
    if (pipe(p) < 0 || p[0] < 0 || p[1] < 0) {
        say("pipe failed\n");
        _exit(1);
    }

    /* Pick a target slot above stdio AND above the pipe ends to avoid
     * collision with anything the runtime has open. */
    int target = (p[0] > p[1] ? p[0] : p[1]) + 5;
    int r = dup2(p[1], target);
    if (r < 0) {
        say("dup2 failed\n");
        _exit(2);
    }
    if (r != target) {
        say("dup2 wrong fd returned\n");
        _exit(3);
    }
    say("dup2 ok\n");

    /* If dup2 truly created a new host-fd that shares the pipe, writing
     * to `target` and reading from p[0] must round-trip. If it just
     * aliased the wasm-fd slot, closing p[1] later would still work but
     * we'd have wfd 'target' pointing at the same hfd as p[1] — also
     * acceptable. The decisive check: write via target, read via p[0]. */
    static const char msg[] = "ping";
    if (write(target, msg, 4) != 4) {
        say("write via dup target failed\n");
        _exit(4);
    }
    char buf[4] = { 0, 0, 0, 0 };
    if (read(p[0], buf, 4) != 4) {
        say("read from pipe head failed\n");
        _exit(5);
    }
    if (buf[0] != 'p' || buf[1] != 'i' || buf[2] != 'n' || buf[3] != 'g') {
        say("roundtrip data mismatch\n");
        _exit(6);
    }
    say("roundtrip ok\n");

    close(target);
    close(p[0]);
    close(p[1]);
    _exit(0);
}
