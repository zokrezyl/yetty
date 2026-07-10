/* Windows host impls of the platform abstraction.
 *
 * Built only when host_machine.system() == 'windows'. Linux/darwin
 * have their own translation units in platform/posix/. */

#include "platform.h"

#include <errno.h>
#include <io.h>          /* _get_osfhandle */
#include <stdint.h>
#include <stdio.h>       /* snprintf */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#define YOS_WIN_DEV_NULL   "YOS$DEV$NULL"
#define YOS_WIN_DEV_ZERO   "YOS$DEV$ZERO"
#define YOS_WIN_DEV_RANDOM "YOS$DEV$RANDOM"

/* Host-side errno value yos_plat_write sets when _write returns EPIPE
 * on a pipe handle. See the long comment in yos_plat_write below — the
 * direct host EPIPE=32 collides in the codegen's flat constants_remap
 * with another constant of the same numeric value, so we use a value
 * that happens to remap unambiguously to guest-EPIPE=32. The startup
 * self-check yos_init_epipe_sentinel() asserts the remap still works
 * the way we think it does. */
#define YOS_WIN_HOST_EPIPE_SENTINEL 1024

#define YOS_WIN_FDKIND_MAX 65536
enum {
    YOS_WIN_FD_REGULAR = 0,
    YOS_WIN_FD_NULL,
    YOS_WIN_FD_ZERO,
    YOS_WIN_FD_RANDOM,
};
static volatile long g_fdkind[YOS_WIN_FDKIND_MAX];

int yos_fdkind_get(int fd)
{
    /* Out-of-range fd: treat as REGULAR. Aliasing into slot 0 would
     * corrupt tracking for fd 0 (stdin). msvcrt can't usually hand
     * out fds beyond _getmaxstdio() ≤ 8192 but a Winsock SOCKET cast
     * to int could be arbitrarily large. */
    if (fd < 0 || fd >= YOS_WIN_FDKIND_MAX) return YOS_WIN_FD_REGULAR;
    return (int)g_fdkind[fd];
}

void yos_fdkind_set(int fd, int kind)
{
    if (fd < 0 || fd >= YOS_WIN_FDKIND_MAX) return;
    g_fdkind[fd] = kind;
}

typedef LONG (WINAPI *yos_BCryptGenRandom_t)(void *, void *, ULONG, ULONG);
static yos_BCryptGenRandom_t g_yos_bcrypt_fn;
static INIT_ONCE             g_yos_bcrypt_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK yos_init_bcrypt(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    /* bcrypt.dll is a permanent part of the Win32 surface; we resolve
     * BCryptGenRandom once and keep the module loaded for the rest of
     * the process lifetime. No FreeLibrary on a per-read basis. */
    HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
    if (bcrypt)
        g_yos_bcrypt_fn = (yos_BCryptGenRandom_t)(uintptr_t)
                          GetProcAddress(bcrypt, "BCryptGenRandom");
    return TRUE;
}

static int yos_win_random(void *buf, size_t n)
{
    InitOnceExecuteOnce(&g_yos_bcrypt_once, yos_init_bcrypt, NULL, NULL);
    yos_BCryptGenRandom_t bg = g_yos_bcrypt_fn;
    if (!bg) { errno = EIO; return -1; }
    unsigned char *p = (unsigned char *)buf;
    while (n > 0) {
        ULONG chunk = n > 0x40000000u ? 0x40000000u : (ULONG)n;
        if (bg(NULL, p, chunk, 2) != 0) { errno = EIO; return -1; }
        p += chunk;
        n -= chunk;
    }
    return 0;
}

pid_t yos_plat_gettid(void)
{
    return (pid_t)GetCurrentThreadId();
}

#define YOS_FADV_NORMAL      0
#define YOS_FADV_RANDOM      1
#define YOS_FADV_SEQUENTIAL  2
#define YOS_FADV_WILLNEED    3
#define YOS_FADV_DONTNEED    4
#define YOS_FADV_NOREUSE     5

int yos_plat_posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
    (void)fd; (void)offset; (void)len;
    switch (advice) {
    case YOS_FADV_NORMAL:
    case YOS_FADV_RANDOM:
    case YOS_FADV_SEQUENTIAL:
    case YOS_FADV_WILLNEED:
    case YOS_FADV_DONTNEED:
    case YOS_FADV_NOREUSE:
        return 0;
    default:
        return EINVAL;
    }
}

int yos_plat_posix_fallocate(int fd, off_t offset, off_t len)
{
    if (offset < 0 || len <= 0) return EINVAL;

    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return EBADF;

    LARGE_INTEGER cur_size;
    if (!GetFileSizeEx(h, &cur_size)) return EIO;

    int64_t need_end = (int64_t)offset + (int64_t)len;
    if (need_end <= cur_size.QuadPart) return 0;

    /* Remember current position so we can restore it after the zero-fill. */
    LARGE_INTEGER zero = {0};
    LARGE_INTEGER saved_pos;
    if (!SetFilePointerEx(h, zero, &saved_pos, FILE_CURRENT)) return EIO;

    /* Seek to old EOF and write zeros up to need_end so the new range
     * is logically allocated and zero-filled per POSIX. */
    LARGE_INTEGER seek;
    seek.QuadPart = cur_size.QuadPart;
    if (!SetFilePointerEx(h, seek, NULL, FILE_BEGIN)) return EIO;

    unsigned char zeros[65536];
    memset(zeros, 0, sizeof zeros);
    int64_t left = need_end - cur_size.QuadPart;
    while (left > 0) {
        DWORD chunk = (left > (int64_t)sizeof zeros)
                          ? (DWORD)sizeof zeros : (DWORD)left;
        DWORD wrote = 0;
        if (!WriteFile(h, zeros, chunk, &wrote, NULL) || wrote != chunk) {
            int e = (GetLastError() == ERROR_DISK_FULL) ? ENOSPC : EIO;
            SetFilePointerEx(h, saved_pos, NULL, FILE_BEGIN);
            return e;
        }
        left -= chunk;
    }

    SetFilePointerEx(h, saved_pos, NULL, FILE_BEGIN);
    return 0;
}

void yos_plat_exit(int code)
{
    /* ExitProcess is Win32's terminate-and-go: no atexit, no destructor
     * cascade, no DllMain on threads — just emit the exit code and
     * release the process record. Avoids the debug-CRT hang we hit
     * during FLS-callback / worker-thread teardown. */
    ExitProcess((UINT)code);
}

/* Cheap socket-vs-CRT-fd classifier. getsockopt(SO_TYPE) succeeds only
 * on a real Winsock SOCKET handle; on a CRT-managed file descriptor it
 * returns WSAENOTSOCK. */
static int yos_is_socket_fd(int hfd)
{
    int t = 0; int len = (int)sizeof t;
    return getsockopt((SOCKET)hfd, SOL_SOCKET, SO_TYPE,
                      (char *)&t, &len) == 0;
}

/* Map a Winsock error to a POSIX errno value usable with the codegen's
 * h2g remap. Only the codes that actually surface in our bridge layer
 * are listed; everything else falls back to EIO so the call still
 * reports a failure but doesn't lie about the kind. */
int yos_wsa_to_errno(int wsa)
{
    switch (wsa) {
    case WSAEWOULDBLOCK:  return EAGAIN;       /* same value POSIX uses */
    case WSAEINTR:        return EINTR;
    case WSAEBADF:        return EBADF;
    case WSAEACCES:       return EACCES;
    case WSAEFAULT:       return EFAULT;
    case WSAEINVAL:       return EINVAL;
    case WSAEMFILE:       return EMFILE;
    case WSAENOTSOCK:     return ENOTSOCK;
    case WSAEMSGSIZE:     return EMSGSIZE;
    case WSAEADDRINUSE:   return EADDRINUSE;
    case WSAEADDRNOTAVAIL:return EADDRNOTAVAIL;
    case WSAENETDOWN:     return ENETDOWN;
    case WSAENETUNREACH:  return ENETUNREACH;
    case WSAENETRESET:    return ENETRESET;
    case WSAECONNABORTED: return ECONNABORTED;
    case WSAECONNRESET:   return ECONNRESET;
    case WSAENOBUFS:      return ENOBUFS;
    case WSAEISCONN:      return EISCONN;
    case WSAENOTCONN:     return ENOTCONN;
    case WSAESHUTDOWN:    return EPIPE;
    case WSAETIMEDOUT:    return ETIMEDOUT;
    case WSAECONNREFUSED: return ECONNREFUSED;
    case WSAEHOSTUNREACH: return EHOSTUNREACH;
    case WSAEINPROGRESS:  return EINPROGRESS;
    case WSAEALREADY:     return EALREADY;
    case WSAEAFNOSUPPORT: return EAFNOSUPPORT;
    case WSAEPROTONOSUPPORT: return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:   return EOPNOTSUPP;
    case WSAEPROTOTYPE:   return EPROTOTYPE;
    case WSANOTINITIALISED:
    case WSAENOTEMPTY:
    default:              return EIO;
    }
}

int yos_plat_close(int hfd)
{
    yos_fdkind_set(hfd, YOS_WIN_FD_REGULAR);
    if (yos_is_socket_fd(hfd)) {
        if (closesocket((SOCKET)hfd) == 0) return 0;
        errno = yos_wsa_to_errno(WSAGetLastError());
        return -1;
    }
    return _close(hfd);
}

/* Cap a single recv/send/_read/_write call to a value that fits in the
 * underlying API's `int` / `unsigned` count parameter. Larger guest
 * requests get chunked at the caller (impl/io/io.c); from yos_plat_*
 * the caller only sees as many bytes as the host actually moved, which
 * is also the POSIX contract (short read/write is allowed). */
#define YOS_WIN_IO_CHUNK ((size_t)0x40000000)  /* 1 GiB — safely < INT_MAX */

ssize_t yos_plat_read(int hfd, void *buf, size_t n)
{
    int kind = yos_fdkind_get(hfd);
    if (kind == YOS_WIN_FD_ZERO) {
        memset(buf, 0, n);
        return (ssize_t)n;
    }
    if (kind == YOS_WIN_FD_RANDOM) {
        return yos_win_random(buf, n) == 0 ? (ssize_t)n : -1;
    }
    size_t chunk = n > YOS_WIN_IO_CHUNK ? YOS_WIN_IO_CHUNK : n;
    if (yos_is_socket_fd(hfd)) {
        int r = recv((SOCKET)hfd, (char *)buf, (int)chunk, 0);
        if (r == SOCKET_ERROR) {
            errno = yos_wsa_to_errno(WSAGetLastError());
            return -1;
        }
        return (ssize_t)r;
    }
    return (ssize_t)_read(hfd, buf, (unsigned)chunk);
}

ssize_t yos_plat_write(int hfd, const void *buf, size_t n)
{
    int kind = yos_fdkind_get(hfd);
    if (kind == YOS_WIN_FD_NULL || kind == YOS_WIN_FD_ZERO ||
        kind == YOS_WIN_FD_RANDOM) {
        (void)buf;
        return (ssize_t)n;
    }
    size_t chunk = n > YOS_WIN_IO_CHUNK ? YOS_WIN_IO_CHUNK : n;
    if (yos_is_socket_fd(hfd)) {
        int r = send((SOCKET)hfd, (const char *)buf, (int)chunk, 0);
        if (r == SOCKET_ERROR) {
            int wsa = WSAGetLastError();
            /* SHUTDOWN → guest EPIPE; reuse the same sentinel the pipe
             * path uses below so the h2g remap lands on guest EPIPE=32. */
            errno = (wsa == WSAESHUTDOWN || wsa == WSAECONNABORTED ||
                     wsa == WSAECONNRESET) ? YOS_WIN_HOST_EPIPE_SENTINEL
                                           : yos_wsa_to_errno(wsa);
            return -1;
        }
        return (ssize_t)r;
    }
    int r = _write(hfd, buf, (unsigned)chunk);
    if (r < 0) {
        HANDLE h = (HANDLE)_get_osfhandle(hfd);
        if (h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_PIPE) {
            /* Broken-pipe sentinel for the Windows host-errno → guest-
             * errno remap path. The codegen merges every numeric constant
             * into one big remap table, so MSVC's host errno=32 (EPIPE)
             * collides with ECHOK (also 32 on FreeBSD-side numerically
             * different) and the table picks ECHOK → guest 4. MSVC's
             * ECHOPRT value happens to remap unambiguously to guest
             * EPIPE=32, so we use that as the sentinel — see
             * yos_init_epipe_sentinel() for a startup-time check that the
             * remap still produces guest-EPIPE. */
            errno = YOS_WIN_HOST_EPIPE_SENTINEL;
        }
    }
    return (ssize_t)r;
}

int yos_plat_isatty(int hfd)
{
    if (yos_is_socket_fd(hfd)) return 0;
    return _isatty(hfd);
}

int yos_plat_access(const char *path, int mode)
{
    /* POSIX X_OK (1) isn't accepted by msvcrt's _access — strip it.
     * F_OK (0), R_OK (4), W_OK (2) all pass through cleanly. */
    return _access(path, mode & 0x6);
}

/* Forward decls for the per-fd flag table maintained in compat_libc.c. */
extern void yos_fdflags_record_open(int fd, int flags);
extern void yos_fdmode_record(int fd, int mode);
extern int  yos_fdmode_get(int fd);

int yos_plat_open(const char *path, int flags, int mode)
{
    int kind = YOS_WIN_FD_REGULAR;
    const char *open_path = path;
    if (strcmp(path, YOS_WIN_DEV_NULL) == 0) {
        kind = YOS_WIN_FD_NULL;
        open_path = "NUL";
    } else if (strcmp(path, YOS_WIN_DEV_ZERO) == 0) {
        kind = YOS_WIN_FD_ZERO;
        open_path = "NUL";
    } else if (strcmp(path, YOS_WIN_DEV_RANDOM) == 0) {
        kind = YOS_WIN_FD_RANDOM;
        open_path = "NUL";
    }

    /* Force binary mode unless the caller explicitly asked for text:
     * the wasm guest expects POSIX byte-for-byte read/write semantics,
     * and msvcrt's default text mode translates \n → \r\n on write
     * (and the reverse on read), inflating byte counts and breaking
     * every fstat / lseek / read-loop the guest does on a file it
     * just wrote. */
    if (!(flags & (_O_BINARY | _O_TEXT))) flags |= _O_BINARY;

    /* O_DIRECTORY: msvcrt's open(2) does not accept directory paths —
     * it returns EACCES (or EINVAL) and fails. The wasm guest, however,
     * uses open(dir, O_RDONLY|O_DIRECTORY) as the standard way to mint
     * a dirfd for mkdirat/openat/fstatat. We satisfy that by opening
     * the directory via CreateFileA with FILE_FLAG_BACKUP_SEMANTICS
     * (the documented way to obtain a directory handle on Win32), then
     * wrapping the HANDLE in a CRT fd via _open_osfhandle so the rest
     * of the io path (yos_plat_close, fstat, etc.) sees a normal int
     * file descriptor. */
    if (flags & 0x10000 /* O_DIRECTORY */) {
        HANDLE h = CreateFileA(open_path,
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS,
                               NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            errno = (err == ERROR_FILE_NOT_FOUND ||
                     err == ERROR_PATH_NOT_FOUND) ? ENOENT
                  : (err == ERROR_ACCESS_DENIED)  ? EACCES
                  :                                 EIO;
            return -1;
        }
        int fd = _open_osfhandle((intptr_t)h, _O_BINARY | _O_RDONLY);
        if (fd < 0) { CloseHandle(h); errno = EMFILE; return -1; }
        yos_fdkind_set(fd, kind);
        yos_fdflags_record_open(fd, flags);
        return fd;
    }

    int fd = open(open_path, flags, mode);
    if (fd >= 0) {
        yos_fdkind_set(fd, kind);
        yos_fdflags_record_open(fd, flags);
        /* Stamp the POSIX mode for files we created — yos_plat_fstat
         * splices it back into st_mode so umask-then-fstat round-trips
         * across the Windows filesystem (which only persists the
         * readonly attribute). */
        if ((flags & 0x100 /* _O_CREAT */) != 0 && mode != 0) {
            yos_fdmode_record(fd, mode);
        }
    }
    return fd;
}

const char *yos_plat_translate_path(const char *path)
{
    /* Per-thread scratch so concurrent forks don't clobber each other.
     * 4 KiB covers any reasonable %TEMP% concatenation. */
    static __declspec(thread) char buf[4096];
    if (!path) return NULL;

    /* Exact-match POSIX device names → Windows null/console devices. */
    if (strcmp(path, "/dev/null") == 0)         return YOS_WIN_DEV_NULL;
    if (strcmp(path, "/dev/zero") == 0)         return YOS_WIN_DEV_ZERO;
    if (strcmp(path, "/dev/random") == 0)       return YOS_WIN_DEV_RANDOM;
    if (strcmp(path, "/dev/urandom") == 0)      return YOS_WIN_DEV_RANDOM;
    if (strcmp(path, "/dev/tty") == 0)          return "CON";
    if (strcmp(path, "/dev/stdin") == 0)        return "CONIN$";
    if (strcmp(path, "/dev/stdout") == 0)       return "CONOUT$";
    if (strcmp(path, "/dev/stderr") == 0)       return "CONOUT$";

    /* /tmp/<x> and /tmp on their own → %TEMP%\<x>. */
    if (path[0] == '/' && path[1] == 't' && path[2] == 'm' && path[3] == 'p' &&
        (path[4] == 0 || path[4] == '/')) {
        const char *tmpdir = getenv("TEMP");
        if (!tmpdir) tmpdir = getenv("TMP");
        if (!tmpdir) tmpdir = "C:\\Windows\\Temp";
        snprintf(buf, sizeof buf, "%s%s", tmpdir, path + 4);
        return buf;
    }

    /* "/" alone → %SystemDrive%\ (typically C:\). Anchors guest's
     * absolute paths to the system drive so opendir("/"), stat("/"),
     * etc. resolve against a real directory. */
    if (path[0] == '/' && path[1] == 0) {
        const char *sd = getenv("SystemDrive");
        if (!sd) sd = "C:";
        snprintf(buf, sizeof buf, "%s\\", sd);
        return buf;
    }

    return path;
}

int yos_plat_fstat(int hfd, struct stat *out)
{
    if (!out) { errno = EFAULT; return -1; }

    /* yos_fd_alloc on Windows packs both CRT file descriptors and
     * Winsock SOCKETs into the host_fd table. SOCKETs are HANDLE-ish
     * kernel objects (just integers); _fstat64i32 on them is undefined.
     * Detect via getsockopt(SO_TYPE) — succeeds only on a valid socket
     * — and synthesise an S_IFSOCK stat. Otherwise fall through to the
     * CRT's fstat. */
    int sock_type = 0;
    int len = (int)sizeof sock_type;
    if (getsockopt((SOCKET)hfd, SOL_SOCKET, SO_TYPE,
                   (char *)&sock_type, &len) == 0) {
        memset(out, 0, sizeof *out);
        out->st_mode = _S_IFIFO | 0666;  /* slot present so caller knows */
        /* S_IFSOCK isn't in MSVC's <sys/stat.h>; we defined the value
         * 0140000 in posix_extras.h. Apply it directly to st_mode here
         * so S_ISSOCK(st_mode) on the wasm side reads as true. */
        out->st_mode = (unsigned short)((out->st_mode & ~0170000u) | 0140000u);
        return 0;
    }

    /* yos's emulated character devices (the open-time path translation
     * mapped /dev/null, /dev/zero, /dev/random to a "NUL" CRT fd with
     * a recorded fdkind). msvcrt's _fstat on NUL may report _S_IFCHR
     * or _S_IFREG depending on UCRT version; cover the contract here
     * by stamping S_IFCHR explicitly so yos_poll's "always-ready"
     * synthesis picks them up regardless. */
    int kind = yos_fdkind_get(hfd);
    if (kind == YOS_WIN_FD_NULL || kind == YOS_WIN_FD_ZERO ||
        kind == YOS_WIN_FD_RANDOM) {
        memset(out, 0, sizeof *out);
        out->st_mode = (unsigned short)(_S_IFCHR | 0666);
        out->st_nlink = 1;
        return 0;
    }

    int rc = _fstat64i32(hfd, (struct _stat64i32 *)out);
    if (rc == 0) {
        /* Splice the recorded POSIX mode (umask-masked open mode) into
         * the permission bits — Windows persists only the read-only
         * attribute, so a fstat of a freshly-created file otherwise
         * always reads back 0666. */
        int recorded = yos_fdmode_get(hfd);
        if (recorded != 0) {
            out->st_mode = (unsigned short)((out->st_mode & ~0777u) |
                                            (recorded & 0777u));
        }
    }
    return rc;
}
