#ifndef YOS_PROCFS_H
#define YOS_PROCFS_H

#include <stdint.h>
#include <stddef.h>

struct yos_exec_ctx;

/* Procfs file types */
enum yos_procfs_file_type {
    YOS_PROCFS_ROOT_DIR = 0,     /* /proc directory */
    YOS_PROCFS_PID_DIR,          /* /proc/[pid] directory */
    YOS_PROCFS_STAT,             /* /proc/[pid]/stat */
    YOS_PROCFS_CMDLINE,          /* /proc/[pid]/cmdline */
    YOS_PROCFS_STATUS,           /* /proc/[pid]/status */
    YOS_PROCFS_COMM,             /* /proc/[pid]/comm */
    YOS_PROCFS_SELF,             /* /proc/self symlink */
};

/* Virtual file descriptor for procfs - stored in fd table */
#define YOS_VIRTUAL_FD_BASE 1000

struct yos_procfs_fd {
    int in_use;
    enum yos_procfs_file_type type;
    int32_t target_pid;           /* for /proc/[pid]/* files */
    size_t read_position;
    char *content;                /* generated content */
    size_t content_length;
    int dir_position;             /* for directory iteration */
};

#define YOS_MAX_PROCFS_FDS 64

/* Check if path is under /proc */
int yos_procfs_is_proc_path(const char *path);

/* Procfs syscall handlers - return negative errno on error */
int32_t yos_procfs_open(struct yos_exec_ctx *ctx, const char *path, int32_t flags);
int32_t yos_procfs_read(struct yos_exec_ctx *ctx, int32_t fd, void *buf, size_t count);
int32_t yos_procfs_close(struct yos_exec_ctx *ctx, int32_t fd);
int32_t yos_procfs_stat(struct yos_exec_ctx *ctx, const char *path, void *statbuf);
int32_t yos_procfs_getdents64(struct yos_exec_ctx *ctx, int32_t fd, void *dirp, size_t count);
int32_t yos_procfs_readlink(struct yos_exec_ctx *ctx, const char *path, char *buf, size_t bufsiz);

/* Check if fd is a procfs virtual fd */
int yos_procfs_is_virtual_fd(int32_t fd);

#endif /* YOS_PROCFS_H */
