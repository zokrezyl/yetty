/*
 * test_ssh_socket_loopback.c — socket/connect/getsockname/getpeername
 * round-trip via a self-connected pair.
 *
 * WHAT this verifies:
 *   The bridges socket / bind / listen / connect / accept /
 *   getsockname / getpeername / getsockopt / setsockopt all work
 *   together end-to-end. We bind a listener to 127.0.0.1:0
 *   (ephemeral port), read the port back via getsockname, connect a
 *   client to that port, accept it, then run getpeername on the
 *   server's accepted fd and getsockname on the client to make sure
 *   the addresses are consistent. setsockopt(SO_REUSEADDR) is
 *   exercised on the listener. The trickiest piece is the
 *   FreeBSD-shape sockaddr_in: sin_len at byte 0, sin_family at
 *   byte 1, sin_port at offset 2 (network byte order), sin_addr at 4.
 *
 * WHY this matters:
 *   This is the exact dance ssh runs to set up its TCP connection.
 *   getsockname on darwin / FreeBSD reads sa_family from byte 1, but
 *   the wasm guest reads from byte 1 too (FreeBSD layout); the
 *   bridge in impl/libc/posix.c stitches them up. The pre-fix bug
 *   read sa_family as `host_buf[0] | (host_buf[1] << 8)` which on
 *   darwin gave `sa_len | (sa_family << 8)` — the wrong family.
 *
 * Expected: exit 0, stdout contains "ssh-loopback ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef unsigned int   socklen_t;
typedef unsigned short uint16_t;

#define AF_INET       2
#define SOCK_STREAM   1
#define IPPROTO_TCP   6
#define SOL_SOCKET    0xffff       /* FreeBSD */
#define SO_REUSEADDR  0x4          /* FreeBSD */
#define INADDR_LOOPBACK 0x7f000001 /* host byte order */

/* FreeBSD sockaddr_in: sin_len(1) sin_family(1) sin_port(2,nbo) sin_addr(4,nbo) sin_zero(8) */
struct sockaddr_in {
    unsigned char  sin_len;
    unsigned char  sin_family;
    uint16_t       sin_port;
    unsigned int   sin_addr;
    char           sin_zero[8];
};

__attribute__((import_module("env"), import_name("socket")))
int socket(int domain, int type, int proto);

__attribute__((import_module("env"), import_name("bind")))
int bind(int fd, const void *addr, socklen_t alen);

__attribute__((import_module("env"), import_name("listen")))
int listen(int fd, int backlog);

__attribute__((import_module("env"), import_name("connect")))
int connect(int fd, const void *addr, socklen_t alen);

__attribute__((import_module("env"), import_name("accept")))
int accept(int fd, void *addr, socklen_t *alen);

__attribute__((import_module("env"), import_name("getsockname")))
int getsockname(int fd, void *addr, socklen_t *alen);

__attribute__((import_module("env"), import_name("getpeername")))
int getpeername(int fd, void *addr, socklen_t *alen);

__attribute__((import_module("env"), import_name("setsockopt")))
int setsockopt(int fd, int level, int opt, const void *val, socklen_t vlen);

__attribute__((import_module("env"), import_name("getsockopt")))
int getsockopt(int fd, int level, int opt, void *val, socklen_t *vlen);

__attribute__((import_module("env"), import_name("close")))
int close(int fd);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

static uint16_t htons16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static unsigned htonl32(unsigned x) {
    return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) |
           ((x << 8) & 0xff0000) | ((x << 24) & 0xff000000);
}

static void zero(void *p, size_t n)
{
    unsigned char *b = p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

void _start(void) {
    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv < 0) { say("FAIL: socket srv\n"); _exit(1); }

    /* SO_REUSEADDR — make sure setsockopt doesn't EBADF. */
    int one = 1;
    if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0) {
        say("FAIL: setsockopt SO_REUSEADDR\n");
        _exit(2);
    }

    struct sockaddr_in sa;
    zero(&sa, sizeof sa);
    sa.sin_len    = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port   = 0;  /* ephemeral */
    sa.sin_addr   = htonl32(INADDR_LOOPBACK);
    if (bind(srv, &sa, sizeof sa) != 0) { say("FAIL: bind\n"); _exit(3); }
    if (listen(srv, 1) != 0)             { say("FAIL: listen\n"); _exit(4); }

    /* Read the port back via getsockname. */
    struct sockaddr_in srv_addr;
    zero(&srv_addr, sizeof srv_addr);
    socklen_t alen = sizeof srv_addr;
    if (getsockname(srv, &srv_addr, &alen) != 0) {
        say("FAIL: getsockname srv\n");
        _exit(5);
    }
    if (srv_addr.sin_family != AF_INET) {
        say("FAIL: srv sin_family\n");
        _exit(6);
    }
    if (srv_addr.sin_port == 0) {
        say("FAIL: srv sin_port stayed 0\n");
        _exit(7);
    }

    /* Connect a client. */
    int cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cli < 0) { say("FAIL: socket cli\n"); _exit(8); }

    sa.sin_port = srv_addr.sin_port;  /* already network order */
    if (connect(cli, &sa, sizeof sa) != 0) { say("FAIL: connect\n"); _exit(9); }

    int acc = accept(srv, 0, 0);
    if (acc < 0) { say("FAIL: accept\n"); _exit(10); }

    /* getpeername on the accepted fd must match the client's
     * getsockname. */
    struct sockaddr_in peer, mine;
    zero(&peer, sizeof peer); zero(&mine, sizeof mine);
    socklen_t plen = sizeof peer, mlen = sizeof mine;
    if (getpeername(acc, &peer, &plen) != 0 ||
        getsockname(cli, &mine, &mlen) != 0) {
        say("FAIL: peer/name on accepted pair\n");
        _exit(11);
    }
    if (peer.sin_family != AF_INET || mine.sin_family != AF_INET) {
        say("FAIL: family roundtrip\n");
        _exit(12);
    }
    if (peer.sin_port != mine.sin_port) {
        say("FAIL: port roundtrip\n");
        _exit(13);
    }

    /* getsockopt SO_REUSEADDR — should be 1 on the listener. */
    int got = 0; socklen_t glen = sizeof got;
    if (getsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &got, &glen) != 0 ||
        got == 0) {
        say("FAIL: getsockopt SO_REUSEADDR didn't stick\n");
        _exit(14);
    }

    close(acc); close(cli); close(srv);
    say("ssh-loopback ok\n");
    _exit(0);
}
