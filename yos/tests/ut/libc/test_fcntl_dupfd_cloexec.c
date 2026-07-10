/*
 * test_fcntl_dupfd_cloexec.c — fcntl(F_DUPFD_CLOEXEC) on a pipe end.
 *
 * WHAT this verifies:
 *   The wasm guest opens a pipe, then calls
 *     dup_fd = fcntl(pipe_fd, F_DUPFD_CLOEXEC, 3)
 *   using FreeBSD's value (17) for F_DUPFD_CLOEXEC. yos_fcntl must
 *   translate the cmd to the host's value (1030 on Linux, 67 on darwin),
 *   call host fcntl, allocate a fresh wasm-fd slot for the new host fd,
 *   and the returned fd must (a) be >= 3 (the floor we asked for) and
 *   (b) have FD_CLOEXEC set.
 *
 * WHY this matters:
 *   This is the exact pattern nvim's `channel_from_stdio()` uses to
 *   stash IPC stdin/stdout before redirecting fd 0/1 to stderr. If the
 *   F_DUPFD_CLOEXEC translation breaks, the saved fd is -1, libuv's
 *   stream_init asserts on UV_NAMED_PIPE/UV_TTY, and the embedded
 *   server crashes in <200 ms — the whole class of failures described
 *   in tmp/nvim-runtime-issues.md. This test pins the
 *   FB_F_DUPFD_CLOEXEC=17 mapping in src/yos/impl/io/io.c:fcntl_cmd_fb_to_lx.
 *
 * Expected: exit 0, stdout contains "dup ok", stdout contains "cloexec ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

#define FB_F_GETFD            1
#define FB_F_DUPFD_CLOEXEC    17
#define FB_FD_CLOEXEC         1

__attribute__((import_module("env"), import_name("pipe")))
int pipe(int *);

__attribute__((import_module("env"), import_name("fcntl")))
int fcntl(int, int, ...);

__attribute__((import_module("env"), import_name("close")))
int close(int);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

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

    /* Mirror nvim's channel_from_stdio: dup with FD_CLOEXEC, floor 3. */
    int dup_fd = fcntl(p[0], FB_F_DUPFD_CLOEXEC, 3);
    if (dup_fd < 0) {
        say("F_DUPFD_CLOEXEC failed\n");
        _exit(2);
    }
    if (dup_fd < 3) {
        say("dup_fd below floor\n");
        _exit(3);
    }
    say("dup ok\n");

    int fd_flags = fcntl(dup_fd, FB_F_GETFD, 0);
    if (fd_flags < 0 || (fd_flags & FB_FD_CLOEXEC) == 0) {
        say("FD_CLOEXEC missing on dup_fd\n");
        _exit(4);
    }
    say("cloexec ok\n");

    close(dup_fd);
    close(p[0]);
    close(p[1]);
    _exit(0);
}
