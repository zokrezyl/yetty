/*
 * test_socketpair_flags.c — socketpair() with SOCK_NONBLOCK | SOCK_CLOEXEC.
 *
 * WHAT this verifies:
 *   The wasm guest calls
 *     socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv)
 *   using FreeBSD's high-bit encoding for SOCK_NONBLOCK (0x20000000) and
 *   SOCK_CLOEXEC (0x10000000). yos_vfs_socketpair must (a) strip those
 *   bits before the host call (darwin's kernel rejects them as EINVAL),
 *   (b) succeed, and (c) re-apply O_NONBLOCK and FD_CLOEXEC on each
 *   returned fd via fcntl. This test reads them back with
 *   fcntl(F_GETFL) / fcntl(F_GETFD) to verify both flags landed.
 *
 * WHY this matters:
 *   nvim's embedded server uses libuv pipes built on socketpair() for the
 *   IPC channel. The original bug (tmp/nvim-runtime-issues.md) was that
 *   socketpair on darwin returned EINVAL for the FreeBSD-flavored type
 *   bits, which silently broke the IPC pipe and made the server exit
 *   with `eof=true` shortly after startup. This test pins the workaround
 *   in src/yos/impl/io/io.c:yos_vfs_socketpair.
 *
 * Expected: exit 0, stdout contains "socketpair ok", stdout contains "flags ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

/* FreeBSD-flavored constants — these are what the wasm guest sees,
 * the bridge translates to host values. */
#define AF_UNIX           1
#define SOCK_STREAM       1
#define SOCK_CLOEXEC      0x10000000
#define SOCK_NONBLOCK     0x20000000

#define FB_F_GETFD        1
#define FB_F_GETFL        3
#define FB_FD_CLOEXEC     1
#define FB_O_NONBLOCK     0x00000004

__attribute__((import_module("env"), import_name("socketpair")))
int socketpair(int, int, int, int *);

/* fcntl is variadic on the C side; clang's wasm32 ABI passes the third
 * arg via a shadow-stack pack, which is what yos_fcntl decodes. Declare
 * variadic so call sites pass `0` as the third arg slot. */
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
    int sv[2] = { -1, -1 };
    int r = socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                       0, sv);
    if (r < 0 || sv[0] < 0 || sv[1] < 0) {
        say("socketpair failed\n");
        _exit(1);
    }
    say("socketpair ok\n");

    for (int i = 0; i < 2; i++) {
        int fl = fcntl(sv[i], FB_F_GETFL, 0);
        if (fl < 0 || (fl & FB_O_NONBLOCK) == 0) {
            say("O_NONBLOCK missing on socketpair end\n");
            _exit(2);
        }
        int fd_flags = fcntl(sv[i], FB_F_GETFD, 0);
        if (fd_flags < 0 || (fd_flags & FB_FD_CLOEXEC) == 0) {
            say("FD_CLOEXEC missing on socketpair end\n");
            _exit(3);
        }
    }
    say("flags ok\n");

    close(sv[0]);
    close(sv[1]);
    _exit(0);
}
