/* <sys/statfs.h> compat — Linux-only header. Defines the same struct
 * shape as <sys/vfs.h>. yos's statfs bridge returns ENOSYS on Windows
 * via io-windows.c, but the generated converter still needs the
 * struct definition to parse. */
#ifndef YOS_WIN_COMPAT_SYS_STATFS_H
#define YOS_WIN_COMPAT_SYS_STATFS_H

#include <sys/mount.h>    /* struct statfs lives there already in our shim */

#endif
