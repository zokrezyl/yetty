#ifndef YOS_VFS_FILE_H
#define YOS_VFS_FILE_H

#include <stdint.h>
#include <stddef.h>

struct yos_file_operations;
struct yos_exec_ctx;

/*
 * Open file - tracks state for an open virtual file.
 */
struct yos_file {
    int in_use;
    const struct yos_file_operations *ops;

    /* Read position */
    size_t position;

    /* For directory iteration */
    int dir_index;

    /* Filesystem-specific data */
    int32_t procfs_pid;         /* for /proc/[pid] files */
    int procfs_file_type;       /* which file in /proc/[pid] */
    char *content;              /* dynamically generated content */
    size_t content_length;
};

/*
 * Virtual file descriptor table.
 * Virtual fds start at YOS_VFS_FD_BASE to avoid collision with host fds.
 */
#define YOS_VFS_FD_BASE 1000
#define YOS_MAX_VFS_FILES 256

struct yos_file_table {
    struct yos_file files[YOS_MAX_VFS_FILES];
};

/* Initialize file table */
void yos_file_table_init(struct yos_file_table *table);

/* Allocate a file. Returns fd (>= YOS_VFS_FD_BASE) or -errno. */
int32_t yos_file_alloc(struct yos_file_table *table);

/* Free a file by fd */
void yos_file_free(struct yos_file_table *table, int32_t fd);

/* Get file by fd. Returns NULL if invalid. */
struct yos_file *yos_file_get(struct yos_file_table *table, int32_t fd);

/* Check if fd is a virtual fd */
static inline int yos_is_virtual_fd(int32_t fd)
{
    return fd >= YOS_VFS_FD_BASE;
}

#endif /* YOS_VFS_FILE_H */
