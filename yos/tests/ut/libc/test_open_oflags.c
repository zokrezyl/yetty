/*
 * test_open_oflags.c — open() with FreeBSD-encoded O_* flags.
 *
 * WHAT this verifies:
 *   The wasm guest opens a fresh path under /tmp with
 *     O_RDWR | O_CREAT | O_NONBLOCK | O_NOCTTY | O_CLOEXEC
 *   using the FreeBSD numeric values for those flags. yos_open must
 *   pass the path through, and oflags_fb_to_lx must translate the
 *   FreeBSD bit pattern to the host's bit pattern (different on Linux,
 *   mostly identical on darwin/FreeBSD-host but with a different
 *   O_CLOEXEC). The open must succeed, and follow-up fcntl probes must
 *   show O_NONBLOCK and FD_CLOEXEC both set on the returned fd.
 *
 * WHY this matters:
 *   The original darwin port had hardcoded Linux O_* values (so darwin
 *   saw uninitialised garbage for O_CLOEXEC and O_NONBLOCK), which
 *   silently broke libuv's pipe-style fds for nvim's IPC channel
 *   (tmp/nvim-runtime-issues.md). This test pins the host-O_* mapping
 *   in src/yos/impl/io/io.c:oflags_fb_to_lx and the #ifndef O_DIRECT/
 *   O_PATH guards for darwin's missing macros.
 *
 *   The mode 0600 below is a conservative perm — we don't want to leak
 *   group-writable files into /tmp.
 *
 * Expected: exit 0, stdout contains "open ok", stdout contains "flags ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

/* FreeBSD O_* numeric values — mirrors src/yos/impl/io/io.c FB_O_*. */
#define FB_O_RDWR         2
#define FB_O_NONBLOCK     0x00000004
#define FB_O_CREAT        0x00000200
#define FB_O_NOCTTY       0x00008000
#define FB_O_CLOEXEC      0x00100000

#define FB_F_GETFD        1
#define FB_F_GETFL        3
#define FB_FD_CLOEXEC     1

/* The bridge maps `open(path, flags)` — mode comes through the bridge
 * elsewhere; the codegen emits the 2-arg form. We declare it variadic
 * to keep the callsite tidy (`open(p, f, mode)`) — clang's wasm32 ABI
 * passes the trailing arg via a shadow-stack pack the bridge ignores
 * for non-O_CREAT paths and reads explicitly when it cares. */
__attribute__((import_module("env"), import_name("open")))
int open(const char *, int, ...);

__attribute__((import_module("env"), import_name("fcntl")))
int fcntl(int, int, ...);

__attribute__((import_module("env"), import_name("close")))
int close(int);

__attribute__((import_module("env"), import_name("unlink")))
int unlink(const char *);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    static const char path[] = "/tmp/yos-test-open-oflags";

    /* Best-effort cleanup from a previous failed run. */
    unlink(path);

    int flags = FB_O_RDWR | FB_O_CREAT | FB_O_NONBLOCK
              | FB_O_NOCTTY | FB_O_CLOEXEC;
    int fd = open(path, flags, 0600);
    if (fd < 0) {
        say("open failed\n");
        _exit(1);
    }
    say("open ok\n");

    int fl = fcntl(fd, FB_F_GETFL, 0);
    if (fl < 0 || (fl & FB_O_NONBLOCK) == 0) {
        say("O_NONBLOCK missing\n");
        _exit(2);
    }
    int fd_flags = fcntl(fd, FB_F_GETFD, 0);
    if (fd_flags < 0 || (fd_flags & FB_FD_CLOEXEC) == 0) {
        say("FD_CLOEXEC missing\n");
        _exit(3);
    }
    say("flags ok\n");

    close(fd);
    unlink(path);
    _exit(0);
}
