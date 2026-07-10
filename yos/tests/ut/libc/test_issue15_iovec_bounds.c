/*
 * test_issue15_iovec_bounds.c — readv/writev bounds probes.
 *
 * Issue #15 fix: yos_iovec_w32_to_host now caps iovcnt at
 * YOS_IOV_MAX (=1024), rejects negative iovcnt, validates the FULL
 * wasm iovec-table range with wptr_range, and validates each
 * iovec's [base, base+len) pair. Without these, a guest-controlled
 * iovcnt or per-iovec base+len could let the kernel walk past wasm
 * memory.
 *
 * Probes (writev to /dev/null so we don't care about the data):
 *   1. writev(fd, iov, -1) must fail with EINVAL.
 *   2. writev(fd, iov, 2000) (> YOS_IOV_MAX) must fail with EINVAL.
 *   3. writev(fd, iov_with_huge_len_on_last_entry, 2) must fail
 *      with EFAULT — the per-iovec range check catches it.
 *   4. writev(fd, valid_iov, 2) must succeed with byte-count == sum.
 *
 * Expected: exit 0, stdout contains "iovec-bounds ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

#define EINVAL 22
#define EFAULT 14
#define O_WRONLY 0x0001

struct iovec { void *iov_base; size_t iov_len; };

extern int   *__error(void);
#define errno (*__error())

extern int    open(const char *, int, ...);
extern int    close(int);
extern ssize_t writev(int, const struct iovec *, int);
extern ssize_t write(int, const void *, size_t);
extern unsigned int strlen(const char *);
extern void   _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

void _start(void)
{
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) { emit_err("FAIL: open /dev/null\n"); _exit(1); }

    struct iovec iov[3];
    iov[0].iov_base = (void *)"hello";  iov[0].iov_len = 5;
    iov[1].iov_base = (void *)" world"; iov[1].iov_len = 6;
    iov[2].iov_base = (void *)"!\n";    iov[2].iov_len = 2;

    /* (1) negative iovcnt → EINVAL. */
    errno = 0;
    if (writev(fd, iov, -1) != -1 || errno != EINVAL) {
        emit_err("FAIL: writev(fd, iov, -1) must EINVAL\n"); _exit(1);
    }

    /* (2) huge iovcnt above the YOS_IOV_MAX cap (1024). */
    errno = 0;
    if (writev(fd, iov, 2000) != -1 || errno != EINVAL) {
        emit_err("FAIL: writev(fd, iov, 2000) must EINVAL\n"); _exit(1);
    }

    /* (3) Bad per-iovec range — base near 0xfffff000 with len 0x10000
     * overflows uint32 and trips the wrap-safe range check inside
     * yos_iovec_w32_to_host. */
    struct iovec bad[2];
    bad[0].iov_base = (void *)"ok";    bad[0].iov_len = 2;
    bad[1].iov_base = (void *)0xfffff000u;
    bad[1].iov_len  = 0x10000u;
    errno = 0;
    if (writev(fd, bad, 2) != -1 || errno != EFAULT) {
        emit_err("FAIL: writev(fd, bad_iov, 2) must EFAULT\n"); _exit(1);
    }

    /* (4) Valid call still works. */
    errno = 0;
    ssize_t n = writev(fd, iov, 3);
    if (n != 13) {
        emit_err("FAIL: writev(fd, iov, 3) sum mismatch\n"); _exit(1);
    }

    close(fd);
    emit("iovec-bounds ok\n");
    _exit(0);
}
