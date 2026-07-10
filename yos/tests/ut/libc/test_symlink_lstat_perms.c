/*
 * test_symlink_lstat_perms.c — lstat on a symlink must return symlink
 * mode bits (S_IFLNK + 0777), NOT stat()-followed target metadata.
 *
 * WHAT this verifies:
 *   1) lstat(symlink) returns st_mode with S_IFLNK set.
 *   2) The permission bits are 0777, regardless of what the host
 *      lstat returns (darwin returns 0755 — yos normalises).
 *   3) lstat()'s st_size for a symlink is the target-string length,
 *      NOT the followed file's size.
 *
 * WHY this matters:
 *   Pre-fix `yos_fstatat` ignored the wasm AT_* flags — FreeBSD's
 *   AT_SYMLINK_NOFOLLOW=0x200 was passed straight to darwin host
 *   fstatat which doesn't recognise that bit (darwin uses 0x20),
 *   so lstat() collapsed to stat() and reported the followed file.
 *   Same `cv_stat_h2w` path silently overrode mode bits with darwin
 *   layout that left S_IFLNK at the wrong offset → ls displayed
 *   the entry as `-rw-r--r--` instead of `lrwxrwxrwx`. Both fixes
 *   land in impl/io/{io.c,cv_stat.c,fifo.c} — this test pins them.
 *
 * Expected: exit 0, stdout contains "symlink-lstat ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

extern int   *__error(void);
#define errno (*__error())

extern int  symlink(const char *, const char *);
extern int  lstat(const char *, void *);
extern int  unlink(const char *);
extern int  write(int, const void *, size_t);
extern unsigned int strlen(const char *);
extern void _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }

/* FreeBSD-i386 struct stat: st_mode @24 (uint16), st_size @96 (int64).
 * Allocate enough room for the full 208-byte struct. */
static unsigned char stbuf[208];
static int   st_mode(void)     { return *(unsigned short *)(stbuf + 24); }
static long  st_size_low(void) { return *(int *)(stbuf + 96); }

#define S_IFMT  0170000
#define S_IFLNK 0120000

void _start(void)
{
    /* Set up: a known target file and a symlink pointing to a fixed
     * string. We don't care if the target exists — lstat must not
     * follow the link. Use a path that almost certainly doesn't exist
     * so any accidental stat-follow trivially fails differently. */
    const char *target  = "/tmp/yos_test_symlink_target_DOES_NOT_EXIST_42";
    const char *linkp   = "/tmp/yos_test_symlink_link";

    /* Tear down any leftover from a previous run. */
    unlink(linkp);

    if (symlink(target, linkp) != 0) {
        emit_err("FAIL: symlink() failed\n");
        _exit(1);
    }

    /* ---- 1. lstat returns S_IFLNK + 0777, NOT a regular file. ---- */
    for (unsigned i = 0; i < sizeof stbuf; i++) stbuf[i] = 0xAA;
    if (lstat(linkp, stbuf) != 0) {
        emit_err("FAIL: lstat(symlink) returned -1; errno=");
        char b[8]; int e = errno, n = 0;
        do { b[n++] = '0' + (e % 10); e /= 10; } while (e);
        for (int i = n - 1; i >= 0; i--) write(2, &b[i], 1);
        write(2, "\n", 1);
        unlink(linkp); _exit(1);
    }
    int mode = st_mode();
    if ((mode & S_IFMT) != S_IFLNK) {
        emit_err("FAIL: lstat returned non-symlink mode — "
                 "AT_SYMLINK_NOFOLLOW translation broken in yos_fstatat. "
                 "Got mode masked = ");
        char b[16]; unsigned v = mode & S_IFMT; int n = 0;
        if (!v) b[n++] = '0';
        else { while (v) { b[n++] = '0' + (v & 7); v >>= 3; } }
        for (int i = n - 1; i >= 0; i--) write(2, &b[i], 1);
        write(2, " octal\n", 7);
        unlink(linkp); _exit(1);
    }
    if ((mode & 0777) != 0777) {
        emit_err("FAIL: symlink perms != 0777 — cv_stat_fbi forgot to "
                 "normalise the host's symlink mode (darwin returns "
                 "0755 raw; FreeBSD contract says symlinks ALWAYS have "
                 "permission 0777)\n");
        unlink(linkp); _exit(1);
    }

    /* ---- 2. st_size is the target-path length. ---- */
    int want_size = (int)strlen(target);
    if (st_size_low() != want_size) {
        emit_err("FAIL: st_size != strlen(target) — lstat followed "
                 "the link (got followed file's size or 0)\n");
        unlink(linkp); _exit(1);
    }

    unlink(linkp);
    emit("symlink-lstat ok\n");
    _exit(0);
}
