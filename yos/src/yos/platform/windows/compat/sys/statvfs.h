/* Minimal <sys/statvfs.h> compat. Windows has no statvfs; expose the
 * struct shape so generated code that #includes the header compiles.
 * Actual statvfs(3) calls land in the Windows io-windows.c ENOSYS
 * path. */
#ifndef YOS_WIN_COMPAT_SYS_STATVFS_H
#define YOS_WIN_COMPAT_SYS_STATVFS_H

#include <sys/types.h>
#include <stdint.h>

typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    fsblkcnt_t    f_blocks;
    fsblkcnt_t    f_bfree;
    fsblkcnt_t    f_bavail;
    fsfilcnt_t    f_files;
    fsfilcnt_t    f_ffree;
    fsfilcnt_t    f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

#endif
