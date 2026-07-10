/* posix_extras.h — additive POSIX bits MSVC/Windows-SDK does not ship.
 *
 * Force-included into every yos host TU on Windows via /FI. Effects:
 *   1. brings <fcntl.h>, <signal.h>, <errno.h>, <sys/types.h>, <sys/stat.h>
 *      into scope so their MSVC-supplied definitions are visible
 *   2. adds POSIX-only constants/types those headers don't define on
 *      Windows (O_CLOEXEC, SIGHUP, sigset_t, struct sigaction, etc.)
 *
 * We deliberately DO NOT shadow the MSVC headers themselves — Windows
 * builds need to keep using MSVC's <fcntl.h> and <signal.h> so the
 * platform-native types/macros still work. */
#ifndef YOS_WIN_COMPAT_POSIX_EXTRAS_H
#define YOS_WIN_COMPAT_POSIX_EXTRAS_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>     /* _MAX_PATH */
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* GCC __atomic_* / __ATOMIC_* shim. Pulled in everywhere so shared
 * sources that touch __atomic_load_n / __atomic_compare_exchange_n / …
 * compile identically across platforms. The wasm3 build force-includes
 * the same header for the same reason (see src/wasm3/meson.build). */
#include "m3_msvc_atomics.h"

/* ── ssize_t / mode_t / pid_t / uid_t / gid_t ──────────────────────── */

#ifndef _SSIZE_T_DEFINED
typedef long long ssize_t;
#define _SSIZE_T_DEFINED 1
#endif
/* MSVC's sys/types.h defines _ino_t, _dev_t, etc., not POSIX ino_t /
 * dev_t / nlink_t / uid_t / gid_t / mode_t. */
#ifndef _MODE_T_DEFINED
typedef unsigned short mode_t;
#define _MODE_T_DEFINED 1
#endif
#ifndef _PID_T_DEFINED
typedef int pid_t;
#define _PID_T_DEFINED 1
#endif
#ifndef _UID_T_DEFINED
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#define _UID_T_DEFINED 1
#endif
#ifndef _NLINK_T_DEFINED
typedef unsigned short nlink_t;
#define _NLINK_T_DEFINED 1
#endif
#ifndef _BLKSIZE_T_DEFINED
typedef long blksize_t;
typedef long long blkcnt_t;
#define _BLKSIZE_T_DEFINED 1
#endif
#ifndef _ID_T_DEFINED
typedef int id_t;
#define _ID_T_DEFINED 1
#endif
#ifndef _USECONDS_T_DEFINED
typedef unsigned int useconds_t;
typedef int          suseconds_t;
#define _USECONDS_T_DEFINED 1
#endif

/* ── time additions ────────────────────────────────────────────────── */
/* CLOCK_* + clock_gettime / nanosleep declarations. MSVC's <time.h> has
 * struct timespec (C11) but none of the POSIX clock IDs. The bodies live
 * in compat/pthread_win32.c (where clock_gettime is paired with the
 * pthread/cond implementations that need it). */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME             0
#define CLOCK_MONOTONIC            1
#define CLOCK_PROCESS_CPUTIME_ID   2
#define CLOCK_THREAD_CPUTIME_ID    3
#define CLOCK_MONOTONIC_RAW        4
#define CLOCK_REALTIME_COARSE      5
#define CLOCK_MONOTONIC_COARSE     6
#define CLOCK_BOOTTIME             7
typedef int clockid_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct timeval;
extern int clock_gettime(clockid_t clk, struct timespec *ts);
extern int nanosleep(const struct timespec *req, struct timespec *rem);
extern int gettimeofday(struct timeval *tv, void *tz);
extern unsigned int sleep(unsigned int seconds);
extern int  usleep(unsigned long usec);
extern long sysconf(int name);
/* MSVC's <time.h> defines time / mktime / difftime / ctime / asctime /
 * gmtime / localtime as macros that expand to their _time64 variants,
 * so the bare names never appear as functions in the codegen extract.
 * Force the codegen + linker to see plain time() by declaring it here
 * (msvcrt exports `time` as an alias for _time64 on x64). */
#ifdef time
#undef time
#endif
extern time_t time(time_t *t);
#ifdef mktime
#undef mktime
#endif
extern time_t mktime(struct tm *t);
#ifdef difftime
#undef difftime
#endif
extern double difftime(time_t a, time_t b);
#ifdef gmtime
#undef gmtime
#endif
extern struct tm *gmtime(const time_t *t);
#ifdef localtime
#undef localtime
#endif
extern struct tm *localtime(const time_t *t);
#ifdef ctime
#undef ctime
#endif
extern char *ctime(const time_t *t);
#ifdef asctime
#undef asctime
#endif
extern char *asctime(const struct tm *t);
/* _r variants — POSIX thread-safe forms. Bodies live in compat_libc.c. */
extern struct tm *gmtime_r   (const time_t *t, struct tm *out);
extern struct tm *localtime_r(const time_t *t, struct tm *out);
extern char      *asctime_r  (const struct tm *t, char *buf);
extern char      *ctime_r    (const time_t *t, char *buf);
/* clock(): MSVC has it (clock_t clock(void)). Declare so it appears
 * in host-api as a function and the bridge can passthrough. */
extern clock_t clock(void);
/* clock_getres lives in compat_libc.c. */
extern int clock_getres(clockid_t clk, struct timespec *res);
/* ffs / ffsl / ffsll — POSIX find-first-set bit. Trivial bit ops;
 * MSVC has no native equivalent but our compat_libc.c implements them
 * via _BitScanForward / _BitScanForward64. */
extern int ffs  (int x);
extern int ffsl (long x);
extern int ffsll(long long x);
/* Misc POSIX strings we deliberately implement on Windows. Declared
 * so the codegen sees them as host functions. */
extern char *stpcpy (char *dst, const char *src);
extern char *stpncpy(char *dst, const char *src, size_t n);
extern void *memmem (const void *h, size_t hl, const void *n, size_t nl);
extern void *memrchr(const void *s, int c, size_t n);
extern void *mempcpy(void *dst, const void *src, size_t n);
extern char *strndup(const char *s, size_t n);
extern char *strchrnul(const char *s, int c);
extern char *strsignal(int sig);
extern char *hstrerror(int err);
extern char *l64a(long v);
extern char *ttyname(int fd);
extern char *ctermid(char *s);
extern char *ptsname(int fd);
extern char *getusershell(void);
extern char *strtok_r(char *str, const char *delim, char **saveptr);
extern char *realpath(const char *path, char *resolved);
extern char *mkdtemp(char *template_);
/* off_t variants — MSVC has no fseeko/ftello, our shim returns long
 * long. Declared so call sites don't truncate the return. */
extern long long fseeko(void *f, long long offset, int whence);
extern long long ftello(void *f);
extern void setbuffer(void *f, char *buf, size_t n);
extern void setlinebuf(void *f);
extern int   timingsafe_bcmp (const void *a, const void *b, size_t n);
extern int   timingsafe_memcmp(const void *a, const void *b, size_t n);
extern size_t strlcpy(char *dst, const char *src, size_t sz);
extern size_t strlcat(char *dst, const char *src, size_t sz);
extern char *strnstr (const char *s, const char *find, size_t n);
extern void  explicit_bzero(void *s, size_t n);
/* strcasecmp / strncasecmp — MSVC has the _-prefixed variants; we
 * provide non-prefixed wrappers (and #undef any macros the build
 * system installed) so the codegen sees them as plain functions. */
#ifdef strcasecmp
#undef strcasecmp
#endif
#ifdef strncasecmp
#undef strncasecmp
#endif
extern int strcasecmp (const char *a, const char *b);
extern int strncasecmp(const char *a, const char *b, size_t n);

#ifdef __cplusplus
}
#endif

/* ── POSIX limits.h additions ──────────────────────────────────────── */
#ifndef PATH_MAX
#define PATH_MAX  4096
#endif
#ifndef NAME_MAX
#define NAME_MAX  255
#endif
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif
#ifndef IOV_MAX
#define IOV_MAX 1024
#endif
#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif

/* ── fcntl.h additions ─────────────────────────────────────────────── */

#ifndef O_CLOEXEC
#define O_CLOEXEC   0x80000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x4000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW  0x20000
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0x10000
#endif
#ifndef O_PATH
#define O_PATH      0x200000
#endif
#ifndef O_DIRECT
#define O_DIRECT    0x40000
#endif
#ifndef O_SYNC
#define O_SYNC      0x101000
#endif
#ifndef O_DSYNC
#define O_DSYNC     0x1000
#endif
#ifndef O_ACCMODE
#define O_ACCMODE   0x3
#endif
#ifndef O_ASYNC
#define O_ASYNC     0x2000
#endif
#ifndef O_NOCTTY
#define O_NOCTTY    0x8000
#endif
#ifndef O_NDELAY
#define O_NDELAY    O_NONBLOCK
#endif
#ifndef O_EXEC
#define O_EXEC      0
#endif
#ifndef O_SEARCH
#define O_SEARCH    0
#endif
#ifndef O_TMPFILE
#define O_TMPFILE   0x400000
#endif

#ifndef F_DUPFD
#define F_DUPFD              0
#define F_GETFD              1
#define F_SETFD              2
#define F_GETFL              3
#define F_SETFL              4
#define F_GETLK              5
#define F_SETLK              6
#define F_SETLKW             7
#define F_SETOWN             8
#define F_GETOWN             9
#define F_DUPFD_CLOEXEC      1030
#endif
#ifndef FD_CLOEXEC
#define FD_CLOEXEC           1
#endif

#ifndef AT_FDCWD
#define AT_FDCWD             (-100)
#define AT_SYMLINK_NOFOLLOW  0x100
#define AT_REMOVEDIR         0x200
#define AT_SYMLINK_FOLLOW    0x400
#define AT_EMPTY_PATH        0x1000
#define AT_EACCESS           0x200
#endif

#ifndef YOS_WIN_HAS_FLOCK
struct flock {
    short     l_type;
    short     l_whence;
    long long l_start;
    long long l_len;
    int       l_pid;
};
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2
#define YOS_WIN_HAS_FLOCK 1
#endif

/* ── stat helpers MSVC lacks ───────────────────────────────────────── */

#ifndef S_ISDIR
/* MSVC's <sys/stat.h> defines _S_IFDIR / _S_IFREG / etc. but not the
 * POSIX S_IS* macros. */
#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#define S_ISCHR(m)  (((m) & _S_IFMT) == _S_IFCHR)
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#define S_ISLNK(m)  (0)
#define S_ISBLK(m)  (0)
#define S_ISSOCK(m) (0)
#endif

/* POSIX permission bits — MSVC only defines _S_IREAD / _S_IWRITE. */
#ifndef S_IRWXU
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#endif
/* Per-symbol guards because MSVC's <sys/stat.h> defines S_IFMT, S_IFDIR,
 * S_IFCHR, S_IFREG via _CRT_NONSTDC_DEPRECATE wrappers but NOT S_IFIFO. */
#ifndef S_IFMT
#define S_IFMT  _S_IFMT
#endif
#ifndef S_IFDIR
#define S_IFDIR _S_IFDIR
#endif
#ifndef S_IFREG
#define S_IFREG _S_IFREG
#endif
#ifndef S_IFCHR
#define S_IFCHR _S_IFCHR
#endif
#ifndef S_IFIFO
#define S_IFIFO _S_IFIFO
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_IFBLK
#define S_IFBLK 0060000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif

/* ── signal.h additions ────────────────────────────────────────────── */
/* MSVC's <signal.h> defines:
 *     SIGINT=2, SIGILL=4, SIGFPE=8, SIGSEGV=11, SIGTERM=15,
 *     SIGBREAK=21, SIGABRT=22 (also SIGABRT_COMPAT=6, _SIGNAL_MAX=23).
 *
 * Everything else is POSIX-only. We pick numbers ≥ 32 for those so
 * they never collide with MSVC's set even when a switch enumerates
 * both worlds in one statement. We never RECEIVE these signals on
 * Windows anyway — no controlling terminal job control, no SIGCHLD,
 * etc. — so the exact number only needs to be unique-per-symbol. */

#ifndef SIGHUP
#define SIGHUP    32
#endif
#ifndef SIGQUIT
#define SIGQUIT   33
#endif
#ifndef SIGTRAP
#define SIGTRAP   34
#endif
#ifndef SIGEMT
#define SIGEMT    35
#endif
#ifndef SIGKILL
#define SIGKILL   36
#endif
#ifndef SIGBUS
#define SIGBUS    37
#endif
#ifndef SIGSYS
#define SIGSYS    38
#endif
#ifndef SIGPIPE
#define SIGPIPE   39
#endif
#ifndef SIGALRM
#define SIGALRM   40
#endif
#ifndef SIGURG
#define SIGURG    41
#endif
#ifndef SIGSTOP
#define SIGSTOP   42
#endif
#ifndef SIGTSTP
#define SIGTSTP   43
#endif
#ifndef SIGCONT
#define SIGCONT   44
#endif
#ifndef SIGCHLD
#define SIGCHLD   45
#endif
#ifndef SIGTTIN
#define SIGTTIN   46
#endif
#ifndef SIGTTOU
#define SIGTTOU   47
#endif
#ifndef SIGIO
#define SIGIO     48
#endif
#ifndef SIGXCPU
#define SIGXCPU   49
#endif
#ifndef SIGXFSZ
#define SIGXFSZ   50
#endif
#ifndef SIGVTALRM
#define SIGVTALRM 51
#endif
#ifndef SIGPROF
#define SIGPROF   52
#endif
#ifndef SIGWINCH
#define SIGWINCH  53
#endif
#ifndef SIGUSR1
#define SIGUSR1   54
#endif
#ifndef SIGUSR2
#define SIGUSR2   55
#endif
#ifndef SIGINFO
#define SIGINFO   56
#endif
#ifndef SIGSTKFLT
#define SIGSTKFLT 57
#endif
#ifndef SIGPWR
#define SIGPWR    58
#endif
#ifndef SIGPOLL
#define SIGPOLL   SIGIO
#endif
#ifndef SIGCLD
#define SIGCLD    SIGCHLD
#endif
#ifndef NSIG
#define NSIG      64
#endif

#ifndef YOS_WIN_HAS_SIGSET_T
typedef unsigned long sigset_t;
#define YOS_WIN_HAS_SIGSET_T 1

static __inline int sigemptyset(sigset_t *s) { if (s) *s = 0; return 0; }
static __inline int sigfillset (sigset_t *s) { if (s) *s = ~(sigset_t)0; return 0; }
static __inline int sigaddset  (sigset_t *s, int n) { if (s && n > 0 && n < 32) *s |= 1u << n; return 0; }
static __inline int sigdelset  (sigset_t *s, int n) { if (s && n > 0 && n < 32) *s &= ~(1u << n); return 0; }
static __inline int sigismember(const sigset_t *s, int n) {
    return (s && n > 0 && n < 32 && (*s & (1u << n))) ? 1 : 0;
}
#endif

#ifndef SIG_BLOCK
#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2
#endif

#ifndef YOS_WIN_HAS_SIGACTION
struct sigaction {
    void   (*sa_handler)(int);
    void   (*sa_sigaction)(int, void *, void *);
    sigset_t sa_mask;
    int      sa_flags;
    void   (*sa_restorer)(void);
};

#define SA_RESTART   0x10000000
#define SA_SIGINFO   0x00000004
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000
#define SA_ONSTACK   0x08000000

extern int sigaction(int signum, const struct sigaction *act,
                     struct sigaction *oldact);
extern int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
extern int sigwait(const sigset_t *set, int *sig);
extern int sigpending(sigset_t *set);
extern int sigsuspend(const sigset_t *mask);
extern int kill(pid_t pid, int sig);

typedef struct {
    void  *ss_sp;
    int    ss_flags;
    size_t ss_size;
} stack_t;
#define SS_DISABLE 2
#define SS_ONSTACK 1

static __inline int sigaltstack(const stack_t *ss, stack_t *oss) {
    (void)ss; (void)oss; return 0;
}

typedef struct {
    int   si_signo;
    int   si_errno;
    int   si_code;
    int   si_pid;
    int   si_uid;
    int   si_status;
    void *si_addr;
} siginfo_t;

typedef enum { P_ALL = 0, P_PID = 1, P_PGID = 2 } idtype_t;
#define WEXITED   0x4
#define WNOWAIT   0x1000000

static __inline int waitid(idtype_t it, int id, siginfo_t *infop, int options) {
    (void)it; (void)id; (void)infop; (void)options; return -1;
}

#define YOS_WIN_HAS_SIGACTION 1
#endif

/* ── GCC keywords MSVC doesn't have ────────────────────────────────── */
/* __thread → MSVC's __declspec(thread). C11 _Thread_local works in
 * MSVC but the project sources use the GCC spelling. */
#ifndef __thread
#define __thread __declspec(thread)
#endif

/* __attribute__ — MSVC has no analogue for most uses. Reduce to no-op so
 * decorator-bearing decls parse. Specific attributes that MSVC honours
 * (deprecated, packed, aligned) have their own MSVC equivalents and
 * shared code is expected to use the project's portable wrappers. */
#ifndef YOS_WIN_HAS_ATTR_STUB
#define __attribute__(x)
#define YOS_WIN_HAS_ATTR_STUB 1
#endif

/* __typeof__ → MSVC C2x __typeof__ when /std:clatest, but yos pins
 * /std:c11. Use C11 _Generic on the few sites that need typeof — leave
 * the keyword unaliased so misuse fails loudly. */

/* __builtin_expect — branch hint. MSVC has none; reduce to identity. */
#ifndef __builtin_expect
#define __builtin_expect(expr, val) ((void)(val), (expr))
#endif
/* __builtin_unreachable — MSVC has __assume(0). */
#ifndef __builtin_unreachable
#define __builtin_unreachable() __assume(0)
#endif
/* __builtin_trap — MSVC has __debugbreak() / __fastfail(). */
#ifndef __builtin_trap
#define __builtin_trap() __debugbreak()
#endif
/* __builtin_prefetch — no analogue on MSVC for arbitrary addresses;
 * _mm_prefetch via <xmmintrin.h> works only when SSE intrinsics are
 * already in scope. Reduce to a no-op. */
#ifndef __builtin_prefetch
#define __builtin_prefetch(...) ((void)0)
#endif
/* __builtin_constant_p — used as a constant-fold hint. MSVC always
 * answers 0 (i.e. "not constant at parse time"); shared code uses it
 * only to enable a faster const path, never for semantic correctness. */
#ifndef __builtin_constant_p
#define __builtin_constant_p(x) (0)
#endif

/* ── errno aliases MSVC lacks ─────────────────────────────────────── */
/* MSVC's <errno.h> defines almost the whole POSIX set under VS 2017+,
 * including ENOSYS, EAFNOSUPPORT, ECONNRESET, etc. The only common
 * gaps are the BSD/Linux-only names below. */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef ELOOP
#define ELOOP 114
#endif
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif
#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif

#endif /* YOS_WIN_COMPAT_POSIX_EXTRAS_H */
