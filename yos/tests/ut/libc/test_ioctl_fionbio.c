/*
 * test_ioctl_fionbio.c — ioctl(FIONBIO) on a pipe end.
 *
 * WHAT this verifies:
 *   The wasm guest opens a pipe and calls
 *     ioctl(fd, FIONBIO, &one)
 *   using FreeBSD's encoding (FB_FIONBIO = 0x8004667e). yos_ioctl must
 *   translate the cmd appropriately for the host:
 *     - On Linux,  to LX_FIONBIO (0x5421).
 *     - On darwin/FreeBSD, pass through unchanged (the BSD encoding is
 *       what those kernels expect).
 *   The call must return 0, and a follow-up fcntl(F_GETFL) must show
 *   O_NONBLOCK set.
 *
 * WHY this matters:
 *   Before the darwin port fix (tmp/nvim-runtime-issues.md), the bridge
 *   blindly returned LX_FIONBIO on every host. On darwin that became an
 *   unrecognised request number → ENOTTY, which broke libuv's
 *   uv_pipe_open(stdin) and prevented the TUI from ever registering its
 *   read watcher. This test pins the platform-aware translation in
 *   src/yos/impl/io/io.c:ioctl_cmd_fb_to_lx.
 *
 * Expected: exit 0, stdout contains "ioctl ok", stdout contains "nonblock ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef unsigned long uint_long;

#define FB_FIONBIO        0x8004667eu
#define FB_F_GETFL        3
#define FB_O_NONBLOCK     0x00000004

__attribute__((import_module("env"), import_name("pipe")))
int pipe(int *);

/* ioctl is variadic in practice; declare so the third arg goes through
 * the shadow-stack pack the bridge unpacks. */
__attribute__((import_module("env"), import_name("ioctl")))
int ioctl(int, unsigned long, ...);

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

    int one = 1;
    int r = ioctl(p[0], FB_FIONBIO, &one);
    if (r < 0) {
        say("ioctl FIONBIO failed\n");
        _exit(2);
    }
    say("ioctl ok\n");

    int fl = fcntl(p[0], FB_F_GETFL, 0);
    if (fl < 0 || (fl & FB_O_NONBLOCK) == 0) {
        say("O_NONBLOCK not set after FIONBIO\n");
        _exit(3);
    }
    say("nonblock ok\n");

    close(p[0]);
    close(p[1]);
    _exit(0);
}
