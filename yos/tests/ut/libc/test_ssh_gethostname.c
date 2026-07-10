/*
 * test_ssh_gethostname.c — host name passthrough.
 *
 * WHAT this verifies:
 *   gethostname(buf, sz) returns 0 and fills buf with a NUL-
 *   terminated host name of length >= 1 and <= sz - 1. The bridge
 *   is a direct host-libc passthrough.
 *
 * WHY this matters:
 *   ssh's known-hosts lookup, ssh's "@<host>" prompt formatting, and
 *   sshd's logging all start with gethostname. A bridge that returns
 *   -1 or leaves buf empty produces "ssh: <empty>: " in error
 *   messages and a useless known-hosts key.
 *
 * Expected: exit 0, stdout contains "gethostname ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("gethostname")))
int gethostname(char *buf, size_t sz);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    char buf[256];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = 0xCC;

    int rc = gethostname(buf, sizeof buf);
    if (rc != 0) { say("gethostname FAIL: rc != 0\n"); _exit(1); }

    /* Must be NUL-terminated somewhere within the buffer. */
    int term = -1;
    for (size_t i = 0; i < sizeof buf; i++) {
        if ((unsigned char)buf[i] == 0) { term = (int)i; break; }
    }
    if (term < 1) { say("gethostname FAIL: empty or unterminated\n"); _exit(2); }

    /* No untouched 0xCC sentinel before the NUL — proves the bridge
     * actually wrote bytes. */
    for (int i = 0; i < term; i++) {
        if ((unsigned char)buf[i] == 0xCC) {
            say("gethostname FAIL: gap in written bytes\n");
            _exit(3);
        }
    }

    say("gethostname ok: ");
    write(1, buf, term);
    say("\n");
    _exit(0);
}
