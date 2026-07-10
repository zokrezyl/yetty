/*
 * test_issue15_cwd_after_chdir.c — guest cwd resolution probe.
 *
 * Issue #15 fix: fopen, openat(AT_FDCWD,…), and similar path-taking
 * bridges must resolve a relative path against ctx->cwd, not the host
 * process cwd. Without the fix, a guest chdir(tmp) followed by
 * fopen("x", "w") would create the file in yos's own startup cwd,
 * not the guest's tmp.
 *
 * This test:
 *   1. mkdir + chdir into a temp dir
 *   2. fopen("rel_a", "w") + write some bytes
 *   3. openat(AT_FDCWD, "rel_b", O_CREAT|O_WRONLY, 0644) + write
 *   4. stat(absolute path of tmp/rel_a) — must report the size we wrote
 *   5. stat(absolute path of tmp/rel_b) — must report the size we wrote
 *
 * If the fix is reverted, step 4/5 stat fails with ENOENT because
 * the file actually landed in $HOST_CWD, not $TMP_CWD.
 *
 * Expected: exit 0, stdout contains "cwd-after-chdir ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef int           mode_t;
typedef long          off_t;

struct stat { char _pad[208]; };

#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400
#define AT_FDCWD (-100)

extern int   *__error(void);
#define errno (*__error())

extern int   chdir(const char *);
extern int   mkdir(const char *, mode_t);
extern int   unlink(const char *);
extern int   rmdir(const char *);
extern void *fopen(const char *, const char *);
extern int   fclose(void *);
extern unsigned int fwrite(const void *, unsigned int, unsigned int, void *);
extern int   openat(int, const char *, int, ...);
extern int   close(int);
extern int   write(int, const void *, size_t);
extern int   stat(const char *, struct stat *);
extern unsigned int strlen(const char *);
extern void  _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }
static long st_size(const struct stat *st) {
    /* st_size at byte 96 — FreeBSD-i386 struct stat layout. */
    return *(const long *)((const char *)st + 96);
}

void _start(void)
{
    const char *dir = "/tmp/yos_issue15_cwd_test";
    /* Best-effort pre-clean (ignore errors). */
    unlink("/tmp/yos_issue15_cwd_test/rel_a");
    unlink("/tmp/yos_issue15_cwd_test/rel_b");
    rmdir(dir);

    if (mkdir(dir, 0755) != 0) {
        emit_err("FAIL: mkdir(/tmp/yos_issue15_cwd_test) failed\n");
        _exit(1);
    }
    if (chdir(dir) != 0) {
        emit_err("FAIL: chdir failed\n");
        rmdir(dir); _exit(1);
    }

    /* fopen via guest cwd: bridge must resolve "rel_a" against
     * ctx->cwd = "/tmp/yos_issue15_cwd_test". */
    void *fp = fopen("rel_a", "w");
    if (!fp) { emit_err("FAIL: fopen(rel_a) returned NULL\n"); _exit(1); }
    if (fwrite("abcdef\n", 1, 7, fp) != 7) {
        emit_err("FAIL: fwrite(rel_a) short\n"); _exit(1);
    }
    fclose(fp);

    /* openat(AT_FDCWD, "rel_b", ...) — same resolution requirement. */
    int fd = openat(AT_FDCWD, "rel_b", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        emit_err("FAIL: openat(AT_FDCWD, rel_b) failed\n"); _exit(1);
    }
    if (write(fd, "01234\n", 6) != 6) {
        emit_err("FAIL: write(rel_b) short\n"); _exit(1);
    }
    close(fd);

    /* Now stat by ABSOLUTE PATH — if the fix is missing, the files
     * went to host cwd, not /tmp/yos_issue15_cwd_test/, so these
     * absolute-path stats will fail with ENOENT. */
    struct stat st_a, st_b;
    if (stat("/tmp/yos_issue15_cwd_test/rel_a", &st_a) != 0) {
        emit_err("FAIL: stat tmp/rel_a — file landed in wrong cwd\n");
        _exit(1);
    }
    if (st_size(&st_a) != 7) {
        emit_err("FAIL: rel_a size mismatch\n"); _exit(1);
    }
    if (stat("/tmp/yos_issue15_cwd_test/rel_b", &st_b) != 0) {
        emit_err("FAIL: stat tmp/rel_b — openat(AT_FDCWD) used host cwd\n");
        _exit(1);
    }
    if (st_size(&st_b) != 6) {
        emit_err("FAIL: rel_b size mismatch\n"); _exit(1);
    }

    /* Clean up. */
    unlink("/tmp/yos_issue15_cwd_test/rel_a");
    unlink("/tmp/yos_issue15_cwd_test/rel_b");
    rmdir(dir);

    emit("cwd-after-chdir ok\n");
    _exit(0);
}
