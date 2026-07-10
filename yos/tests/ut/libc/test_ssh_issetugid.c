/*
 * test_ssh_issetugid.c — set-uid / set-gid detection.
 *
 * WHAT this verifies:
 *   issetugid() returns 0 when the guest is not running with elevated
 *   privileges (real == effective uid AND real == effective gid). The
 *   bridge is in impl/posix.c routed via custom_proc. The yos
 *   binary itself isn't installed setuid, so the answer must be 0.
 *
 * WHY this matters:
 *   ssh calls issetugid() before reading some env vars (DISPLAY,
 *   SSH_AUTH_SOCK, …). A bridge that returns -1/ENOSYS or 1 makes
 *   ssh think it's running set-uid and silently drops every one of
 *   those vars — agent forwarding stops working, X forwarding stops
 *   working, with no error message. issetugid was previously
 *   stubbed; this test pins the route.
 *
 * Expected: exit 0, stdout contains "issetugid ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("issetugid")))
int issetugid(void);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    int r = issetugid();
    if (r != 0) {
        say("issetugid FAIL: expected 0\n");
        _exit(1);
    }
    say("issetugid ok\n");
    _exit(0);
}
