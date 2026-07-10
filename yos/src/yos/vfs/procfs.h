#ifndef YOS_VFS_PROCFS_H
#define YOS_VFS_PROCFS_H

#include "mount.h"

/* Procfs file types */
enum yos_procfs_type {
    YOS_PROCFS_ROOT = 0,        /* /proc directory */
    YOS_PROCFS_SELF,            /* /proc/self symlink */
    YOS_PROCFS_PID_DIR,         /* /proc/[pid] directory */
    YOS_PROCFS_STAT,            /* /proc/[pid]/stat */
    YOS_PROCFS_CMDLINE,         /* /proc/[pid]/cmdline */
    YOS_PROCFS_STATUS,          /* /proc/[pid]/status */
    YOS_PROCFS_COMM,            /* /proc/[pid]/comm */
    YOS_PROCFS_EXE,             /* /proc/[pid]/exe symlink */
    YOS_PROCFS_CWD,             /* /proc/[pid]/cwd symlink */
    YOS_PROCFS_MOUNTS,          /* /proc/mounts */
    YOS_PROCFS_MEMINFO,         /* /proc/meminfo */
    YOS_PROCFS_VERSION,         /* /proc/version */
    YOS_PROCFS_UPTIME,          /* /proc/uptime */
};

/* Procfs file operations */
extern const struct yos_file_operations yos_procfs_ops;

#endif /* YOS_VFS_PROCFS_H */
