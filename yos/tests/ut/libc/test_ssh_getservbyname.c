/*
 * test_ssh_getservbyname.c — services-DB lookup.
 *
 * WHAT this verifies:
 *   getservbyname("ssh", "tcp") returns a non-NULL struct servent
 *   pointer whose s_port field encodes 22 (network byte order). The
 *   bridge is a host-libc passthrough; the test confirms it isn't a
 *   silent ENOSYS stub.
 *
 * WHY this matters:
 *   ssh resolves a symbolic service name to a port via
 *   getservbyname when the user wrote `Port ssh` or `-p ssh`. A
 *   stubbed bridge makes ssh fall through to its hard-coded 22, which
 *   masks other broken-services-file bugs. Pinning the bridge keeps
 *   the contract explicit.
 *
 * Expected: exit 0, stdout contains "getservbyname ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

/* FreeBSD-i386 servent layout. We only touch s_port. */
struct servent {
    char  *s_name;
    char **s_aliases;
    int    s_port;        /* network byte order */
    char  *s_proto;
};

__attribute__((import_module("env"), import_name("getservbyname")))
struct servent *getservbyname(const char *name, const char *proto);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    struct servent *se = getservbyname("ssh", "tcp");
    if (!se) { say("getservbyname FAIL: NULL\n"); _exit(1); }

    /* s_port is network byte order — htons(22) = 0x1600. Both byte
     * orders end up resolving to 22 if we swap. */
    unsigned p_net = (unsigned)se->s_port & 0xFFFF;
    unsigned p_host = ((p_net >> 8) & 0xFF) | ((p_net & 0xFF) << 8);
    if (p_host != 22) {
        say("getservbyname FAIL: port != 22\n");
        _exit(2);
    }
    say("getservbyname ok\n");
    _exit(0);
}
