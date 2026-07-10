/*
 * test_closefrom_preserves_stdio.c — closefrom(3) must leave stdio alone.
 *
 * WHAT this verifies:
 *   The guest opens an extra wasm fd, calls closefrom(3), then writes to
 *   stderr (wfd 2). The write must succeed: closefrom is bounded by the
 *   wasm fd table, not by host fd numbers.
 *
 * WHY this matters:
 *   yos_fd_table_init parks the host's stdin/stdout/stderr dups at host
 *   fds 3/4/5 (fd_map[0..2]). glibc 2.34+ exposes closefrom as a host
 *   libc symbol. If the bridge passes the wasm lowfd straight to host
 *   closefrom(3), the host closes host fds 3-UINT_MAX — including the
 *   stdio dups — and the next guest write to stderr returns EBADF. ssh
 *   -v hit exactly this on Linux: silent exit after the wasm guest
 *   called closefrom(3) on startup.
 *
 * Expected: exit 0, stderr contains "stderr after closefrom".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("open")))
int open(const char *, int);

__attribute__((import_module("env"), import_name("closefrom")))
void closefrom(int);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }

void _start(void) {
    /* Open something at wfd >= 3 so closefrom(3) has work to do. */
    int extra = open("/dev/null", 0);
    if (extra < 3) _exit(10);

    closefrom(3);

    const char m[] = "stderr after closefrom\n";
    if (write(2, m, slen(m)) != slen(m)) _exit(11);
    _exit(0);
}
