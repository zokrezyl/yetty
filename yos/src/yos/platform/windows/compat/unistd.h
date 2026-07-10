/* <unistd.h> — MSVC/Windows-SDK has no unistd. Provide just enough of
 * the POSIX surface yos's shared sources reach for.
 *
 * Most function bodies live elsewhere: msvcrt supplies _read / _write /
 * _close / _open / _lseek / _unlink / etc., which we macro-redirect to
 * their POSIX names below. Process/identity functions (getpid, getuid)
 * have stubs in compat_stubs.c. */
#ifndef YOS_WIN_COMPAT_UNISTD_H
#define YOS_WIN_COMPAT_UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <direct.h>     /* _mkdir, _rmdir, _chdir, _getcwd */
#include <io.h>         /* _read, _write, _close, _open, _lseek, _unlink, _access, _dup */
#include <process.h>    /* _getpid */
#include <sys/types.h>

#ifndef YOS_WIN_HAS_SSIZE_T
#define YOS_WIN_HAS_SSIZE_T 1
#ifndef _SSIZE_T_DEFINED
typedef long long ssize_t;
#define _SSIZE_T_DEFINED 1
#endif
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

/* sysconf — yos's shared sources only ever ask for _SC_PAGESIZE,
 * _SC_NPROCESSORS_ONLN, _SC_OPEN_MAX, _SC_CLK_TCK. */
#define _SC_PAGESIZE           30
#define _SC_PAGE_SIZE          _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN   84
#define _SC_OPEN_MAX           4
#define _SC_CLK_TCK            2

#ifdef __cplusplus
extern "C" {
#endif

extern long sysconf(int name);
extern int  getpagesize(void);
extern unsigned int sleep(unsigned int seconds);
extern int  usleep(unsigned long usec);

/* Process / identity — Windows has _getpid in <process.h>; alias under
 * the POSIX name. The uid/gid family has no Windows analogue — return
 * 0 (root-ish) so callers' permission checks pass. */
static __inline int   getpid(void)         { return _getpid(); }
static __inline int   getppid(void)        { return 0; }
/* Pretend we're a regular (non-root) user. Tests + sshd-style checks
 * insist on getuid != 0; on Windows there's no Unix-style root, so 1000
 * is the conventional placeholder. */
static __inline unsigned getuid(void)      { return 1000; }
static __inline unsigned geteuid(void)     { return 1000; }
static __inline unsigned getgid(void)      { return 1000; }
static __inline unsigned getegid(void)     { return 1000; }
static __inline int   setuid(unsigned u)   { (void)u; return 0; }
static __inline int   seteuid(unsigned u)  { (void)u; return 0; }
static __inline int   setgid(unsigned g)   { (void)g; return 0; }
static __inline int   setsid(void)         { return _getpid(); }
static __inline int   getsid(int pid)      { (void)pid; return _getpid(); }
static __inline int   getpgrp(void)        { return _getpid(); }
static __inline int   getpgid(int pid)     { (void)pid; return _getpid(); }
static __inline int   setpgid(int pid, int pgid) { (void)pid; (void)pgid; return 0; }

/* Two-arg POSIX mkdir → msvcrt one-arg _mkdir. */
static __inline int yos_compat_mkdir2(const char *p, int mode) {
    (void)mode;
    return _mkdir(p);
}
#define mkdir yos_compat_mkdir2

/* MSVC's <io.h> already exposes POSIX-named wrappers for the standard
 * descriptor-based I/O — read, write, close, dup, dup2, lseek, unlink,
 * access, isatty, chdir, getcwd. We just include <io.h> + <direct.h>
 * above and let them resolve naturally. lseek() returns a 32-bit offset
 * on MSVC; for 64-bit we use _lseeki64 directly. */

/* dup / dup2: msvcrt's _dup asserts in the debug CRT when fd is not a
 * CRT-tracked file descriptor (sockets aren't). Our wrappers detect
 * Winsock SOCKETs and use WSADuplicateSocketW for those. */
extern int   yos_compat_dup(int fd);
extern int   yos_compat_dup2(int oldfd, int newfd);
#define dup  yos_compat_dup
#define dup2 yos_compat_dup2

extern int   pipe(int fds[2]);
extern int   ftruncate(int fd, long long len);
extern int   truncate(const char *path, long long len);
extern int   fsync(int fd);
extern int   fdatasync(int fd);
extern char *getlogin(void);
extern char *ttyname(int fd);
/* gethostname is declared by winsock2.h (WSAAPI / __stdcall) — do not
 * redeclare. Callers should #include <winsock2.h> if they need it. */
extern int   readlink(const char *path, char *buf, size_t bufsz);
extern int   symlink(const char *target, const char *linkpath);

#ifdef __cplusplus
}
#endif

#endif /* YOS_WIN_COMPAT_UNISTD_H */
