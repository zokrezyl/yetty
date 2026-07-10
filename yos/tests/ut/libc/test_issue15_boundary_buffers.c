/*
 * test_issue15_boundary_buffers.c — buffer-range bridge probes.
 *
 * Issue #15 fix: `wptr_range(ctx, offset, len)` was added so that
 * read/write/pread/pwrite/readlink/getentropy validate the FULL
 * [offset, offset+len) range — not just the starting byte — with a
 * wrap-safe 64-bit end calc, and reject NULL+nonzero-length while
 * allowing NULL+zero-length (POSIX no-op).
 *
 * Probes:
 *   1. read(fd, NULL, 0)    must return 0 (no-op per POSIX).
 *   2. write(fd, NULL, 0)   must return 0.
 *   3. read(fd, NULL, 1)    must fail with EFAULT.
 *   4. write(fd, NULL, 1)   must fail with EFAULT.
 *   5. getentropy(NULL, 0)  must return 0.
 *   6. getentropy(NULL, 1)  must fail with EFAULT.
 *
 * If wptr_range is regressed back to the wptr-only check that
 * treated offset=0 as a valid wasm pointer regardless of len, the
 * NULL+nonzero cases would silently succeed and the kernel would
 * read/write at wasm linear-memory offset 0 — a real corruption
 * channel. If the zero-length cases are over-restricted (the prior
 * round-3 regression), POSIX-compliant guests would EFAULT on a
 * valid no-op call.
 *
 * Expected: exit 0, stdout contains "boundary-buffers ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

#define EFAULT 14
#define O_RDONLY 0x0000

extern int   *__error(void);
#define errno (*__error())

extern int    open(const char *, int, ...);
extern int    close(int);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern int    getentropy(void *, size_t);
extern unsigned int strlen(const char *);
extern void   _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

void _start(void)
{
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) { emit_err("FAIL: open /dev/null\n"); _exit(1); }

    /* (1) read(fd, NULL, 0) — POSIX no-op. */
    errno = 0;
    if (read(fd, (void *)0, 0) != 0) {
        emit_err("FAIL: read(fd, NULL, 0) should return 0\n"); _exit(1);
    }

    /* (2) write to /dev/null with NULL, 0. /dev/null is RDONLY here,
     * so use stderr instead. */
    errno = 0;
    if (write(2, (void *)0, 0) != 0) {
        emit_err("FAIL: write(2, NULL, 0) should return 0\n"); _exit(1);
    }

    /* (3) read(fd, NULL, 1) must EFAULT. */
    errno = 0;
    ssize_t rc3 = read(fd, (void *)0, 1);
    if (rc3 != -1 || errno != EFAULT) {
        emit_err("FAIL: read(fd, NULL, 1) must EFAULT\n"); _exit(1);
    }

    /* (4) write(2, NULL, 1) must EFAULT. */
    errno = 0;
    ssize_t rc4 = write(2, (void *)0, 1);
    if (rc4 != -1 || errno != EFAULT) {
        emit_err("FAIL: write(2, NULL, 1) must EFAULT\n"); _exit(1);
    }

    /* (5) getentropy(NULL, 0) — no-op per the new convention. */
    errno = 0;
    if (getentropy((void *)0, 0) != 0) {
        emit_err("FAIL: getentropy(NULL, 0) should return 0\n"); _exit(1);
    }

    /* (6) getentropy(NULL, 1) must EFAULT. */
    errno = 0;
    int rc6 = getentropy((void *)0, 1);
    if (rc6 != -1 || errno != EFAULT) {
        emit_err("FAIL: getentropy(NULL, 1) must EFAULT\n"); _exit(1);
    }

    close(fd);
    emit("boundary-buffers ok\n");
    _exit(0);
}
