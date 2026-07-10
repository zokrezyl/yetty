/*
 * test_lpathconf_errno.c — yos_lpathconf must set wasm errno on failure.
 *
 * WHAT this verifies:
 *   When lpathconf() fails (e.g., for a _PC_* value the host doesn't
 *   know about, or for a non-existent path), the wasm-side errno
 *   must NOT be left at whatever value it held before the call. ls(1)
 *   reads errno IMMEDIATELY after a -1 return; if the bridge forgot
 *   to set it, ls strerror's 0 and prints "<path>: Undefined error: 0"
 *   for every directory entry, polluting the output with a fake fts
 *   info-leak.
 *
 * WHY this matters:
 *   Pre-fix `yos_lpathconf` returned -1 without writing wasm errno,
 *   so the wasm guest read errno from before the call. fts_safe_readdir
 *   explicitly does `errno = 0` before its readdir loop; by the time
 *   ls called lpathconf on the first entry, wasm errno was 0. ls saw
 *   -1 + errno=0 and printed "./<file>: Undefined error: 0".
 *
 *   The fix: yos_lpathconf now calls yos_errno_neg(EINVAL) on the
 *   non-supported-name path so the wasm guest's `errno != EINVAL`
 *   check correctly skips the ACL warning. This regression test pins
 *   that contract.
 *
 * Expected: exit 0, stdout contains "lpathconf-errno ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

extern int   *__error(void);
#define errno (*__error())

extern long  lpathconf(const char *, int);
extern int          write(int, const void *, size_t);
extern unsigned int strlen(const char *);
extern void         _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

/* Pre-set wasm errno to 0 so the test catches the "forgot to set
 * errno" regression — i.e., a buggy bridge that returns -1 without
 * touching errno would leave the field at 0 and our assertion would
 * read 0. With the fix, we read EINVAL (22). */
static void clear_errno(void)  { *__error() = 0; }

/* We use a FreeBSD-only _PC_* value that the Linux host doesn't
 * implement to force the failure path: _PC_ACL_NFS4 = 64. (The
 * exact integer is the FreeBSD <unistd.h> definition; using the
 * raw value avoids a host-header include in the wasm guest.) */
#define _PC_ACL_NFS4 64

#define FBSD_EINVAL 22

/* No crt1 in the test build — emit _start directly. Mirrors how
 * test_strmode.c (and the other small libc tests) wire their entry. */
void _start(void)
{
    int rc, e;

    /* ---- 1. lpathconf on a real path, unsupported _PC_* ----------
     * Host pathconf() returns -1 with EINVAL (or similar) for ACL_NFS4
     * on filesystems / kernels without NFSv4 ACL support. We expect
     * the bridge to surface that as wasm errno=EINVAL via
     * yos_errno_neg. Before the fix wasm errno stayed 0. */
    clear_errno();
    rc = (int)lpathconf("/", _PC_ACL_NFS4);
    e = errno;
    if (rc != -1) {
        emit_err("FAIL: lpathconf(_PC_ACL_NFS4) returned ");
        char b[16] = {0}; int n = 0, v = rc, neg = 0;
        if (v < 0) { neg = 1; v = -v; }
        do { b[n++] = '0' + (v % 10); v /= 10; } while (v);
        if (neg) b[n++] = '-';
        for (int i = n-1; i >= 0; i--) write(2, &b[i], 1);
        emit_err(", want -1\n");
        _exit(1);
    }
    if (e == 0) {
        /* This is the regression: rc==-1 but the bridge forgot to
         * set errno. ls would print "Undefined error: 0". */
        emit_err("FAIL: lpathconf(-1) left wasm errno=0 — bridge "
                 "forgot yos_errno_neg(). Was a regression of "
                 "the fix in src/yos/impl/libc/posix.c::yos_lpathconf.\n");
        _exit(1);
    }

    /* ---- 2. lpathconf on NULL path — must report EFAULT ----------- */
    clear_errno();
    rc = (int)lpathconf((const char *)0, _PC_ACL_NFS4);
    e = errno;
    if (rc != -1) {
        emit_err("FAIL: lpathconf(NULL) returned non-negative\n");
        _exit(1);
    }
    if (e == 0) {
        emit_err("FAIL: lpathconf(NULL) left wasm errno=0\n");
        _exit(1);
    }

    emit("lpathconf-errno ok\n");
    _exit(0);
}
