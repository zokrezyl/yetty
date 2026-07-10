/*
 * test_issue15_fdopen_lifetime.c — fdopen fd-ownership probe.
 *
 * Issue #15 fix: yos_fdopen used to allocate a SECOND wasm fd slot
 * for the same host fd, then `fclose(fp)` released only that second
 * slot. The caller's original wasm fd was left mapped to a closed
 * host fd — a later `close(original_fd)` would close an unrelated
 * host descriptor the kernel had recycled.
 *
 * The fix adopts the existing wfd into the FILE handle table, so
 * fclose releases the SAME wfd the caller has.
 *
 * Test:
 *   1. open(file_a) → fd_a
 *   2. fdopen(fd_a, "r") → fp
 *   3. fclose(fp) — closes the underlying host fd, releases fd_a's slot
 *   4. open(file_b, O_CREAT) → fd_x. Without the fix, fd_x's host fd
 *      would be the recycled number that used to back fd_a — so a
 *      subsequent close(fd_a) (which the guest might do defensively)
 *      would in fact close fd_x's host fd, breaking file_b.
 *   5. defensive close(fd_a) — must NOT damage fd_x
 *   6. write to fd_x — must succeed
 *
 * Expected: exit 0, stdout contains "fdopen-lifetime ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef int           mode_t;
typedef long          off_t;

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

extern int   *__error(void);
#define errno (*__error())

extern int    open(const char *, int, ...);
extern int    close(int);
extern int    write(int, const void *, size_t);
extern int    unlink(const char *);
extern void  *fdopen(int, const char *);
extern int    fclose(void *);
extern unsigned int strlen(const char *);
extern void   _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

void _start(void)
{
    const char *path_a = "/tmp/yos_issue15_fd_a.tmp";
    const char *path_b = "/tmp/yos_issue15_fd_b.tmp";
    unlink(path_a); unlink(path_b);

    /* Create file_a with known content. */
    int seed = open(path_a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (seed < 0) { emit_err("FAIL: create path_a\n"); _exit(1); }
    write(seed, "seed", 4); close(seed);

    /* Open + fdopen + fclose. The fclose releases the host fd that
     * fd_a was mapped to. */
    int fd_a = open(path_a, O_RDONLY);
    if (fd_a < 0) { emit_err("FAIL: open path_a for read\n"); _exit(1); }
    void *fp = fdopen(fd_a, "r");
    if (!fp) { emit_err("FAIL: fdopen returned NULL\n"); _exit(1); }
    /* fclose closes the underlying host fd AND, per the fix, marks
     * fd_a as released in fd_map. */
    fclose(fp);

    /* Now open a NEW file. The host kernel may recycle the host fd
     * number that used to back fd_a. With the fix, fd_a's slot is -1
     * and our new wfd points at a fresh host fd. WITHOUT the fix,
     * fd_a still maps to the (now-recycled) host fd that backs fd_x. */
    int fd_x = open(path_b, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_x < 0) { emit_err("FAIL: open path_b\n"); _exit(1); }

    /* Defensive close on the stale fd. Must NOT damage fd_x.
     * With the fix this is EBADF (slot already released).
     * Without the fix this closes fd_x's host fd via the recycled
     * number, and the next write below EBADFs. */
    (void)close(fd_a);

    /* Write to fd_x — must succeed. */
    if (write(fd_x, "hello", 5) != 5) {
        emit_err("FAIL: write(fd_x) after close(stale fd_a) "
                 "— host fd was recycled and close clobbered it\n");
        close(fd_x); unlink(path_a); unlink(path_b); _exit(1);
    }
    close(fd_x);
    unlink(path_a); unlink(path_b);
    emit("fdopen-lifetime ok\n");
    _exit(0);
}
