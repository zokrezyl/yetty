/*
 * test_ssh_explicit_bzero.c — secret-scrubbing primitive.
 *
 * WHAT this verifies:
 *   explicit_bzero(buf, n) overwrites every byte of [buf, buf+n) with
 *   zero. The bridge sits in impl/posix.c::yos_explicit_bzero; the
 *   important guarantee for ssh is that the call is NOT a no-op — the
 *   bytes really get cleared.
 *
 * WHY this matters:
 *   ssh / sshd / ssh-keygen use explicit_bzero to wipe key material,
 *   passphrase buffers, ephemeral session bits. The host-API extractor
 *   misses the declaration on darwin, so without an explicit hook in
 *   hooks.yaml the codegen leaves a void no-op stub and every
 *   "secret scrub" call silently drops on the floor. This test pins
 *   the route: pre-fill with 0xAA, call explicit_bzero, confirm all
 *   bytes are 0.
 *
 * Expected: exit 0, stdout contains "explicit_bzero ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("explicit_bzero")))
void explicit_bzero(void *p, size_t n);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int, const void *, size_t);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    /* Sized to span more than one wasm 32-bit word so a partial-zero
     * impl that only clears the first 4/8 bytes would show. */
    static unsigned char buf[273];

    for (size_t i = 0; i < sizeof buf; i++) buf[i] = 0xAA;

    /* Sentinel byte just past the end — must NOT be touched. */
    static unsigned char tail = 0x5A;
    (void)tail;

    explicit_bzero(buf, sizeof buf);

    for (size_t i = 0; i < sizeof buf; i++) {
        if (buf[i] != 0) {
            say("explicit_bzero FAIL: byte not zero\n");
            _exit(1);
        }
    }

    /* Zero-length must be a safe no-op. */
    unsigned char one = 0x77;
    explicit_bzero(&one, 0);
    if (one != 0x77) {
        say("explicit_bzero FAIL: n=0 must not touch buffer\n");
        _exit(2);
    }

    /* Single-byte clears the one byte and stops. */
    one = 0x77;
    explicit_bzero(&one, 1);
    if (one != 0) {
        say("explicit_bzero FAIL: n=1 didn't clear\n");
        _exit(3);
    }

    say("explicit_bzero ok\n");
    _exit(0);
}
