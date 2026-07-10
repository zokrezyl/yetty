/*
 * test_ssh_arc4random_buf.c — bridge fills caller buffer with entropy.
 *
 * WHAT this verifies:
 *   arc4random_buf(buf, n) writes n random bytes into buf. The bridge
 *   is hand-implemented in impl/libc/random.c (calls host getentropy
 *   in 256-byte chunks). The test pre-fills with a sentinel pattern
 *   and confirms (a) the bytes were touched at all, (b) the buffer
 *   is not all-zero, (c) two back-to-back calls don't return the
 *   same 32 bytes.
 *
 * WHY this matters:
 *   ssh seeds its CSPRNG via arc4random_buf. If the bridge silently
 *   no-ops or fails partway through, ssh's session keys collapse to
 *   the buffer's prior contents — exactly the class of bug the
 *   glibc-atfork-confused-arc4random-state workaround in random.c
 *   sidesteps.
 *
 * Expected: exit 0, stdout contains "arc4random_buf ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("arc4random_buf")))
void arc4random_buf(void *buf, size_t n);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

static int memeq(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return 0;
    return 1;
}

void _start(void) {
    /* 300 bytes > 256-byte chunk size — exercises the loop. */
    unsigned char buf[300];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = 0xCC;

    arc4random_buf(buf, sizeof buf);

    /* The bridge must have touched at least one byte. */
    int all_cc = 1;
    for (size_t i = 0; i < sizeof buf; i++) {
        if (buf[i] != 0xCC) { all_cc = 0; break; }
    }
    if (all_cc) { say("arc4random_buf FAIL: bridge no-op\n"); _exit(1); }

    /* And shouldn't be all-zero (1 in 2^2400 — effectively zero). */
    int all_zero = 1;
    for (size_t i = 0; i < sizeof buf; i++) {
        if (buf[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) { say("arc4random_buf FAIL: all zero\n"); _exit(2); }

    /* Two back-to-back fills must differ in at least one byte. */
    unsigned char a[32], b[32];
    arc4random_buf(a, sizeof a);
    arc4random_buf(b, sizeof b);
    if (memeq(a, b, sizeof a)) {
        say("arc4random_buf FAIL: identical consecutive draws\n");
        _exit(3);
    }

    say("arc4random_buf ok\n");
    _exit(0);
}
