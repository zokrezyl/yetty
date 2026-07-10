#ifndef YOS_VFS_MOUNT_H
#define YOS_VFS_MOUNT_H

#include <stdint.h>
#include <stddef.h>

struct yos_exec_ctx;
struct yos_file;

/*
 * File operations - vtable for filesystem operations.
 * Each filesystem (procfs, hostfs, etc.) implements this.
 */
struct yos_file_operations {
    int32_t (*open)(struct yos_exec_ctx *ctx, const char *path, int32_t flags, int32_t mode);
    int32_t (*read)(struct yos_exec_ctx *ctx, struct yos_file *file, void *buf, size_t count);
    int32_t (*close)(struct yos_exec_ctx *ctx, struct yos_file *file);
    int32_t (*stat)(struct yos_exec_ctx *ctx, const char *path, void *statbuf);
    int32_t (*getdents64)(struct yos_exec_ctx *ctx, struct yos_file *file, void *dirp, size_t count);
    int32_t (*readlink)(struct yos_exec_ctx *ctx, const char *path, char *buf, size_t bufsiz);
};

/*
 * Mount entry - maps an EXACT path to a filesystem.
 */
#define YOS_MOUNT_PATH_MAX 256
#define YOS_MAX_MOUNTS 16

struct yos_mount {
    char path[YOS_MOUNT_PATH_MAX];
    size_t path_len;
    const struct yos_file_operations *ops;
};

/*
 * Mount table - stored in yos_runtime, shared across processes.
 */
struct yos_mount_table {
    struct yos_mount mounts[YOS_MAX_MOUNTS];
    int count;
};

/* Initialize mount table */
void yos_mount_table_init(struct yos_mount_table *table);

/* Add a mount. Returns 0 on success, -errno on error. */
int yos_mount_add(struct yos_mount_table *table, const char *path,
                  const struct yos_file_operations *ops);

/*
 * Resolve path to filesystem.
 * Walks path component by component, checking for mount points.
 * Returns ops and sets *remaining to path after mount point.
 * Returns NULL if no mount matches (use host filesystem).
 */
const struct yos_file_operations *yos_mount_resolve(
    struct yos_mount_table *table,
    const char *path,
    const char **remaining);

#endif /* YOS_VFS_MOUNT_H */
