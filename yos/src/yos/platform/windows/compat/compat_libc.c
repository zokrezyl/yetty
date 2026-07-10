/* compat_libc.c — Windows POSIX surface that msvcrt doesn't ship.
 *
 * Each function either wraps an msvcrt `_xxx` equivalent, calls a Win32
 * primitive, or returns -1+ENOSYS. The yos host runtime never relies on
 * any of these for correctness on Windows — sockets, threads, mmap go
 * through their proper Win32-native slices — but the linker still
 * demands symbols for every name the shared sources reference.
 *
 * Where MSVC's CRT already provides a working POSIX-named symbol
 * (read/write/close/_dup, etc.) we DO NOT redefine here; this file
 * fills only the gaps that the link step surfaces as undefined refs.
 */

#include "posix_extras.h"
#include "pthread.h"        /* clock_gettime decls live there */
#include "unistd.h"         /* _SC_* constants */
#include "dirent.h"
#include "termios.h"
#include "regex.h"
#include "pwd.h"
#include "grp.h"
#include "libgen.h"
#include "poll.h"

#include <errno.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utime.h>
#include <time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <intrin.h>

/* ── globals: tzname / timezone / daylight ─────────────────────────── */
/* MSVC's CRT keeps these under _-prefixed names. The OLDNAMES.lib that
 * ships with the SDK should alias them, but the link sometimes fails to
 * find them when /MD and /MDd diverge in how they import.  Bind plain
 * names to extern wrappers around the CRT's _tzname / _timezone /
 * _daylight via __pioinfo-style globals. */

/* MSVC exposes _tzname / _timezone / _daylight as macros that expand
 * to function calls (__p__tzname, __p__timezone, __p__daylight) rather
 * than raw symbols. We do NOT redeclare them — <time.h> already brings
 * the macros into scope. yos_tz_sync() snapshots them onto the POSIX-
 * named globals tzname[]/timezone/daylight so shared code that reads
 * those gets the current values. */

char  *tzname[2];
long   timezone;
int    daylight;

static void yos_tz_sync(void)
{
    static int inited = 0;
    if (!inited) {
        _tzset();
        tzname[0] = _tzname[0];
        tzname[1] = _tzname[1];
        timezone  = _timezone;
        daylight  = _daylight;
        inited = 1;
    }
}

/* ── thread-safe time variants ─────────────────────────────────────── */

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    return gmtime_s(out, t) == 0 ? out : NULL;
}
struct tm *localtime_r(const time_t *t, struct tm *out)
{
    yos_tz_sync();
    return localtime_s(out, t) == 0 ? out : NULL;
}
char *asctime_r(const struct tm *tm, char *buf)
{
    return asctime_s(buf, 26, tm) == 0 ? buf : NULL;
}
char *ctime_r(const time_t *t, char *buf)
{
    return ctime_s(buf, 26, t) == 0 ? buf : NULL;
}

/* ── POSIX clocks ──────────────────────────────────────────────────── */

int clock_getres(clockid_t clk, struct timespec *res)
{
    (void)clk;
    if (res) {
        /* QueryPerformanceFrequency is at least 100ns; pessimistic 1µs. */
        res->tv_sec  = 0;
        res->tv_nsec = 1000;
    }
    return 0;
}

unsigned int sleep(unsigned int seconds)
{
    Sleep(seconds * 1000U);
    return 0;
}
int usleep(unsigned long us)
{
    Sleep((DWORD)(us / 1000U));
    return 0;
}
unsigned int pause(void)
{
    errno = EINTR;
    return -1;
}

long sysconf(int name)
{
    /* Accept BOTH the FreeBSD-i386 and the Linux/glibc constant
     * values. The wasm guest's libc resolves these constants on the
     * GUEST side (FreeBSD-i386 ABI), so the integer that arrives
     * here is FreeBSD's _SC_*. We also accept Linux numbers for any
     * yos-host code that happens to call sysconf directly. */
    SYSTEM_INFO si; GetSystemInfo(&si);
    switch (name) {
    /* _SC_PAGESIZE: linux 30 / freebsd 47 */
    case 30: case 47:
        return (long)si.dwPageSize;
    /* _SC_NPROCESSORS_ONLN: linux 84 / freebsd 58 */
    case 84: case 58:
        return (long)si.dwNumberOfProcessors;
    /* _SC_OPEN_MAX: linux 4 / freebsd 5 */
    case 4: case 5:
        return _getmaxstdio();
    /* _SC_CLK_TCK: linux 2 / freebsd 3 */
    case 2: case 3:
        return 1000;
    default:
        errno = EINVAL;
        return -1;
    }
}
int getpagesize(void)
{
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (int)si.dwPageSize;
}

/* ── filesystem stubs ─────────────────────────────────────────────── */

static int yos_win_is_abs_path(const char *p)
{
    if (!p || !p[0]) return 0;
    if (p[0] == '/' || p[0] == '\\') return 1;
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
           p[1] == ':';
}

static const char *yos_win_strip_nt_prefix(char *p)
{
    if (strncmp(p, "\\\\?\\UNC\\", 8) == 0) {
        p[0] = '\\';
        p[1] = '\\';
        memmove(p + 2, p + 8, strlen(p + 8) + 1);
    } else if (strncmp(p, "\\\\?\\", 4) == 0) {
        memmove(p, p + 4, strlen(p + 4) + 1);
    }
    return p;
}

static const char *yos_win_at_path(int dfd, const char *p, char *buf, size_t buflen)
{
    if (!p) {
        errno = EFAULT;
        return NULL;
    }
    if (dfd == AT_FDCWD || yos_win_is_abs_path(p)) return p;

    intptr_t osfh = _get_osfhandle(dfd);
    if (osfh == -1) {
        errno = EBADF;
        return NULL;
    }
    DWORD n = GetFinalPathNameByHandleA((HANDLE)osfh, buf, (DWORD)buflen,
                                        FILE_NAME_NORMALIZED);
    if (n == 0 || n >= buflen) {
        errno = (n >= buflen) ? ENAMETOOLONG : EBADF;
        return NULL;
    }
    yos_win_strip_nt_prefix(buf);
    size_t used = strlen(buf);
    if (used && buf[used - 1] != '\\' && buf[used - 1] != '/') {
        if (used + 1 >= buflen) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        buf[used++] = '\\';
        buf[used] = 0;
    }
    if (used + strlen(p) >= buflen) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(buf + used, p);
    return buf;
}

extern int yos_plat_open(const char *path, int flags, int mode);

int chown    (const char *p, uid_t u, gid_t g)            { (void)p;(void)u;(void)g; errno = ENOSYS; return -1; }
int fchown   (int fd, uid_t u, gid_t g)                   { (void)fd;(void)u;(void)g; errno = ENOSYS; return -1; }
int lchown   (const char *p, uid_t u, gid_t g)            { (void)p;(void)u;(void)g; errno = ENOSYS; return -1; }
int fchownat (int dfd, const char *p, uid_t u, gid_t g, int f) { (void)dfd;(void)p;(void)u;(void)g;(void)f; errno = ENOSYS; return -1; }
int fchmod   (int fd, mode_t m)                           { (void)fd;(void)m; errno = ENOSYS; return -1; }
int fchmodat (int dfd, const char *p, mode_t m, int f)    { (void)dfd;(void)p;(void)m;(void)f; errno = ENOSYS; return -1; }
int faccessat(int dfd, const char *p, int m, int f)       { (void)f; char b[MAX_PATH * 4]; const char *q = yos_win_at_path(dfd, p, b, sizeof b); return q ? _access(q, m) : -1; }
int fchdir   (int fd)                                     { (void)fd; errno = ENOSYS; return -1; }
int lstat    (const char *p, struct stat *st)             { return _stat64i32(p, (struct _stat64i32 *)st); }
int fstatat  (int dfd, const char *p, struct stat *st, int f) { (void)f; char b[MAX_PATH * 4]; const char *q = yos_win_at_path(dfd, p, b, sizeof b); return q ? _stat64i32(q, (struct _stat64i32 *)st) : -1; }
int mkdirat  (int dfd, const char *p, mode_t m)           { (void)m; char b[MAX_PATH * 4]; const char *q = yos_win_at_path(dfd, p, b, sizeof b); return q ? _mkdir(q) : -1; }
int mknodat  (int dfd, const char *p, mode_t m, dev_t d)  { (void)dfd;(void)p;(void)m;(void)d; errno = ENOSYS; return -1; }
int openat   (int dfd, const char *p, int f, ...)         {
    int mode = 0;
    if (f & _O_CREAT) {
        va_list ap; va_start(ap, f); mode = va_arg(ap, int); va_end(ap);
    }
    char b[MAX_PATH * 4];
    const char *q = yos_win_at_path(dfd, p, b, sizeof b);
    return q ? yos_plat_open(q, f, mode) : -1;
}
int link     (const char *o, const char *n)               { return CreateHardLinkA(n, o, NULL) ? 0 : (errno = EPERM, -1); }
int linkat   (int od, const char *o, int nd, const char *n, int f) { (void)f; char ob[MAX_PATH * 4], nb[MAX_PATH * 4]; const char *op = yos_win_at_path(od, o, ob, sizeof ob); const char *np = yos_win_at_path(nd, n, nb, sizeof nb); return (op && np) ? link(op, np) : -1; }
int symlink  (const char *t, const char *p)               {
    /* Probe target type and pass SYMBOLIC_LINK_FLAG_DIRECTORY when
     * appropriate. SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x2)
     * lets unprivileged processes create symlinks on Windows 10 1703+
     * when Developer Mode is on — without the bit, the call fails
     * with ERROR_PRIVILEGE_NOT_HELD on a stock user account. */
    DWORD flags = 0x2;  /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
    DWORD attrs = GetFileAttributesA(t);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        flags |= 0x1;  /* SYMBOLIC_LINK_FLAG_DIRECTORY */
    if (CreateSymbolicLinkA(p, t, flags)) return 0;
    DWORD err = GetLastError();
    /* Retry without the unprivileged flag for hosts that don't grok
     * the bit (Windows 8.1 / unpatched Win10). */
    if (err == ERROR_INVALID_PARAMETER &&
        CreateSymbolicLinkA(p, t, flags & ~0x2u)) return 0;
    errno = (err == ERROR_PRIVILEGE_NOT_HELD) ? EPERM
          : (err == ERROR_PATH_NOT_FOUND)     ? ENOENT
          : (err == ERROR_FILE_NOT_FOUND)     ? ENOENT
          : (err == ERROR_ALREADY_EXISTS)     ? EEXIST
          :                                     EIO;
    return -1;
}
int symlinkat(const char *t, int dfd, const char *p)      { char b[MAX_PATH * 4]; const char *q = yos_win_at_path(dfd, p, b, sizeof b); return q ? symlink(t, q) : -1; }
int readlink (const char *p, char *b, size_t n)           { (void)p;(void)b;(void)n; errno = ENOSYS; return -1; }
int readlinkat(int dfd, const char *p, char *b, size_t n) { (void)dfd;(void)p;(void)b;(void)n; errno = ENOSYS; return -1; }
int unlinkat (int dfd, const char *p, int f)              { char b[MAX_PATH * 4]; const char *q = yos_win_at_path(dfd, p, b, sizeof b); if (!q) return -1; return (f & AT_REMOVEDIR) ? _rmdir(q) : _unlink(q); }
int renameat (int od, const char *o, int nd, const char *n) { char ob[MAX_PATH * 4], nb[MAX_PATH * 4]; const char *op = yos_win_at_path(od, o, ob, sizeof ob); const char *np = yos_win_at_path(nd, n, nb, sizeof nb); return (op && np) ? rename(op, np) : -1; }
int truncate (const char *p, long long len)               {
    int fd = _open(p, _O_RDWR | _O_BINARY); if (fd < 0) return -1;
    int r = _chsize_s(fd, len); _close(fd); return r ? -1 : 0;
}
int ftruncate(int fd, long long len)                      { return _chsize_s(fd, len) ? -1 : 0; }
int fsync    (int fd)                                     {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    return (h && h != INVALID_HANDLE_VALUE && FlushFileBuffers(h)) ? 0 : -1;
}
int fdatasync(int fd)                                     { return fsync(fd); }
void sync    (void)                                       { /* no-op on Windows */ }
int mincore  (void *a, size_t n, unsigned char *v) {
    /* Windows has no direct mincore. The yos snapshot path uses
     * `(vec[i] & 1u)` to mark "page is present in physical memory" —
     * answer YES for every page so the fork snapshot copies the
     * entire wasm linear memory rather than the small page-unaligned
     * prefix and zero middle. Cost: a fork copies more bytes than
     * necessary; correctness: the child runtime sees a faithful
     * memory snapshot instead of a mostly-zero shell. */
    (void)a;
    if (v) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        size_t pg = si.dwPageSize ? si.dwPageSize : 4096;
        size_t pages = (n + pg - 1) / pg;
        memset(v, 1, pages);
    }
    return 0;
}
int posix_madvise(void *a, size_t n, int adv)             { (void)a;(void)n;(void)adv; return 0; }
char *realpath(const char *p, char *out)                  {
    if (!p) { errno = EINVAL; return NULL; }
    char *buf = out ? out : (char *)malloc(PATH_MAX);
    if (!buf) { errno = ENOMEM; return NULL; }

    /* For POSIX-shape inputs ("/tmp/x/..", "/foo/./bar/..") do a
     * pure-string canonicalisation — keeps the result in POSIX shape
     * regardless of what the host CRT thinks. The wasm guest expects
     * POSIX-shape returns; running GetFullPathNameA would emit
     * "C:\\Users\\…" which fails every test that expects "/tmp". */
    if (p[0] == '/') {
        size_t pl = strlen(p);
        if (pl >= PATH_MAX) { if (!out) free(buf); errno = ENAMETOOLONG; return NULL; }
        /* Tokenize on '/'. Build buf by appending segments and popping
         * on "..". */
        buf[0] = '/';
        size_t out_len = 1;
        const char *s = p + 1;
        while (*s) {
            const char *seg = s;
            while (*s && *s != '/') s++;
            size_t sl = (size_t)(s - seg);
            if (sl == 0) {
                /* skip empty segment from "//" */
            } else if (sl == 1 && seg[0] == '.') {
                /* skip "." */
            } else if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
                /* pop one segment from buf (don't go above root). */
                if (out_len > 1) {
                    out_len--;  /* trailing '/' */
                    while (out_len > 1 && buf[out_len - 1] != '/') out_len--;
                }
            } else {
                if (buf[out_len - 1] != '/') {
                    if (out_len + 1 >= PATH_MAX) { if (!out) free(buf); errno = ENAMETOOLONG; return NULL; }
                    buf[out_len++] = '/';
                }
                if (out_len + sl + 1 >= PATH_MAX) { if (!out) free(buf); errno = ENAMETOOLONG; return NULL; }
                memcpy(buf + out_len, seg, sl);
                out_len += sl;
            }
            while (*s == '/') s++;
        }
        /* Strip trailing slash (except for root). */
        if (out_len > 1 && buf[out_len - 1] == '/') out_len--;
        buf[out_len] = 0;
        return buf;
    }

    /* Non-POSIX-shape input (Windows-native, e.g. "C:\\..."): fall back
     * to GetFullPathNameA. */
    DWORD n = GetFullPathNameA(p, PATH_MAX, buf, NULL);
    if (n == 0 || n >= PATH_MAX) {
        if (!out) free(buf);
        errno = ENOENT; return NULL;
    }
    return buf;
}
long pathconf(const char *p, int name) { (void)p;(void)name; return -1; }

/* mkstemp/mktemp family — msvcrt has _mktemp_s; create + open. */
int mkstemp(char *tmpl) {
    if (!tmpl) { errno = EINVAL; return -1; }
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0) return -1;
    return _open(tmpl, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
}
int mkostemp (char *t, int flags)              { (void)flags; return mkstemp(t); }
int mkostemps(char *t, int slen, int flags)    { (void)slen; (void)flags; return mkstemp(t); }
char *mkdtemp(char *tmpl) {
    if (!tmpl) { errno = EINVAL; return NULL; }
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0) return NULL;
    return _mkdir(tmpl) == 0 ? tmpl : NULL;
}

/* ── pipe / fcntl ─────────────────────────────────────────────────── */

int pipe(int fds[2]) {
    return _pipe(fds, 65536, _O_BINARY);
}

/* Per-fd flag store. POSIX fcntl(F_GETFL/F_SETFL/F_GETFD/F_SETFD)
 * needs a place to record O_NONBLOCK / O_CLOEXEC per-fd; Windows has
 * no native equivalent — SOCKETs track non-block via ioctlsocket and
 * CRT fds track nothing.
 *
 * CRT fds always fit in the direct-mapped array (msvcrt's _nhandle is
 * bounded to <8192). Winsock SOCKET handles, however, are raw kernel
 * object integers and can land anywhere in the uintptr_t range —
 * larger than YOS_WIN_FDFLAGS_MAX, sometimes much larger. Mapping
 * those into bucket 0 corrupted stdin's flag state and aliased
 * unrelated large sockets onto each other (reviewer finding #5).
 *
 * Layout: direct-mapped array for the low fd range (fast path) + a
 * small linear-probed open-addressed hash for out-of-range fds. The
 * hash is sized to hold a few hundred concurrent SOCKETs; insertions
 * past capacity fall back to "no record" (F_GETFL returns 0 / F_SETFL
 * is dropped) — that's strictly better than the corruption it
 * replaces. */
#define YOS_WIN_FDFLAGS_MAX     65536
#define YOS_WIN_HASH_FLAGS_CAP  512   /* power of two */

static volatile long g_fdflags[YOS_WIN_FDFLAGS_MAX];
static volatile long g_fdmode [YOS_WIN_FDFLAGS_MAX];

struct yos_fd_hash_entry {
    int  fd;       /* 0 = empty */
    long flags;
    long mode;
};
static struct yos_fd_hash_entry g_fdflags_hash[YOS_WIN_HASH_FLAGS_CAP];
static SRWLOCK g_fdflags_hash_lock = SRWLOCK_INIT;

static unsigned yos_fdflags_hash_probe(int fd)
{
    /* Knuth-style multiplicative hash; fd is uintptr-shaped so cast
     * through unsigned to drop sign-extension noise. */
    unsigned h = ((unsigned)fd * 2654435761u);
    return h & (YOS_WIN_HASH_FLAGS_CAP - 1);
}

/* Look up an out-of-range fd in the hash. Returns a pointer to the
 * entry (which may be empty / freshly-claimed) or NULL if the table
 * is full AND the fd isn't already present. Lock held by caller. */
static struct yos_fd_hash_entry *yos_fdflags_hash_locate(int fd, int create)
{
    unsigned h = yos_fdflags_hash_probe(fd);
    for (unsigned i = 0; i < YOS_WIN_HASH_FLAGS_CAP; i++) {
        unsigned j = (h + i) & (YOS_WIN_HASH_FLAGS_CAP - 1);
        struct yos_fd_hash_entry *e = &g_fdflags_hash[j];
        if (e->fd == fd) return e;
        if (e->fd == 0) {
            if (!create) return NULL;
            e->fd = fd;
            return e;
        }
    }
    return NULL;
}

/* Direct-array index for low fds. -1 means "use hash path". */
static int yos_fdflags_idx(int fd) {
    if (fd >= 0 && fd < YOS_WIN_FDFLAGS_MAX) return fd;
    return -1;
}

static long yos_fdflags_load(int fd) {
    int idx = yos_fdflags_idx(fd);
    if (idx >= 0) return g_fdflags[idx];
    AcquireSRWLockShared(&g_fdflags_hash_lock);
    struct yos_fd_hash_entry *e = yos_fdflags_hash_locate(fd, 0);
    long v = e ? e->flags : 0;
    ReleaseSRWLockShared(&g_fdflags_hash_lock);
    return v;
}

static void yos_fdflags_store(int fd, long v) {
    int idx = yos_fdflags_idx(fd);
    if (idx >= 0) { _InterlockedExchange(&g_fdflags[idx], v); return; }
    AcquireSRWLockExclusive(&g_fdflags_hash_lock);
    struct yos_fd_hash_entry *e = yos_fdflags_hash_locate(fd, 1);
    if (e) e->flags = v;
    ReleaseSRWLockExclusive(&g_fdflags_hash_lock);
}

static long yos_fdmode_load(int fd) {
    int idx = yos_fdflags_idx(fd);
    if (idx >= 0) return g_fdmode[idx];
    AcquireSRWLockShared(&g_fdflags_hash_lock);
    struct yos_fd_hash_entry *e = yos_fdflags_hash_locate(fd, 0);
    long v = e ? e->mode : 0;
    ReleaseSRWLockShared(&g_fdflags_hash_lock);
    return v;
}

static void yos_fdmode_store(int fd, long v) {
    int idx = yos_fdflags_idx(fd);
    if (idx >= 0) { _InterlockedExchange(&g_fdmode[idx], v); return; }
    AcquireSRWLockExclusive(&g_fdflags_hash_lock);
    struct yos_fd_hash_entry *e = yos_fdflags_hash_locate(fd, 1);
    if (e) e->mode = v;
    ReleaseSRWLockExclusive(&g_fdflags_hash_lock);
}

/* Called from yos_plat_open after a successful host open(); records the
 * O_NONBLOCK / O_CLOEXEC bits so fcntl(F_GETFL/F_GETFD) reads them
 * back. Reset the slot completely (old fd values may have leaked
 * non-zero bits from a previous fd assignment). */
void yos_fdflags_record_open(int fd, int flags) {
    /* Store O_NONBLOCK / O_APPEND / O_ACCMODE etc. — the bits POSIX
     * fcntl(F_GETFL) reads back. CLOEXEC lives in a separate high bit
     * (0x40000000) so the same long carries both fields. */
    long v = flags & (O_NONBLOCK | O_ACCMODE | O_APPEND);
    if (flags & O_CLOEXEC) v |= 0x40000000;
    yos_fdflags_store(fd, v);
}

/* g_fdmode (declared with g_fdflags above) tracks the POSIX permission
 * bits of files we opened with O_CREAT. Windows' filesystem stores
 * only the readonly attribute, so a later fstat() would return
 * _S_IREAD | _S_IWRITE (0666) regardless of the umask-masked mode the
 * caller asked for. We remember the mode here and yos_plat_fstat
 * splices it back. Routes through yos_fdmode_load/store so out-of-
 * range SOCKETs use the hash side and don't trample fd 0's bucket. */

void yos_fdmode_record(int fd, int mode) {
    yos_fdmode_store(fd, mode & 0777);
}

int yos_fdmode_get(int fd) {
    return (int)yos_fdmode_load(fd);
}

/* fdkind tracking (declared in platform-windows.c). Forward-declared
 * here so the fcntl(F_DUPFD) and dup wrappers below can copy the
 * synthetic device kind across the dup. */
extern int  yos_fdkind_get(int fd);
extern void yos_fdkind_set(int fd, int kind);

int fcntl(int fd, int cmd, ...) {
    if (cmd == F_GETFL) {
        return (int)yos_fdflags_load(fd);
    }
    if (cmd == F_SETFL) {
        va_list ap; va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);
        yos_fdflags_store(fd, flags);
        /* Push through to the SOCKET side too so the kernel knows. */
        int sock_type = 0; int slen = (int)sizeof sock_type;
        if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE,
                       (char *)&sock_type, &slen) == 0) {
            u_long nb = (flags & O_NONBLOCK) ? 1 : 0;
            ioctlsocket((SOCKET)fd, FIONBIO, &nb);
        }
        return 0;
    }
    if (cmd == F_GETFD) {
        return (yos_fdflags_load(fd) & 0x40000000) ? FD_CLOEXEC : 0;
    }
    if (cmd == F_SETFD) {
        va_list ap; va_start(ap, cmd);
        int fdflag = va_arg(ap, int);
        va_end(ap);
        long cur = yos_fdflags_load(fd);
        long new_v = (fdflag & FD_CLOEXEC) ? (cur | 0x40000000) : (cur & ~0x40000000);
        yos_fdflags_store(fd, new_v);
        return 0;
    }
    if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
        /* Socket fds aren't in the CRT's fd table — _dup() on them
         * asserts in the debug CRT (dup.cpp: "fh >= 0 && (unsigned)fh
         * < (unsigned)_nhandle"). Detect via getsockopt(SO_TYPE) and
         * use WSADuplicateSocketW + WSASocketW to obtain a fresh
         * SOCKET that references the same underlying transport. */
        int st = 0; int sl = (int)sizeof st;
        if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE,
                       (char *)&st, &sl) == 0) {
            WSAPROTOCOL_INFOW pi;
            if (WSADuplicateSocketW((SOCKET)fd,
                                    GetCurrentProcessId(), &pi) != 0) {
                errno = EBADF;
                return -1;
            }
            SOCKET ns = WSASocketW(pi.iAddressFamily, pi.iSocketType,
                                   pi.iProtocol, &pi, 0,
                                   WSA_FLAG_OVERLAPPED);
            if (ns == INVALID_SOCKET) { errno = EIO; return -1; }
            int nfd = (int)ns;
            if (cmd == F_DUPFD_CLOEXEC) {
                yos_fdflags_store(nfd, 0x40000000);
            }
            return nfd;
        }
        int nfd = _dup(fd);
        if (nfd >= 0) {
            int kind = yos_fdkind_get(fd);
            if (kind != 0) yos_fdkind_set(nfd, kind);
            if (cmd == F_DUPFD_CLOEXEC) {
                yos_fdflags_store(nfd, 0x40000000);
            }
        }
        return nfd;
    }
    return 0;
}

/* Socket-aware dup/dup2: msvcrt's _dup asserts in the debug CRT for any
 * fd that isn't a registered CRT file descriptor. Winsock SOCKETs are
 * such fds (they're raw kernel object handles cast to int), so we route
 * them through WSADuplicateSocketW + WSASocketW instead. */
#undef dup
#undef dup2

int yos_compat_dup(int fd)
{
    int st = 0; int sl = (int)sizeof st;
    if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE,
                   (char *)&st, &sl) == 0) {
        WSAPROTOCOL_INFOW pi;
        if (WSADuplicateSocketW((SOCKET)fd,
                                GetCurrentProcessId(), &pi) != 0) {
            errno = EBADF; return -1;
        }
        SOCKET ns = WSASocketW(pi.iAddressFamily, pi.iSocketType,
                               pi.iProtocol, &pi, 0,
                               WSA_FLAG_OVERLAPPED);
        if (ns == INVALID_SOCKET) { errno = EIO; return -1; }
        return (int)ns;
    }
    int newfd = _dup(fd);
    if (newfd >= 0) {
        /* Carry the synthetic device kind (YOS_WIN_FD_NULL etc) across
         * the dup so a later fstat/poll on the dup'd fd still gets the
         * S_IFCHR fast-path. Without this propagation the dup'd fd
         * appears as YOS_WIN_FD_REGULAR and tests that dup /dev/null
         * and poll the dup miss the "always ready" synthesis. */
        int kind = yos_fdkind_get(fd);
        if (kind != 0) yos_fdkind_set(newfd, kind);
    }
    return newfd;
}

int yos_compat_dup2(int oldfd, int newfd)
{
    int st = 0; int sl = (int)sizeof st;
    if (getsockopt((SOCKET)oldfd, SOL_SOCKET, SO_TYPE,
                   (char *)&st, &sl) == 0) {
        /* Source is a SOCKET. There's no native dup2 for SOCKETs; we
         * emulate by closing the existing newfd handle (whichever kind
         * it is) and duplicating the source SOCKET into a fresh handle.
         * Caller's fd table treats both ints uniformly.
         *
         * NB: this can't actually place the new SOCKET at the exact
         * numeric value `newfd` because Windows allocates handles
         * itself. yos's fd-table layer (yos_fd_assign) takes a host
         * fd and remaps it into the wasm-side slot the caller wants,
         * so the host's numeric identity doesn't have to match. */
        int sst = 0; int ssl = (int)sizeof sst;
        if (getsockopt((SOCKET)newfd, SOL_SOCKET, SO_TYPE,
                       (char *)&sst, &ssl) == 0) {
            closesocket((SOCKET)newfd);
        } else {
            /* Best-effort close of a CRT fd; _close asserts on invalid
             * fds, so probe with _get_osfhandle first. */
            HANDLE h = (HANDLE)_get_osfhandle(newfd);
            if (h != INVALID_HANDLE_VALUE) _close(newfd);
        }
        return yos_compat_dup(oldfd);
    }
    return _dup2(oldfd, newfd);
}
#define dup  yos_compat_dup
#define dup2 yos_compat_dup2

/* ioctl(FIONBIO) on a SOCKET works natively; track the bit in our
 * flag store too so fcntl(F_GETFL) reads it back. */
int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    if (!fds || nfds == 0) {
        if (timeout_ms > 0) Sleep((DWORD)timeout_ms);
        return 0;
    }

    DWORD start = GetTickCount();
    for (;;) {
        int ready = 0;
        int saw_pipe = 0;

        for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) continue;

            int st = 0; int sl = (int)sizeof st;
            if (getsockopt((SOCKET)fds[i].fd, SOL_SOCKET, SO_TYPE,
                           (char *)&st, &sl) == 0) {
                continue;
            }

            HANDLE h = (HANDLE)_get_osfhandle(fds[i].fd);
            if (h == INVALID_HANDLE_VALUE) {
                fds[i].revents = POLLNVAL;
                ready++;
                continue;
            }
            if (GetFileType(h) != FILE_TYPE_PIPE) {
                continue;
            }
            saw_pipe = 1;

            if (fds[i].events & POLLOUT) {
                fds[i].revents |= POLLOUT;
            }
            if (fds[i].events & POLLIN) {
                DWORD avail = 0;
                if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
                    if (avail > 0) fds[i].revents |= POLLIN;
                } else {
                    DWORD e = GetLastError();
                    if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED)
                        fds[i].revents |= POLLHUP;
                    else
                        fds[i].revents |= POLLERR;
                }
            }
            if (fds[i].revents) ready++;
        }

        /* WSAPoll uses Winsock event constants (POLLIN=0x300, POLLOUT
         * =0x010, etc.) that disagree with the FreeBSD/Linux values we
         * present to callers via poll.h. Translate at the boundary. */
        #define YOS_WIN_POLLRDNORM 0x0100
        #define YOS_WIN_POLLRDBAND 0x0200
        #define YOS_WIN_POLLIN     (YOS_WIN_POLLRDNORM | YOS_WIN_POLLRDBAND)
        #define YOS_WIN_POLLPRI    0x0400
        #define YOS_WIN_POLLWRNORM 0x0010
        #define YOS_WIN_POLLOUT    YOS_WIN_POLLWRNORM
        #define YOS_WIN_POLLWRBAND 0x0020
        #define YOS_WIN_POLLERR    0x0001
        #define YOS_WIN_POLLHUP    0x0002
        #define YOS_WIN_POLLNVAL   0x0004
        WSAPOLLFD wfds[64];
        int       wmap[64];
        ULONG     wn = 0;
        for (nfds_t i = 0; i < nfds && wn < (ULONG)(sizeof wfds / sizeof wfds[0]); i++) {
            int st = 0; int sl = (int)sizeof st;
            if (fds[i].fd >= 0 &&
                getsockopt((SOCKET)fds[i].fd, SOL_SOCKET, SO_TYPE,
                           (char *)&st, &sl) == 0) {
                short e = fds[i].events;
                SHORT we = 0;
                if (e & POLLIN)  we |= YOS_WIN_POLLIN;
                if (e & POLLPRI) we |= YOS_WIN_POLLPRI;
                if (e & POLLOUT) we |= YOS_WIN_POLLOUT;
                wfds[wn].fd      = (SOCKET)fds[i].fd;
                wfds[wn].events  = we;
                wfds[wn].revents = 0;
                wmap[wn] = (int)i;
                wn++;
            }
        }

        if (wn > 0) {
            int sock_timeout = saw_pipe ? 0 : timeout_ms;
            int rc = WSAPoll(wfds, wn, sock_timeout);
            if (rc < 0) {
                errno = EINVAL;
                return -1;
            }
            for (ULONG j = 0; j < wn; j++) {
                SHORT wr = wfds[j].revents;
                if (wr) {
                    short r = 0;
                    if (wr & (YOS_WIN_POLLRDNORM | YOS_WIN_POLLRDBAND))
                        r |= POLLIN;
                    if (wr & YOS_WIN_POLLPRI)    r |= POLLPRI;
                    if (wr & YOS_WIN_POLLWRNORM) r |= POLLOUT;
                    if (wr & YOS_WIN_POLLERR)    r |= POLLERR;
                    if (wr & YOS_WIN_POLLHUP)    r |= POLLHUP;
                    if (wr & YOS_WIN_POLLNVAL)   r |= POLLNVAL;
                    fds[wmap[j]].revents = r;
                    ready++;
                }
            }
        }

        if (ready || timeout_ms == 0 || (wn > 0 && !saw_pipe)) return ready;
        if (timeout_ms > 0 && GetTickCount() - start >= (DWORD)timeout_ms) return 0;
        Sleep(1);
    }
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap; va_start(ap, request);
    if (request == FIONBIO) {
        int *pv = va_arg(ap, int *);
        va_end(ap);
        int nb = pv ? (*pv != 0) : 0;
        long cur = yos_fdflags_load(fd);
        long new_v = nb ? (cur | O_NONBLOCK) : (cur & ~O_NONBLOCK);
        yos_fdflags_store(fd, new_v);
        /* If this is a Winsock SOCKET, push the bit through. Otherwise
         * the flag is tracked purely in our table — Windows pipes can't
         * be made non-blocking via ioctl natively, but yos's read/write
         * shims honour O_NONBLOCK when the caller queries it. */
        int sock_type = 0; int slen = (int)sizeof sock_type;
        if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE,
                       (char *)&sock_type, &slen) == 0) {
            u_long u = (u_long)nb;
            (void)ioctlsocket((SOCKET)fd, FIONBIO, &u);
        }
        return 0;
    }
    if (request == FIONREAD) {
        int *pv = va_arg(ap, int *);
        va_end(ap);
        u_long avail = 0;
        if (ioctlsocket((SOCKET)fd, FIONREAD, &avail) == 0) {
            if (pv) *pv = (int)avail;
            return 0;
        }
        if (pv) *pv = 0;
        return 0;
    }
    va_end(ap);
    errno = EINVAL;
    return -1;
}

/* ── stdio extras ─────────────────────────────────────────────────── */

long long fseeko(FILE *f, long long off, int whence) { return _fseeki64(f, off, whence); }
long long ftello(FILE *f)                            { return _ftelli64(f); }
int getc_unlocked(FILE *f)  { return getc(f); }
int putc_unlocked(int c, FILE *f) { return putc(c, f); }
void setbuffer(FILE *f, char *buf, size_t n) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, n); }
void setlinebuf(FILE *f) { setvbuf(f, NULL, _IOLBF, 0); }
int dprintf(int fd, const char *fmt, ...) {
    char tmp[2048];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if (n > (int)sizeof tmp) n = (int)sizeof tmp;
    return _write(fd, tmp, (unsigned)n);
}
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) { errno = EINVAL; return -1; }
    if (!*lineptr || *n == 0) { *n = 256; *lineptr = (char *)malloc(*n); if (!*lineptr) return -1; }
    size_t pos = 0; int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t nn = *n * 2;
            char *p = (char *)realloc(*lineptr, nn);
            if (!p) return -1;
            *lineptr = p; *n = nn;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = 0;
    return (ssize_t)pos;
}

/* ── readv / writev / pread / pwrite ──────────────────────────────── */

ssize_t pread (int fd, void *buf, size_t n, long long off)        {
    long long save = _lseeki64(fd, 0, SEEK_CUR);
    if (save < 0) return -1;
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    int r = _read(fd, buf, (unsigned)n);
    _lseeki64(fd, save, SEEK_SET);
    return r;
}
ssize_t pwrite(int fd, const void *buf, size_t n, long long off)  {
    long long save = _lseeki64(fd, 0, SEEK_CUR);
    if (save < 0) return -1;
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    int r = _write(fd, buf, (unsigned)n);
    _lseeki64(fd, save, SEEK_SET);
    return r;
}
ssize_t readv (int fd, const struct iovec *iov, int n)             {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        int r = _read(fd, iov[i].iov_base, (unsigned)iov[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if (r < (int)iov[i].iov_len) break;
    }
    return total;
}
ssize_t writev(int fd, const struct iovec *iov, int n)             {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        int r = _write(fd, iov[i].iov_base, (unsigned)iov[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if (r < (int)iov[i].iov_len) break;
    }
    return total;
}
ssize_t preadv (int fd, const struct iovec *iov, int n, long long off) {
    long long save = _lseeki64(fd, 0, SEEK_CUR);
    if (save < 0) return -1;
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = readv(fd, iov, n);
    _lseeki64(fd, save, SEEK_SET);
    return r;
}
ssize_t pwritev(int fd, const struct iovec *iov, int n, long long off) {
    long long save = _lseeki64(fd, 0, SEEK_CUR);
    if (save < 0) return -1;
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = writev(fd, iov, n);
    _lseeki64(fd, save, SEEK_SET);
    return r;
}

/* ── env ──────────────────────────────────────────────────────────── */

int setenv(const char *name, const char *value, int overwrite) {
    if (!name) { errno = EINVAL; return -1; }
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value ? value : "");
}
int unsetenv(const char *name) {
    if (!name) { errno = EINVAL; return -1; }
    return _putenv_s(name, "");
}

/* ── process / signal ─────────────────────────────────────────────── */

pid_t fork(void) { errno = ENOSYS; return -1; }
int   killpg(int pgrp, int sig) { (void)pgrp; (void)sig; errno = ENOSYS; return -1; }
char *strsignal(int sig) {
    static char buf[16];
    snprintf(buf, sizeof buf, "Signal %d", sig);
    return buf;
}
int   uname(struct utsname *u) {
    if (!u) { errno = EFAULT; return -1; }
    strncpy(u->sysname,  "Windows", sizeof u->sysname  - 1);
    DWORD n = sizeof u->nodename; GetComputerNameA(u->nodename, &n);
    OSVERSIONINFOA vi = { sizeof vi };
    /* GetVersionEx is deprecated but still works; the precise version
     * isn't security-critical for our compat path. */
    #pragma warning(push)
    #pragma warning(disable: 4996)
    if (GetVersionExA(&vi)) {
        snprintf(u->release, sizeof u->release, "%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion);
        snprintf(u->version, sizeof u->version, "Build %lu", vi.dwBuildNumber);
    }
    #pragma warning(pop)
    strncpy(u->machine, "x86_64", sizeof u->machine - 1);
    return 0;
}
int getgroups(int gidsetsize, gid_t *grouplist) {
    (void)grouplist;
    return gidsetsize > 0 ? 0 : 0;
}
int getloadavg(double avg[], int n) {
    for (int i = 0; i < n; i++) avg[i] = 0.0;
    return n;
}

int getrlimit(int r, struct rlimit *rl) {
    (void)r;
    if (rl) { rl->rlim_cur = RLIM_INFINITY; rl->rlim_max = RLIM_INFINITY; }
    return 0;
}
int setrlimit(int r, const struct rlimit *rl) {
    (void)r; (void)rl; return 0;
}

/* getrusage — fill ru_utime/ru_stime from GetProcessTimes / GetThreadTimes.
 * Other rusage fields are left zero (no Win32 analogue without GetProcessIoCounters,
 * which we skip to keep the call cheap). */
static void filetime_to_timeval(const FILETIME *ft, struct timeval *tv) {
    uint64_t hundreds = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    tv->tv_sec  = (long)(hundreds / 10000000ULL);
    tv->tv_usec = (long)((hundreds % 10000000ULL) / 10);
}
int getrusage(int who, struct rusage *ru) {
    if (!ru) { errno = EFAULT; return -1; }
    memset(ru, 0, sizeof *ru);
    FILETIME create, exit_, kern, user;
    BOOL ok = FALSE;
    if (who == RUSAGE_SELF || who == RUSAGE_CHILDREN) {
        ok = GetProcessTimes(GetCurrentProcess(), &create, &exit_, &kern, &user);
    } else if (who == RUSAGE_THREAD) {
        ok = GetThreadTimes(GetCurrentThread(), &create, &exit_, &kern, &user);
    } else {
        errno = EINVAL; return -1;
    }
    if (!ok) { errno = EINVAL; return -1; }
    filetime_to_timeval(&user, &ru->ru_utime);
    filetime_to_timeval(&kern, &ru->ru_stime);
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
        ru->ru_maxrss = (long)(pmc.PeakWorkingSetSize / 1024);
    }
    return 0;
}

/* ── tty ──────────────────────────────────────────────────────────── */

char *ctermid(char *s) {
    static char buf[8];
    char *out = s ? s : buf;
    strcpy(out, "CON:");
    return out;
}
char *ttyname(int fd) {
    (void)fd;
    return _isatty(fd) ? "CON:" : NULL;
}
int   ttyname_r(int fd, char *buf, size_t n) {
    const char *t = ttyname(fd);
    if (!t) return ENOTTY;
    if (n < strlen(t) + 1) return ERANGE;
    strcpy(buf, t);
    return 0;
}
int posix_openpt(int flags) { (void)flags; errno = ENOSYS; return -1; }
int grantpt(int fd)         { (void)fd; errno = ENOSYS; return -1; }
int unlockpt(int fd)        { (void)fd; errno = ENOSYS; return -1; }
char *ptsname(int fd)       { (void)fd; errno = ENOSYS; return NULL; }
int ptsname_r(int fd, char *b, size_t n) { (void)fd;(void)b;(void)n; return ENOSYS; }

int tcgetattr(int fd, struct termios *t) { (void)fd; if (t) memset(t, 0, sizeof *t); return 0; }
int tcsetattr(int fd, int when, const struct termios *t) { (void)fd;(void)when;(void)t; return 0; }
int tcsendbreak(int fd, int dur) { (void)fd;(void)dur; return 0; }
int tcdrain(int fd) { (void)fd; return 0; }
int tcflush(int fd, int q) { (void)fd;(void)q; return 0; }
int tcflow(int fd, int a)  { (void)fd;(void)a; return 0; }
speed_t cfgetispeed(const struct termios *t) { (void)t; return B9600; }
speed_t cfgetospeed(const struct termios *t) { (void)t; return B9600; }
int cfsetispeed(struct termios *t, speed_t s) { (void)t;(void)s; return 0; }
int cfsetospeed(struct termios *t, speed_t s) { (void)t;(void)s; return 0; }
int cfsetspeed (struct termios *t, speed_t s) { (void)t;(void)s; return 0; }
void cfmakeraw(struct termios *t) { if (t) memset(t, 0, sizeof *t); }

/* ── networking helpers ───────────────────────────────────────────── */

/* AF_UNIX socketpair loopback: bind a unix-domain listener to a unique
 * temp path, connect a client, accept, return [accepted, client]. Used
 * when the caller asked for AF_UNIX (the FreeBSD wasm guest's libuv /
 * channel pipes do). Returns 0 on success and fills sv[].
 *
 * Windows native AF_UNIX (Win10 1803+) doesn't expose socketpair() but
 * does support the bind/listen/connect/accept dance over filesystem
 * paths. Using real AF_UNIX sockets is what makes getsockname /
 * uv_guess_handle / SO_TYPE behave like FreeBSD on these fds. */
static int yos_socketpair_unix(int sv[2]) {
    static volatile LONG s_inited;
    if (InterlockedCompareExchange(&s_inited, 1, 0) == 0) {
        WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
    }
    SOCKET l = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l == INVALID_SOCKET) return -1;
    struct sockaddr_un {
        unsigned short sun_family;
        char           sun_path[108];
    } addr = {0};
    addr.sun_family = AF_UNIX;
    char tmpdir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof tmpdir, tmpdir);
    if (n == 0 || n >= sizeof tmpdir) { closesocket(l); return -1; }
    snprintf(addr.sun_path, sizeof addr.sun_path,
             "%syos-spw-%lu-%lu.sock",
             tmpdir, (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetTickCount());
    DeleteFileA(addr.sun_path);
    int alen = (int)(sizeof(unsigned short) + strlen(addr.sun_path) + 1);
    if (bind(l, (struct sockaddr *)&addr, alen) != 0 ||
        listen(l, 1) != 0) {
        closesocket(l); DeleteFileA(addr.sun_path); return -1;
    }
    SOCKET c = socket(AF_UNIX, SOCK_STREAM, 0);
    if (c == INVALID_SOCKET) {
        closesocket(l); DeleteFileA(addr.sun_path); return -1;
    }
    if (connect(c, (struct sockaddr *)&addr, alen) != 0) {
        closesocket(c); closesocket(l);
        DeleteFileA(addr.sun_path); return -1;
    }
    SOCKET s = accept(l, NULL, NULL);
    closesocket(l);
    DeleteFileA(addr.sun_path);
    if (s == INVALID_SOCKET) { closesocket(c); return -1; }
    sv[0] = (int)s; sv[1] = (int)c;
    return 0;
}

int socketpair(int dom, int type, int proto, int sv[2]) {
    (void)type; (void)proto;
    /* AF_UNIX caller: use real AF_UNIX sockets so getsockname reports
     * AF_UNIX (1) instead of AF_INET (2), matching the FreeBSD wasm
     * guest's libuv / channel-pipe expectations. Fall back to AF_INET
     * loopback only on systems too old to support AF_UNIX (Win10 <1803,
     * pre-2018). */
    if (dom == AF_UNIX) {
        if (yos_socketpair_unix(sv) == 0) return 0;
        /* fall through to AF_INET loopback */
    }
    static volatile LONG s_inited;
    if (InterlockedCompareExchange(&s_inited, 1, 0) == 0) {
        WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
    }
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l == INVALID_SOCKET) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int alen = sizeof addr;
    if (bind(l, (struct sockaddr *)&addr, alen) != 0 ||
        getsockname(l, (struct sockaddr *)&addr, &alen) != 0 ||
        listen(l, 1) != 0) {
        closesocket(l); return -1;
    }
    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET) { closesocket(l); return -1; }
    if (connect(c, (struct sockaddr *)&addr, alen) != 0) {
        closesocket(c); closesocket(l); return -1;
    }
    SOCKET s = accept(l, NULL, NULL);
    closesocket(l);
    if (s == INVALID_SOCKET) { closesocket(c); return -1; }
    sv[0] = (int)s; sv[1] = (int)c;
    return 0;
}
char *hstrerror(int err) {
    static char buf[64];
    snprintf(buf, sizeof buf, "WSA error %d", err);
    return buf;
}
unsigned int if_nametoindex(const char *name) { (void)name; return 0; }
char        *if_indextoname(unsigned int idx, char *name) {
    (void)idx; if (name) name[0] = 0; return NULL;
}

/* ── pwd / grp / users ────────────────────────────────────────────── */

static struct passwd g_pw = {
    .pw_name = "user", .pw_passwd = "", .pw_uid = 0, .pw_gid = 0,
    .pw_gecos = "", .pw_dir = "C:\\Users\\Default", .pw_shell = "",
};
struct passwd *getpwnam(const char *n)    { (void)n; return &g_pw; }
struct passwd *getpwuid(uid_t u)          { (void)u; return &g_pw; }
int getpwnam_r(const char *n, struct passwd *p, char *b, size_t bsz, struct passwd **r) {
    (void)n;(void)b;(void)bsz;
    if (p) *p = g_pw; if (r) *r = p; return 0;
}
int getpwuid_r(uid_t u, struct passwd *p, char *b, size_t bsz, struct passwd **r) {
    (void)u;(void)b;(void)bsz;
    if (p) *p = g_pw; if (r) *r = p; return 0;
}
struct passwd *getpwent(void) { return NULL; }
void setpwent(void) {}
void endpwent(void) {}

static struct group g_gr = { .gr_name = "users", .gr_passwd = "", .gr_gid = 0, .gr_mem = NULL };
struct group *getgrnam(const char *n) { (void)n; return &g_gr; }
struct group *getgrgid(gid_t g)       { (void)g; return &g_gr; }
int getgrnam_r(const char *n, struct group *g, char *b, size_t bsz, struct group **r) {
    (void)n;(void)b;(void)bsz; if (g) *g = g_gr; if (r) *r = g; return 0;
}
int getgrgid_r(gid_t gid, struct group *g, char *b, size_t bsz, struct group **r) {
    (void)gid;(void)b;(void)bsz; if (g) *g = g_gr; if (r) *r = g; return 0;
}
struct group *getgrent(void) { return NULL; }
void setgrent(void) {}
void endgrent(void) {}

char *getlogin(void) {
    static char buf[256];
    DWORD n = sizeof buf;
    if (!GetUserNameA(buf, &n)) return "user";
    return buf;
}
void  setusershell(void) {}
char *getusershell(void) { return NULL; }
void  endusershell(void) {}

/* ── string extras ────────────────────────────────────────────────── */

char *strsep(char **stringp, const char *delim) {
    if (!stringp || !*stringp) return NULL;
    char *start = *stringp;
    char *p = strpbrk(start, delim);
    if (p) { *p = 0; *stringp = p + 1; }
    else   { *stringp = NULL; }
    return start;
}
char *strtok_r(char *str, const char *delim, char **save) {
    return strtok_s(str, delim, save);
}
char *strptime(const char *buf, const char *fmt, struct tm *tm) {
    /* MSVC has no strptime; we provide a no-op stub that returns NULL
     * so callers detect failure and fall back. */
    (void)buf; (void)fmt; (void)tm;
    return NULL;
}

/* ── libgen ───────────────────────────────────────────────────────── */

char *basename(char *path) {
    if (!path || !*path) return ".";
    size_t n = strlen(path);
    /* Strip trailing slashes. */
    while (n > 1 && (path[n-1] == '/' || path[n-1] == '\\')) path[--n] = 0;
    char *p = path + n;
    while (p > path && p[-1] != '/' && p[-1] != '\\') p--;
    return p;
}
char *dirname(char *path) {
    if (!path || !*path) return ".";
    size_t n = strlen(path);
    while (n > 1 && (path[n-1] == '/' || path[n-1] == '\\')) path[--n] = 0;
    char *last = NULL;
    for (size_t i = 0; i < n; i++)
        if (path[i] == '/' || path[i] == '\\') last = path + i;
    if (!last) { strcpy(path, "."); return path; }
    if (last == path) { path[1] = 0; return path; }
    *last = 0;
    return path;
}

/* ── locale ───────────────────────────────────────────────────────── */

char *nl_langinfo(int item) {
    switch (item) {
    case 14 /* CODESET */: return "UTF-8";
    default: return "";
    }
}

/* ── random ──────────────────────────────────────────────────────── */

int getentropy(void *buf, size_t buflen) {
    if (buflen > 256) { errno = EIO; return -1; }
    HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
    if (!bcrypt) { errno = EIO; return -1; }
    typedef LONG (WINAPI *BG_t)(void *, void *, ULONG, ULONG);
    BG_t BCryptGenRandom = (BG_t)(uintptr_t)GetProcAddress(bcrypt, "BCryptGenRandom");
    LONG r = BCryptGenRandom ? BCryptGenRandom(NULL, buf, (ULONG)buflen, 2 /*BCRYPT_USE_SYSTEM_PREFERRED_RNG*/) : -1;
    FreeLibrary(bcrypt);
    if (r != 0) { errno = EIO; return -1; }
    return 0;
}

/* ── conversion ───────────────────────────────────────────────────── */

char *l64a(long v) {
    static char buf[7];
    static const char a64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int i; unsigned long x = (unsigned long)v;
    for (i = 0; i < 6 && x; i++) { buf[i] = a64[x & 0x3f]; x >>= 6; }
    buf[i] = 0;
    return buf;
}

/* ── ydev backend symbols ────────────────────────────────────────── */
/* The ydev sensor/location backends on Windows are pure stubs; the
 * common surface declares ydev_loc_read / ydev_sensor_read for the
 * core dispatcher to call. Provide empty bodies here so the link
 * resolves. */
int  ydev_loc_read   (void *l, void *out)   { (void)l;(void)out; return -1; }
int  ydev_sensor_read(void *s, void *out)   { (void)s;(void)out; return -1; }

/* ── filesystems ──────────────────────────────────────────────────── */

int fstatfs(int fd, void *buf) { (void)fd; (void)buf; errno = ENOSYS; return -1; }
int setitimer(int which, const struct itimerval *iv, struct itimerval *old)
{ (void)which; (void)iv; (void)old; errno = ENOSYS; return -1; }
int getitimer(int which, struct itimerval *iv)
{ (void)which; (void)iv; errno = ENOSYS; return -1; }
int futimes(int fd, const struct timeval tv[2]) { (void)fd; (void)tv; errno = ENOSYS; return -1; }
int lutimes(const char *p, const struct timeval tv[2]) { (void)p; (void)tv; errno = ENOSYS; return -1; }
int utimes (const char *p, const struct timeval tv[2]) {
    if (!p || !tv) { errno = EINVAL; return -1; }
    struct __utimbuf64 ub;
    ub.actime  = (__time64_t)tv[0].tv_sec;
    ub.modtime = (__time64_t)tv[1].tv_sec;
    return _utime64(p, &ub);
}

/* ── regex (stubs that fail loudly) ─────────────────────────────── */

int    regcomp (regex_t *r, const char *p, int f) { (void)r;(void)p;(void)f; return REG_BADPAT; }
int    regexec (const regex_t *r, const char *s, size_t nm, regmatch_t pm[], int f) {
    (void)r;(void)s;(void)nm;(void)pm;(void)f; return REG_NOMATCH;
}
size_t regerror(int e, const regex_t *r, char *b, size_t bsz) {
    (void)r; if (b && bsz) snprintf(b, bsz, "regex error %d", e); return 0;
}
void   regfree (regex_t *r) { (void)r; }

/* The codegen now emits cv_timeval_w2h directly — no manual override
 * needed here (would cause LNK2005 multiple definition). */

/* ── POSIX ffs family ──────────────────────────────────────────────── */
int ffs(int x) {
    unsigned long i;
    return _BitScanForward(&i, (unsigned long)(unsigned)x) ? (int)i + 1 : 0;
}
int ffsl(long x) {
    unsigned long i;
    return _BitScanForward(&i, (unsigned long)x) ? (int)i + 1 : 0;
}
int ffsll(long long x) {
    unsigned long i;
    return _BitScanForward64(&i, (unsigned long long)x) ? (int)i + 1 : 0;
}

/* ── POSIX string extras MSVC lacks ──────────────────────────────── */
char *stpcpy(char *dst, const char *src) {
    while ((*dst = *src)) { dst++; src++; }
    return dst;
}
char *stpncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    char *end = dst + i;
    for (; i < n; i++) dst[i] = 0;
    return end;
}
void *memmem(const void *h, size_t hl, const void *n, size_t nl) {
    if (nl == 0) return (void *)h;
    if (hl < nl) return NULL;
    const unsigned char *hs = (const unsigned char *)h;
    const unsigned char *ns = (const unsigned char *)n;
    for (size_t i = 0; i + nl <= hl; i++) {
        if (memcmp(hs + i, ns, nl) == 0) return (void *)(hs + i);
    }
    return NULL;
}
void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = n; i--; ) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return NULL;
}
void *mempcpy(void *dst, const void *src, size_t n) {
    memcpy(dst, src, n);
    return (char *)dst + n;
}
char *strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}
char *strchrnul(const char *s, int c) {
    while (*s && *s != (char)c) s++;
    return (char *)s;
}
int timingsafe_bcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    unsigned r = 0;
    for (size_t i = 0; i < n; i++) r |= (unsigned)(pa[i] ^ pb[i]);
    return (int)((r | (0u - r)) >> (sizeof(unsigned) * 8 - 1));
}
size_t strlcpy(char *dst, const char *src, size_t sz) {
    size_t s = strlen(src);
    if (sz > 0) {
        size_t c = (s >= sz) ? sz - 1 : s;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return s;
}
size_t strlcat(char *dst, const char *src, size_t sz) {
    size_t d = 0;
    while (d < sz && dst[d]) d++;
    size_t s = strlen(src);
    if (d == sz) return d + s;
    size_t rem = sz - d - 1;
    size_t c = (s > rem) ? rem : s;
    memcpy(dst + d, src, c);
    dst[d + c] = 0;
    return d + s;
}
char *strnstr(const char *s, const char *find, size_t n) {
    size_t fl = strlen(find);
    if (fl == 0) return (char *)s;
    for (size_t i = 0; i + fl <= n && s[i]; i++) {
        if (strncmp(s + i, find, fl) == 0) return (char *)(s + i);
    }
    return NULL;
}
void explicit_bzero(void *s, size_t n) {
    volatile unsigned char *p = (volatile unsigned char *)s;
    while (n--) *p++ = 0;
}
int strcasecmp(const char *a, const char *b)  { return _stricmp(a, b); }
int strncasecmp(const char *a, const char *b, size_t n) {
    return _strnicmp(a, b, n);
}
int timingsafe_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    int lt = 0, gt = 0;
    for (size_t i = 0; i < n; i++) {
        int d = pa[i] - pb[i];
        lt |= (d >> 31) & ~gt;
        gt |= ((-d) >> 31) & ~lt;
    }
    return gt - lt;
}

/* ── GCC builtins MSVC doesn't have ──────────────────────────────── */

unsigned __builtin_ctz(unsigned x) {
    unsigned long i;
    return _BitScanForward(&i, x) ? (unsigned)i : 32u;
}
/* Some shared files call __atomic_or_fetch / __atomic_and_fetch (newer
 * GCC builtins not covered by the load/store/exchange/cas set in
 * m3_msvc_atomics.h). Provide tiny inline wrappers via Interlocked. */
int __atomic_or_fetch_4(int volatile *p, int v, int mo) {
    (void)mo; return _InterlockedOr((long volatile *)p, (long)v) | v;
}
int __atomic_and_fetch_4(int volatile *p, int v, int mo) {
    (void)mo; return _InterlockedAnd((long volatile *)p, (long)v) & v;
}
