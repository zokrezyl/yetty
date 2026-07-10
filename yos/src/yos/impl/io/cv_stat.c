/* impl/io/cv_stat.c — FreeBSD-i386 struct stat fill, used by every
 * yos bridge that converts a host stat into the wasm guest's buffer.
 *
 * Why hand-written: the auto-generated cv_stat_h2w in
 * src/yos/generated/struct_convert.c is wrong on darwin — it was
 * derived from a stale wasm32_stat snapshot (st_mode at the wrong
 * offset, no time fields) and breaks uv_guess_handle / ls / fts on
 * every macOS+iOS+tvOS build. Replacing the codegen output requires
 * hooks.yaml + struct-snapshot work; until then this file is the
 * authoritative writer and both posix.c::yos_fstat and
 * fifo.c::yos_stat/lstat/fstatat call yos_cv_stat_fbi() instead of
 * cv_stat_h2w().
 *
 * Offsets verified via tools/struct-offsets.py stat sys/stat.h —
 * re-run if the FreeBSD sysroot is bumped. sizeof(struct stat) is
 * 208 on FreeBSD-i386 wasm32 ABI; the auto-bridge generator only
 * emits 192-byte copies (it stops before st_gen/st_filerev), and
 * we match that for now — anything past 192 is left zero by the
 * caller's bounds check.
 */

#include "yos/types.h"
#include "impl/io/cv_stat.h"
#include "platform.h"

#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* FreeBSD-i386 struct stat field offsets. Total: 208 bytes; we write
 * up to 192 to stay compatible with the auto-bridge's emit size. */
#define FBI_ST_OFF_DEV         0   /* dev_t,   8B */
#define FBI_ST_OFF_INO         8   /* ino_t,   8B */
#define FBI_ST_OFF_NLINK      16   /* nlink_t, 8B */
#define FBI_ST_OFF_MODE       24   /* mode_t,  2B */
#define FBI_ST_OFF_BSDFLAGS   26   /* int16,   2B */
#define FBI_ST_OFF_UID        28   /* uid_t,   4B */
#define FBI_ST_OFF_GID        32   /* gid_t,   4B */
/* st_padding1 @36 — 4B */
#define FBI_ST_OFF_RDEV       40   /* dev_t,   8B */
/* timespec fields: each is { time_t sec; long nsec; } = 8B on i386. */
/* st_atim_ext  @48 (int32) — high bits of sec for year-2038+; zero */
#define FBI_ST_OFF_ATIM_SEC   52
#define FBI_ST_OFF_ATIM_NSEC  56
/* st_mtim_ext  @60 */
#define FBI_ST_OFF_MTIM_SEC   64
#define FBI_ST_OFF_MTIM_NSEC  68
/* st_ctim_ext  @72 */
#define FBI_ST_OFF_CTIM_SEC   76
#define FBI_ST_OFF_CTIM_NSEC  80
/* st_btim_ext  @84 */
#define FBI_ST_OFF_BIRTH_SEC  88
#define FBI_ST_OFF_BIRTH_NSEC 92
#define FBI_ST_OFF_SIZE       96   /* off_t,    8B */
#define FBI_ST_OFF_BLOCKS    104   /* blkcnt_t, 8B */
#define FBI_ST_OFF_BLKSIZE   112   /* blksize_t,4B */
#define FBI_ST_OFF_FLAGS     116   /* fflags_t, 4B */
#define FBI_ST_OFF_GEN       120   /* uint64,   8B */
/* st_filerev @128 — 8B */

/* darwin and Linux disagree on the timespec field name inside struct
 * stat: darwin = st_atimespec/st_mtimespec/st_ctimespec/st_birthtimespec,
 * Linux glibc = st_atim/st_mtim/st_ctim (no birthtime).
 *
 * Hide that here so callers stay clean. The slice files below are the
 * ONLY platform divergence allowed in this TU. */
#if defined(__APPLE__) || defined(__FreeBSD__)
#  define H_ATIM(h)      ((h)->st_atimespec)
#  define H_MTIM(h)      ((h)->st_mtimespec)
#  define H_CTIM(h)      ((h)->st_ctimespec)
#  define H_HAS_BIRTH    1
#  define H_BIRTH(h)     ((h)->st_birthtimespec)
#elif defined(_MSC_VER)
/* MSVC's <sys/stat.h> exposes st_atime/st_mtime/st_ctime as flat
 * time_t (no sub-second component). Synthesise a struct-timespec
 * value via these helper expressions; nsec is always 0. */
struct yos_win_ts { long long tv_sec; long tv_nsec; };
#  define H_ATIM(h)      ((struct yos_win_ts){ (long long)(h)->st_atime, 0 })
#  define H_MTIM(h)      ((struct yos_win_ts){ (long long)(h)->st_mtime, 0 })
#  define H_CTIM(h)      ((struct yos_win_ts){ (long long)(h)->st_ctime, 0 })
#  define H_HAS_BIRTH    0
#else
#  define H_ATIM(h)      ((h)->st_atim)
#  define H_MTIM(h)      ((h)->st_mtim)
#  define H_CTIM(h)      ((h)->st_ctim)
#  define H_HAS_BIRTH    0
#endif

void yos_cv_stat_fbi(uint8_t *w, const struct stat *h)
{
    memset(w, 0, 192);
    *(int64_t  *)(w + FBI_ST_OFF_DEV)   = (int64_t)h->st_dev;
    *(int64_t  *)(w + FBI_ST_OFF_INO)   = (int64_t)h->st_ino;
    *(int64_t  *)(w + FBI_ST_OFF_NLINK) = (int64_t)h->st_nlink;
    {
        /* FreeBSD/Linux guarantee symlinks read back with permissions
         * 0777; darwin's lstat returns the symlink's actual perm bits
         * (often 0755). Normalise so guest code (ls's strmode, find -
         * perm) sees the BSD-shape contract. */
        mode_t m = h->st_mode;
        if (S_ISLNK(m)) m = (m & ~(mode_t)0777) | 0777;
        *(int16_t  *)(w + FBI_ST_OFF_MODE) = (int16_t)m;
    }
    /* st_bsdflags @26 — host has no equivalent; leave 0. */
    *(int32_t  *)(w + FBI_ST_OFF_UID)   = (int32_t)h->st_uid;
    *(int32_t  *)(w + FBI_ST_OFF_GID)   = (int32_t)h->st_gid;
    *(int64_t  *)(w + FBI_ST_OFF_RDEV)  = (int64_t)h->st_rdev;

    *(int32_t  *)(w + FBI_ST_OFF_ATIM_SEC)  = (int32_t)H_ATIM(h).tv_sec;
    *(int32_t  *)(w + FBI_ST_OFF_ATIM_NSEC) = (int32_t)H_ATIM(h).tv_nsec;
    *(int32_t  *)(w + FBI_ST_OFF_MTIM_SEC)  = (int32_t)H_MTIM(h).tv_sec;
    *(int32_t  *)(w + FBI_ST_OFF_MTIM_NSEC) = (int32_t)H_MTIM(h).tv_nsec;
    *(int32_t  *)(w + FBI_ST_OFF_CTIM_SEC)  = (int32_t)H_CTIM(h).tv_sec;
    *(int32_t  *)(w + FBI_ST_OFF_CTIM_NSEC) = (int32_t)H_CTIM(h).tv_nsec;
#if H_HAS_BIRTH
    *(int32_t  *)(w + FBI_ST_OFF_BIRTH_SEC)  = (int32_t)H_BIRTH(h).tv_sec;
    *(int32_t  *)(w + FBI_ST_OFF_BIRTH_NSEC) = (int32_t)H_BIRTH(h).tv_nsec;
#endif

    *(int64_t  *)(w + FBI_ST_OFF_SIZE)    = (int64_t)h->st_size;
    *(int64_t  *)(w + FBI_ST_OFF_BLOCKS)  = (int64_t)yos_plat_stat_blocks(h);
    *(int32_t  *)(w + FBI_ST_OFF_BLKSIZE) = (int32_t)yos_plat_stat_blksize(h);
#if defined(__APPLE__) || defined(__FreeBSD__)
    /* darwin/BSD have st_flags. Linux glibc doesn't — leave 0 there. */
    *(int32_t  *)(w + FBI_ST_OFF_FLAGS)   = (int32_t)h->st_flags;
    *(int64_t  *)(w + FBI_ST_OFF_GEN)     = (int64_t)h->st_gen;
#endif
}
