/*
 * test_ssh_alloc_basic.c — malloc / calloc / realloc / free contracts.
 *
 * WHAT this verifies:
 *   - malloc returns 4-byte-aligned non-NULL pointers and the memory
 *     is writable through the returned wasm offset.
 *   - calloc zeroes the buffer.
 *   - realloc(NULL, n) == malloc(n).
 *   - realloc(p, 0) is allowed to return NULL or a free-able pointer.
 *   - realloc grows: the original contents are preserved.
 *   - free(NULL) is a no-op (must not crash).
 *
 * WHY this matters:
 *   ssh runs every channel-state buffer through malloc/realloc; an
 *   off-by-one in the realloc-grow path corrupts the channel queue
 *   and silently drops packets. The bridges in impl/mem/alloc.c sit
 *   over yos's mimalloc-on-linear-memory; the test is the smallest
 *   probe that catches "alloc returns a host pointer cast to uint32"
 *   regressions.
 *
 * Expected: exit 0, stdout contains "ssh-alloc ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("malloc")))
void *malloc(size_t n);

__attribute__((import_module("env"), import_name("calloc")))
void *calloc(size_t nmemb, size_t size);

__attribute__((import_module("env"), import_name("realloc")))
void *realloc(void *p, size_t n);

__attribute__((import_module("env"), import_name("free")))
void free(void *p);

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn)) void _exit(int);

static int slen(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write(1, s, slen(s)); }

void _start(void) {
    /* malloc + writable. */
    unsigned char *p = malloc(128);
    if (!p) { say("FAIL: malloc(128) NULL\n"); _exit(1); }
    /* 4-byte aligned (wasm32 default). */
    if (((unsigned)(unsigned long)p) & 3) {
        say("FAIL: malloc misaligned\n");
        _exit(2);
    }
    for (int i = 0; i < 128; i++) p[i] = (unsigned char)i;
    for (int i = 0; i < 128; i++) {
        if (p[i] != (unsigned char)i) {
            say("FAIL: malloc memory not writable\n");
            _exit(3);
        }
    }

    /* calloc zeros. */
    unsigned char *q = calloc(64, 4);  /* 256 bytes, all zero */
    if (!q) { say("FAIL: calloc NULL\n"); _exit(4); }
    for (int i = 0; i < 256; i++) {
        if (q[i] != 0) {
            say("FAIL: calloc didn't zero\n");
            _exit(5);
        }
    }
    free(q);

    /* realloc(NULL, n) == malloc(n). */
    unsigned char *r = realloc(0, 32);
    if (!r) { say("FAIL: realloc(NULL,32)\n"); _exit(6); }
    for (int i = 0; i < 32; i++) r[i] = (unsigned char)(0xA0 + i);

    /* realloc grow preserves prefix. */
    unsigned char *r2 = realloc(r, 128);
    if (!r2) { say("FAIL: realloc grow\n"); _exit(7); }
    for (int i = 0; i < 32; i++) {
        if (r2[i] != (unsigned char)(0xA0 + i)) {
            say("FAIL: realloc grow lost data\n");
            _exit(8);
        }
    }
    free(r2);

    /* realloc(p, 0) — POSIX allows either NULL or a freeable ptr;
     * the only contract we enforce is that it doesn't crash. */
    unsigned char *r3 = malloc(8);
    if (!r3) { say("FAIL: malloc for zero-realloc\n"); _exit(9); }
    unsigned char *r4 = realloc(r3, 0);
    /* No further check — both NULL and non-NULL are valid. */
    if (r4) free(r4);

    /* free(NULL): no-op. */
    free(0);

    /* Two large allocations must not alias. */
    char *big1 = malloc(4096);
    char *big2 = malloc(4096);
    if (!big1 || !big2 || big1 == big2) {
        say("FAIL: two mallocs alias\n");
        _exit(10);
    }
    free(big1);
    free(big2);
    free(p);

    say("ssh-alloc ok\n");
    _exit(0);
}
