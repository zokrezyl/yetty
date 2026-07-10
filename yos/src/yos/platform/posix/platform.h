/* yos host platform abstraction (POSIX side).
 *
 * Cross-cutting host primitives that diverge between Linux and darwin
 * (macOS / iOS / tvOS). Concrete impls live next to this header in
 * platform-linux.c and platform-darwin.c; meson picks the right one
 * for host_machine.system() at configure time. The Windows port will
 * grow a sibling platform/windows/platform.h with the same surface.
 *
 * Add a function here when you find a Linux-only call that has a
 * darwin equivalent (or vice-versa); leave fully-portable POSIX calls
 * (open, close, pthread_create, kqueue, …) at their natural site.
 */
#ifndef YOS_PLATFORM_POSIX_PLATFORM_H
#define YOS_PLATFORM_POSIX_PLATFORM_H

#include <sys/types.h>
#include <sys/stat.h>

/* Host-stat accessors. Linux/darwin: `struct stat` carries st_blksize
 * and st_blocks directly. Windows: those fields don't exist; the
 * Windows platform.h emits a fallback (4096 / size>>9). Shared sources
 * never touch the raw fields — always go through these helpers. */
static inline long      yos_plat_stat_blksize(const struct stat *s) { return (long)s->st_blksize; }
static inline long long yos_plat_stat_blocks (const struct stat *s) { return (long long)s->st_blocks; }

/* Host fstat wrapper. POSIX hosts forward to fstat(2); Windows
 * additionally classifies Winsock SOCKETs (which msvcrt's _fstat does
 * not recognise) as S_IFSOCK. Returns 0/-1+errno. */
static inline int yos_plat_fstat(int hfd, struct stat *out) {
    return fstat(hfd, out);
}

/* Host process-exit. POSIX calls exit() so atexit handlers + stdio
 * flush run. Windows uses _exit() — debug CRT's exit() can deadlock
 * during DLL/FLS callback teardown when threads we created via
 * _beginthreadex are still alive, and our atexit list is empty
 * anyway. Declared __attribute__((noreturn)) where supported so
 * downstream code knows it doesn't return. */
#include <stdlib.h>
static inline void yos_plat_exit(int code) {
    exit(code);
}

/* Host fd I/O wrappers. POSIX hosts forward straight to read/write/
 * close/isatty. Windows classifies SOCKETs vs CRT fds and routes
 * each side through Winsock or msvcrt respectively — calling msvcrt's
 * _read / _write / _close / _isatty on a SOCKET fires the debug-CRT
 * invalid-parameter handler (exit 3 / assertion dialog). */
#include <unistd.h>
#include <sys/types.h>
static inline int yos_plat_close(int hfd) {
    return close(hfd);
}
static inline ssize_t yos_plat_read(int hfd, void *buf, size_t n) {
    return read(hfd, buf, n);
}
static inline ssize_t yos_plat_write(int hfd, const void *buf, size_t n) {
    return write(hfd, buf, n);
}
static inline int yos_plat_isatty(int hfd) {
    return isatty(hfd);
}

/* Translate POSIX-shape device / temp paths to host equivalents.
 * POSIX hosts pass-through; Windows maps /dev/null → NUL, /dev/tty
 * → CON, /tmp → %TEMP%, etc. */
static inline const char *yos_plat_translate_path(const char *path) {
    return path;
}

/* Host open() wrapper. POSIX passes flags straight to open(2);
 * Windows captures O_NONBLOCK / O_CLOEXEC into the per-fd flag table
 * so fcntl(F_GETFL/F_GETFD) reads them back. Returns the host fd. */
#include <fcntl.h>
static inline int yos_plat_open(const char *path, int flags, int mode) {
    return open(path, flags, mode);
}

/* Host access(). POSIX accepts F_OK/R_OK/W_OK/X_OK; MSVC's _access
 * doesn't define X_OK and the debug CRT asserts on (mode & ~6). The
 * Windows impl strips X_OK; POSIX passes through. */
static inline int yos_plat_access(const char *path, int mode) {
    return access(path, mode);
}

/* Kernel-visible thread identifier for the calling thread.
 *
 * Linux:  gettid(2)               — pid_t fits the kernel TID exactly.
 * Darwin: pthread_threadid_np(3)  — returns uint64; we truncate to
 *         pid_t for the call sites that compare/log it. The darwin
 *         id is a 64-bit monotonic counter; truncation collisions
 *         are theoretical for our use (logging, ring-buffer keys). */
pid_t yos_plat_gettid(void);

/* posix_fadvise / posix_fallocate — host-libc on Linux, fcntl(2)
 * equivalents on darwin (which has neither symbol). Both return 0 on
 * success and a positive errno on failure, matching the POSIX spec
 * (return value, *not* errno).
 *   - fadvise advice values are the POSIX numeric constants 0..5
 *     (NORMAL, RANDOM, SEQUENTIAL, WILLNEED, DONTNEED, NOREUSE) —
 *     same on FreeBSD-guest, Linux-host, and our darwin shim.
 *   - fallocate guarantees the file is at least offset+len bytes,
 *     with the new range allocated and zero-filled. */
int yos_plat_posix_fadvise(int fd, off_t offset, off_t len, int advice);
int yos_plat_posix_fallocate(int fd, off_t offset, off_t len);

#endif /* YOS_PLATFORM_POSIX_PLATFORM_H */
