/* Minimal <sys/mount.h> compat. yos's generated struct converter
 * pulls this in for statfs; we provide a stub struct so it compiles.
 * Functional statfs/statvfs is ENOSYS in io-windows.c. */
#ifndef YOS_WIN_COMPAT_SYS_MOUNT_H
#define YOS_WIN_COMPAT_SYS_MOUNT_H

#include <sys/types.h>

struct statfs {
    unsigned long f_type;
    unsigned long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_fsid;
    unsigned long f_namelen;
    unsigned long f_frsize;
    unsigned long f_flags;
    unsigned long f_spare[4];
};

#endif
