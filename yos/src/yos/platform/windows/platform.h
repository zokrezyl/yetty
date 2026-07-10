/* yos host platform abstraction (Windows side).
 *
 * Same surface as platform/posix/platform.h — main.c, sig.c, kqueue
 * slices etc. include this header through `platform/<os>/platform.h`
 * via the meson include-dirs setup. Concrete impls live in
 * platform-windows.c alongside this file.
 *
 * yos's wasm guest still sees a FreeBSD-shape libc; the host we
 * translate to here is whichever POSIX surface MinGW-w64 / msvcrt
 * gives us on top of Win32. Pieces with no Windows analogue
 * (posix_fadvise, posix_fallocate) get best-effort or EINVAL
 * implementations.
 */
#ifndef YOS_PLATFORM_WINDOWS_PLATFORM_H
#define YOS_PLATFORM_WINDOWS_PLATFORM_H

#include <sys/types.h>
#include <sys/stat.h>

/* Host-stat accessors. MSVC's `struct stat` has no st_blksize / st_blocks;
 * synth them from st_size (one default page, blocks of 512). The POSIX
 * platform.h returns the real fields; shared sources call only these
 * helpers. */
static __inline long      yos_plat_stat_blksize(const struct stat *s) {
    (void)s; return 4096;
}
static __inline long long yos_plat_stat_blocks (const struct stat *s) {
    return ((long long)s->st_size + 511) >> 9;
}

/* Host fstat wrapper. Detect Winsock SOCKETs (which msvcrt's _fstat
 * doesn't recognise) and synthesise an S_IFSOCK-shaped stat. */
extern int yos_plat_fstat(int hfd, struct stat *out);

/* Host process-exit. On Windows we route through Win32 ExitProcess —
 * MSVC's debug CRT exit()/_exit() both stall in DLL_PROCESS_DETACH +
 * FLS-callback teardown when worker threads we created via
 * _beginthreadex (and their parked FlsAlloc slots) are still alive.
 * ExitProcess terminates immediately, calling DLL detaches once but
 * not running the CRT's atexit chain. */
#include <stdlib.h>
extern void yos_plat_exit(int code);

/* Close a host fd. Detect Winsock SOCKETs first (msvcrt's _close on a
 * SOCKET fires the debug-CRT invalid-parameter handler → exit 3),
 * and route those to closesocket(). Plain CRT fds fall through to
 * _close. Returns 0 / -1+errno. */
extern int yos_plat_close(int hfd);

/* Host fd read / write / isatty wrappers. Route Winsock SOCKETs to
 * send / recv / closesocket / always-not-tty and CRT fds to _read /
 * _write / _close / _isatty. Without these wrappers msvcrt fires the
 * debug-CRT assertion handler the first time a guest does write()
 * on a socketpair fd. */
#include <sys/types.h>
extern ssize_t yos_plat_read (int hfd, void *buf, size_t n);
extern ssize_t yos_plat_write(int hfd, const void *buf, size_t n);
extern int     yos_plat_isatty(int hfd);

/* Translate POSIX-shape paths the wasm guest hands us to Windows-shape
 * paths the host CRT understands. Maps POSIX device paths to internal
 * sentinels handled by yos_plat_open/read/write, and /tmp/<x> to
 * %TEMP%\<x>. Pure-string translation; result is either `path` itself
 * or a thread-local buffer the caller must not free. */
extern const char *yos_plat_translate_path(const char *path);

/* Host open() wrapper. Stamps the per-fd flag table with O_NONBLOCK
 * and O_CLOEXEC from `flags` so a later fcntl(F_GETFL) / F_GETFD
 * reads them back. */
extern int yos_plat_open(const char *path, int flags, int mode);

/* Host access() wrapper. Strips POSIX X_OK (bit 1) — msvcrt's _access
 * only accepts F_OK/R_OK/W_OK (0/4/2) and the debug CRT asserts on
 * anything else. Treating X_OK as F_OK matches NTFS reality (the
 * file system has no execute bit; any file you can read can be
 * spawned by CreateProcess). */
extern int yos_plat_access(const char *path, int mode);

/* On MinGW-w64 <sys/types.h> typedefs pid_t through to int. We keep
 * the POSIX signature so call sites don't change between host slices. */

/* Kernel-visible thread identifier for the calling thread.
 * Win32 GetCurrentThreadId returns a 32-bit unsigned; we truncate
 * the same way platform-darwin.c does for pthread_threadid_np. */
pid_t yos_plat_gettid(void);

/* posix_fadvise: no direct equivalent on Windows. We accept the POSIX
 * advice values 0..5 and return 0 (treat every advice as a no-op).
 * For invalid advice we return EINVAL — matching the POSIX spec's
 * "return value is the error, not -1+errno" convention. */
int yos_plat_posix_fadvise(int fd, off_t offset, off_t len, int advice);

/* posix_fallocate: Windows can extend a file via SetEndOfFile after
 * SetFilePointerEx, but the bytes between the previous EOF and the
 * new one are not guaranteed zero-filled by default; we explicitly
 * write zeros in chunks across the new range so callers see POSIX
 * semantics. Returns 0 on success or a positive errno on failure. */
int yos_plat_posix_fallocate(int fd, off_t offset, off_t len);

#endif /* YOS_PLATFORM_WINDOWS_PLATFORM_H */
