/*
 * test_socketpair_sotype_isatty.c — uv_guess_handle probes on a socket.
 *
 * WHAT this verifies:
 *   The wasm guest creates a socketpair(AF_UNIX, SOCK_STREAM, 0, sv),
 *   then probes one end with the same calls libuv's uv_guess_handle
 *   uses to decide which UV_HANDLE_TYPE a raw fd should map to:
 *     - getsockopt(fd, SOL_SOCKET, SO_TYPE, &t, &len)  must return 0,
 *       and t must be SOCK_STREAM (FreeBSD value 1).
 *     - isatty(fd) must return 0 (not a tty).
 *   Together these prove the host bridge surfaces enough type info for
 *   libuv to classify the fd as UV_NAMED_PIPE rather than UV_FILE.
 *
 * WHY this matters:
 *   The smoking-gun observation in tmp/nvim-runtime-issues.md is that
 *   nvim's IPC stdin (saved as stdin_dup_fd) is never registered on any
 *   kqueue. The leading hypothesis is that uv_guess_handle returns
 *   UV_FILE for the dup'd socketpair end on darwin, which sends libuv
 *   down its idle-handler fallback path that never adds a kqueue
 *   watcher. If getsockopt(SO_TYPE) returns the wrong value here, that
 *   would be the root cause; this test pins the contract.
 *
 * Expected: exit 0, stdout contains "so_type ok", stdout contains "isatty ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef unsigned int  socklen_t;

/* FreeBSD-flavored constants. */
#define AF_UNIX           1
#define SOCK_STREAM       1
#define SOL_SOCKET        0xffff
#define SO_TYPE           0x1008

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int, int, int, int *);

__attribute__((import_module("env"), import_name("getsockopt")))
int getsockopt(int, int, int, void *, socklen_t *);

__attribute__((import_module("env"), import_name("isatty")))
int isatty(int);

__attribute__((import_module("env"), import_name("close")))
int close(int);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        say("socketpair failed\n");
        _exit(1);
    }

    int t = -1;
    socklen_t len = sizeof(t);
    int r = getsockopt(sv[0], SOL_SOCKET, SO_TYPE, &t, &len);
    if (r < 0) {
        say("getsockopt SO_TYPE failed\n");
        _exit(2);
    }
    if (t != SOCK_STREAM) {
        say("SO_TYPE wrong value\n");
        _exit(3);
    }
    say("so_type ok\n");

    int tty = isatty(sv[0]);
    if (tty != 0) {
        say("isatty true on socket\n");
        _exit(4);
    }
    say("isatty ok\n");

    close(sv[0]);
    close(sv[1]);
    _exit(0);
}
