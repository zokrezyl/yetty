/*
 * test_issue15_mkdirat_dirfd.c — *at-family real-dirfd probe.
 *
 * Issue #15 fix: yos_mkdirat used to forward its `dfd` arg untouched
 * to the host kernel, treating the wasm-fd number as a host fd.
 * Anything other than AT_FDCWD silently misbehaved.
 *
 * The fix routes dfd through yos_xlate_dfd. With it, mkdirat(real_dirfd,
 * "child", …) lands the new dir under the directory that real_dirfd
 * refers to. The same pattern applies to the rest of the *at family,
 * but mkdirat is the most direct test (no path resolver to muddy the
 * picture).
 *
 * Test:
 *   1. mkdir(/tmp/yos_issue15_at_parent)
 *   2. open(/tmp/yos_issue15_at_parent) with O_DIRECTORY → dirfd
 *   3. mkdirat(dirfd, "child", 0755) — must succeed
 *   4. stat(/tmp/yos_issue15_at_parent/child) — must report a directory
 *   5. cleanup
 *
 * Expected: exit 0, stdout contains "mkdirat-real-dirfd ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;
typedef int           mode_t;

#define O_RDONLY    0x0000
#define O_DIRECTORY 0x00020000
#define S_IFMT      0170000
#define S_IFDIR     0040000

struct stat { char _pad[208]; };

extern int   *__error(void);
#define errno (*__error())

extern int    open(const char *, int, ...);
extern int    close(int);
extern ssize_t write(int, const void *, size_t);
extern int    mkdir(const char *, mode_t);
extern int    mkdirat(int, const char *, mode_t);
extern int    rmdir(const char *);
extern int    stat(const char *, struct stat *);
extern unsigned int strlen(const char *);
extern void   _exit(int) __attribute__((noreturn));

static void emit(const char *s) { write(1, s, (size_t)strlen(s)); }
static void emit_err(const char *s) { write(2, s, (size_t)strlen(s)); }
static unsigned int st_mode(const struct stat *st) {
    /* st_mode @ byte 24, uint16, FreeBSD-i386 layout. */
    return *(const unsigned short *)((const char *)st + 24);
}

void _start(void)
{
    const char *parent = "/tmp/yos_issue15_at_parent";
    const char *child  = "/tmp/yos_issue15_at_parent/child";
    /* Pre-clean. */
    rmdir(child); rmdir(parent);

    if (mkdir(parent, 0755) != 0) {
        emit_err("FAIL: mkdir parent\n"); _exit(1);
    }
    int dirfd = open(parent, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        emit_err("FAIL: open parent as O_DIRECTORY\n");
        rmdir(parent); _exit(1);
    }

    /* The fix: mkdirat(dirfd, "child", 0755) creates parent/child.
     * Without the fix, dirfd was passed straight to the host kernel,
     * which interpreted it as some unrelated host-side fd number —
     * either EBADF or, worse, a successful mkdir at a host-cwd
     * relative path. */
    if (mkdirat(dirfd, "child", 0755) != 0) {
        emit_err("FAIL: mkdirat(dirfd, \"child\") failed — dfd not translated?\n");
        close(dirfd); rmdir(parent); _exit(1);
    }
    close(dirfd);

    struct stat st;
    if (stat(child, &st) != 0) {
        emit_err("FAIL: stat parent/child — mkdirat didn't land under dirfd\n");
        rmdir(parent); _exit(1);
    }
    if ((st_mode(&st) & S_IFMT) != S_IFDIR) {
        emit_err("FAIL: parent/child is not a directory\n");
        rmdir(child); rmdir(parent); _exit(1);
    }

    rmdir(child); rmdir(parent);
    emit("mkdirat-real-dirfd ok\n");
    _exit(0);
}
