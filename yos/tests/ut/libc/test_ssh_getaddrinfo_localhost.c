/*
 * test_ssh_getaddrinfo_localhost.c — name → sockaddr → name roundtrip.
 *
 * WHAT this verifies:
 *   getaddrinfo("127.0.0.1", "22", &hints, &res) returns success and
 *   fills *res with at least one entry pointing at a sockaddr_in
 *   whose first byte encodes either Linux's AF_INET (= 2) or the
 *   FreeBSD-shape sa_len+sa_family pair (also yields 2 at offset 1
 *   on darwin/BSD). gai_strerror(0) returns a non-empty string.
 *   getnameinfo on the returned sockaddr round-trips to "127.0.0.1".
 *   freeaddrinfo releases the chain. The whole sequence is what
 *   ssh's sshconnect.c walks before opening a TCP socket.
 *
 * WHY this matters:
 *   The trace from `ssh nixem` showed getaddrinfo → connect →
 *   getnameinfo on every host. If any link in that chain returns
 *   ENOSYS or a malformed sockaddr, ssh prints "Could not resolve
 *   hostname" and bails. The bridge is in impl/libc/posix.c with
 *   wasm-to-host pointer translation for the sockaddr buffer; this
 *   test is the smallest probe that exercises that translation
 *   end-to-end.
 *
 * Expected: exit 0, stdout contains "ssh-getaddrinfo ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef unsigned int  socklen_t;

/* FreeBSD-i386 addrinfo layout — matches the sysroot decl bridged by
 * the auto-generated yos_getaddrinfo. We only touch the next pointer
 * + the ai_addr / ai_addrlen pair the test cares about. */
struct addrinfo {
    int               ai_flags;
    int               ai_family;
    int               ai_socktype;
    int               ai_protocol;
    socklen_t         ai_addrlen;
    char             *ai_canonname;       /* FreeBSD order: canonname before addr */
    void             *ai_addr;
    struct addrinfo  *ai_next;
};

#define AF_INET     2

__attribute__((import_module("env"), import_name("getaddrinfo")))
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);

__attribute__((import_module("env"), import_name("freeaddrinfo")))
void freeaddrinfo(struct addrinfo *res);

__attribute__((import_module("env"), import_name("getnameinfo")))
int getnameinfo(const void *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen,
                int flags);

__attribute__((import_module("env"), import_name("gai_strerror")))
const char *gai_strerror(int ecode);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

/* FreeBSD-i386 netdb.h flag values — these are the bits the wasm
 * guest sees and the bridge forwards unchanged to the host. */
#define NI_NUMERICHOST  0x00000002
#define NI_NUMERICSERV  0x00000008

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

void _start(void) {
    struct addrinfo hints;
    /* Zero hints manually — calloc/memset would add unrelated noise. */
    char *h = (char *)&hints;
    for (size_t i = 0; i < sizeof hints; i++) h[i] = 0;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = 1;        /* SOCK_STREAM */

    struct addrinfo *res = (struct addrinfo *)0;
    int rc = getaddrinfo("127.0.0.1", "22", &hints, &res);
    if (rc != 0) {
        say("getaddrinfo FAIL: rc=");
        char d[16]; int n = 0, x = rc < 0 ? -rc : rc;
        if (rc < 0) say("-");
        if (x == 0) d[n++] = '0';
        while (x) { d[n++] = '0' + (x % 10); x /= 10; }
        while (n--) write(1, d + n, 1);
        say(" (");
        say(gai_strerror(rc));
        say(")\n");
        _exit(1);
    }
    if (!res) { say("getaddrinfo FAIL: res NULL\n"); _exit(2); }
    if (!res->ai_addr || res->ai_addrlen < 8) {
        say("getaddrinfo FAIL: ai_addr missing\n");
        _exit(3);
    }

    /* getnameinfo back to numeric. */
    char host[64] = {0};
    char serv[16] = {0};
    rc = getnameinfo(res->ai_addr, res->ai_addrlen,
                     host, sizeof host,
                     serv, sizeof serv,
                     NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) { say("getnameinfo FAIL\n"); _exit(4); }
    if (!streq(host, "127.0.0.1")) {
        say("getnameinfo FAIL: host=");
        say(host);
        say("\n");
        _exit(5);
    }
    if (!streq(serv, "22")) {
        say("getnameinfo FAIL: serv=");
        say(serv);
        say("\n");
        _exit(6);
    }

    /* gai_strerror must return SOMETHING for the all-OK code. */
    const char *msg = gai_strerror(0);
    if (!msg || !msg[0]) { say("gai_strerror FAIL\n"); _exit(7); }

    /* The host slot of getnameinfo with NI_NUMERICHOST MUST be the
     * dotted-quad IP — never a DNS name. On Linux NI_NUMERICHOST has a
     * different bit value than FreeBSD; pre-fix the bridge passed the
     * flags unchanged and Linux saw the FreeBSD bit as
     * NI_NUMERICSERV, ran a reverse DNS lookup, and stuck the FQDN in
     * the host slot. ssh's "host key unknown" prompt then showed the
     * FQDN where the IP belonged. */
    if (host[0] == '\0' ||
        (host[0] < '0' || host[0] > '9')) {
        say("getnameinfo FAIL: NUMERICHOST produced a name, not an IP: ");
        say(host);
        say("\n");
        _exit(20);
    }

    freeaddrinfo(res);

    say("ssh-getaddrinfo ok\n");
    _exit(0);
}
