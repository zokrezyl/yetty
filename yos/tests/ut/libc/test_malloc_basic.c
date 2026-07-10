/*
 * test_malloc_basic.c — first end-to-end test of yos's mimalloc-backed
 * guest allocator.
 *
 * WHAT this verifies:
 *   - env.malloc returns a non-NULL wasm offset.
 *   - The returned region is writable (no trap on store).
 *   - env.free on it doesn't trap.
 *   - A second malloc of the same size after free reuses or extends
 *     without trapping.
 *
 * WHY this matters:
 *   Until impl/alloc.c landed, malloc was stubbed to -ENOSYS — every
 *   guest that called malloc returned NULL and crashed. This is the
 *   smallest probe that the whole bridge → mi_heap_malloc → wasm
 *   offset round-trip works.
 *
 * Expected: exit 0, stdout contains "malloc-basic ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("malloc")))
void *malloc(size_t n);

__attribute__((import_module("env"), import_name("free")))
void free(void *p);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn))
void _exit(int);

static void emit(const char *s) {
    size_t n = 0; while (s[n]) n++;
    write(1, s, n);
}

void _start(void) {
    char *p = (char *)malloc(64);
    if (!p) { emit("malloc returned NULL\n"); _exit(1); }
    /* Touch the region — if the offset is bogus, this traps. */
    for (int i = 0; i < 64; i++) p[i] = (char)(i + 1);
    /* Read back to defeat dead-store elimination. */
    int sum = 0;
    for (int i = 0; i < 64; i++) sum += p[i];
    if (sum != 64 * 65 / 2) { emit("readback mismatch\n"); _exit(2); }
    free(p);

    /* Reuse: alloc again, write, free. */
    char *q = (char *)malloc(64);
    if (!q) { emit("second malloc returned NULL\n"); _exit(3); }
    q[0] = 'X'; q[63] = 'Y';
    free(q);

    emit("malloc-basic ok\n");
    _exit(0);
}
