/*
 * test_write_passthrough.c — write() is a pure-libc passthrough.
 *
 * WHAT this verifies:
 *   `ssize_t write(int fd, const void *buf, size_t n)` declared with
 *   import attributes routes to yos's bridge, which forwards to the
 *   host libc `write()` unchanged. No struct conversion, no scalar
 *   widening, no errno remap is needed because the FreeBSD wasm32
 *   shape and the host libc shape are layout-identical for this
 *   signature.
 *
 * WHY this matters:
 *   This is the "compatible" category of bridge.py's analyser. Most
 *   stateless libc functions land here (read, lseek, close, fchmod,
 *   getpid, …). If passthrough breaks for write, the whole
 *   "compatible" surface is broken. This test is the smallest such
 *   passthrough we can probe end-to-end (uses stdout fd 1, writes a
 *   known string, verifies output).
 *
 * Expected: exit 0, stdout contains "write-passthrough ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn))
void _exit(int);

void _start(void) {
    static const char m[] = "write-passthrough ok\n";
    write(1, m, sizeof(m) - 1);
    _exit(0);
}
