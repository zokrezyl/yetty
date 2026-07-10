/*
 * test_ssh_misc_libc.c — handful of small libc bridges ssh leans on.
 *
 * WHAT this verifies:
 *   - atoi("22") == 22, atoi(" -5") == -5, atoi("abc") == 0
 *   - timingsafe_bcmp returns 0 iff buffers match
 *   - memcmp / memmove / memchr — overlapping copy, search
 *   - umask: setting and reading current mask is consistent
 *   - getpid returns a positive integer
 *   - getuid returns the host uid (must be > 0; root running the
 *     suite isn't supported)
 *   - time() advances or is stable across two calls
 *   - clock_gettime(CLOCK_MONOTONIC) is non-decreasing
 *   - setlocale(LC_ALL, "C") returns non-NULL
 *
 * WHY this matters:
 *   ssh sprinkles atoi over port-number parsing, timingsafe_bcmp
 *   over MAC verification, memmove over packet defragmentation;
 *   anything other than the bare host-libc contract here makes ssh
 *   either silently misbehave (atoi truncation) or crash (memmove
 *   overlap mishandling).
 *
 * Expected: exit 0, stdout contains "ssh-misc ok".
 */

typedef unsigned int   size_t;
typedef int            ssize_t;
typedef int            pid_t;
typedef unsigned int   uid_t;
typedef unsigned int   mode_t;

#define CLOCK_MONOTONIC 4   /* FreeBSD */

struct timespec { int tv_sec; int tv_nsec; };

__attribute__((import_module("env"), import_name("atoi")))
int atoi(const char *s);

__attribute__((import_module("env"), import_name("timingsafe_bcmp")))
int timingsafe_bcmp(const void *a, const void *b, size_t n);

__attribute__((import_module("env"), import_name("memcmp")))
int memcmp(const void *a, const void *b, size_t n);

__attribute__((import_module("env"), import_name("memmove")))
void *memmove(void *dst, const void *src, size_t n);

__attribute__((import_module("env"), import_name("memchr")))
void *memchr(const void *p, int c, size_t n);

__attribute__((import_module("env"), import_name("umask")))
mode_t umask(mode_t mask);

__attribute__((import_module("env"), import_name("getpid")))
pid_t getpid(void);

__attribute__((import_module("env"), import_name("getuid")))
uid_t getuid(void);

__attribute__((import_module("env"), import_name("time")))
int time(int *t);

__attribute__((import_module("env"), import_name("clock_gettime")))
int clock_gettime(int clk, struct timespec *ts);

__attribute__((import_module("env"), import_name("setlocale")))
char *setlocale(int category, const char *locale);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    /* atoi. */
    if (atoi("22") != 22)        { say("FAIL: atoi(\"22\")\n"); _exit(1); }
    if (atoi(" -5") != -5)       { say("FAIL: atoi(\" -5\")\n"); _exit(2); }
    if (atoi("abc") != 0)        { say("FAIL: atoi(\"abc\")\n"); _exit(3); }
    if (atoi("100rest") != 100)  { say("FAIL: atoi(\"100rest\")\n"); _exit(4); }

    /* timingsafe_bcmp. The contract: returns 0 iff the two buffers
     * are byte-identical for `n` bytes, non-zero otherwise. Linux
     * glibc has no native timingsafe_bcmp; codegen used to stub it
     * with ENOSYS / return -1 which made ssh's MAC verifier think
     * every packet's tag was wrong — exactly the symptom
     * "ssh_dispatch_run_fatal: ... message authentication code
     * incorrect" the moment encryption activates. Same stub broke
     * ssh-keygen's "-----BEGIN SSH SIGNATURE-----" magic check. */
    const char *a = "openssh!!";
    const char *b = "openssh!!";
    const char *c = "openssh!?";
    if (timingsafe_bcmp(a, b, 9) != 0) { say("FAIL: tsb equal\n"); _exit(5); }
    if (timingsafe_bcmp(a, c, 9) == 0) { say("FAIL: tsb differ\n"); _exit(6); }
    /* Empty-range comparison is always equal. */
    if (timingsafe_bcmp(a, c, 0) != 0) { say("FAIL: tsb n=0\n"); _exit(30); }
    /* Pin the exact OpenSSH call-site signature: comparing the
     * "-----BEGIN SSH SIGNATURE-----" magic against itself must
     * report equal (=0). This is the call ssh-keygen makes when
     * parsing a sig file. */
    const char *magic = "-----BEGIN SSH SIGNATURE-----";
    if (timingsafe_bcmp(magic, magic, 29) != 0) {
        say("FAIL: tsb on sshsig magic\n");
        _exit(31);
    }

    /* memcmp tracks the byte-level diff. */
    if (memcmp(a, b, 9) != 0) { say("FAIL: memcmp equal\n"); _exit(7); }
    if (memcmp(a, c, 9) == 0) { say("FAIL: memcmp differ\n"); _exit(8); }

    /* memmove must handle overlap correctly (forward case). */
    char buf[10] = "0123456789";
    memmove(buf + 1, buf, 9);
    if (buf[0] != '0' || buf[1] != '0' || buf[2] != '1' || buf[9] != '8') {
        say("FAIL: memmove forward overlap\n");
        _exit(9);
    }
    /* And backward overlap. */
    char buf2[10] = "0123456789";
    memmove(buf2, buf2 + 1, 9);
    if (buf2[0] != '1' || buf2[8] != '9') {
        say("FAIL: memmove backward overlap\n");
        _exit(10);
    }

    /* memchr. */
    const char *needle = "ssh-rsa AAAAB3";
    char *hit = memchr(needle, 'A', slen(needle));
    if (!hit || *hit != 'A' || (int)(hit - needle) != 8) {
        say("FAIL: memchr\n");
        _exit(11);
    }

    /* umask: set 022, read back via setting another and comparing. */
    mode_t prev = umask(022);
    mode_t got  = umask(prev);
    if (got != 022) { say("FAIL: umask roundtrip\n"); _exit(12); }

    /* getpid > 0, getuid > 0 (suite isn't expected to run as root). */
    if (getpid() <= 0) { say("FAIL: getpid\n"); _exit(13); }
    if (getuid() == 0) { say("FAIL: getuid says 0 (root)\n"); _exit(14); }

    /* time() must yield something > 1.7e9 (sometime after Aug 2023). */
    int t1 = 0; int rc = time(&t1);
    if (rc < 1700000000) { say("FAIL: time() unreasonably small\n"); _exit(15); }
    if (t1 != rc)        { say("FAIL: time(&t) didn't write *t\n"); _exit(16); }

    /* clock_gettime: monotonic non-decreasing. */
    struct timespec ts1, ts2;
    ts1.tv_sec = ts1.tv_nsec = 0;
    ts2.tv_sec = ts2.tv_nsec = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &ts1) != 0) {
        say("FAIL: clock_gettime first\n"); _exit(17);
    }
    if (clock_gettime(CLOCK_MONOTONIC, &ts2) != 0) {
        say("FAIL: clock_gettime second\n"); _exit(18);
    }
    if (ts2.tv_sec < ts1.tv_sec ||
        (ts2.tv_sec == ts1.tv_sec && ts2.tv_nsec < ts1.tv_nsec)) {
        say("FAIL: monotonic went backwards\n");
        _exit(19);
    }

    /* setlocale("C") — must return non-NULL on a portable platform. */
    if (setlocale(0 /* LC_ALL */, "C") == 0) {
        say("FAIL: setlocale C\n");
        _exit(20);
    }

    say("ssh-misc ok\n");
    _exit(0);
}
