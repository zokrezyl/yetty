#define _GNU_SOURCE
#include "procfs.h"
#include "file.h"
#include "../types.h"
#include <yos/ytrace/ytrace.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

/*
 * Kernel ABI constants from linux/stat.h and linux/fs_dirent.h
 * These are stable ABI values that will never change.
 */

/* stat mode bits (from linux/stat.h) */
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IFLNK  0120000

/* directory entry types (from linux/fs_dirent.h) */
#define DT_DIR   4
#define DT_REG   8
#define DT_LNK   10

/* Forward declarations for process table access */
extern struct yos_proc *yos_proc_find(struct yos_runtime *rt, int32_t pid);

/*
 * Parse procfs path.
 * Returns file type and sets *pid if path is /proc/[pid]/...
 *
 * Examples:
 *   ""           -> YOS_PROCFS_ROOT (directory listing)
 *   "self"       -> YOS_PROCFS_SELF (symlink)
 *   "123"        -> YOS_PROCFS_PID_DIR (directory)
 *   "123/stat"   -> YOS_PROCFS_STAT
 *   "123/cmdline"-> YOS_PROCFS_CMDLINE
 */
static int parse_procfs_path(const char *path, int32_t *pid)
{
    *pid = 0;

    /* Empty or root */
    if (!path || !*path)
        return YOS_PROCFS_ROOT;

    /* /proc/self */
    if (strcmp(path, "self") == 0)
        return YOS_PROCFS_SELF;

    /* Root-level files */
    if (strcmp(path, "mounts") == 0)
        return YOS_PROCFS_MOUNTS;
    if (strcmp(path, "meminfo") == 0)
        return YOS_PROCFS_MEMINFO;
    if (strcmp(path, "version") == 0)
        return YOS_PROCFS_VERSION;
    if (strcmp(path, "uptime") == 0)
        return YOS_PROCFS_UPTIME;

    /* Parse pid */
    char *endptr;
    long parsed_pid = strtol(path, &endptr, 10);
    if (endptr == path)
        return -ENOENT;  /* Not a number */

    *pid = (int32_t)parsed_pid;

    /* Just /proc/[pid] */
    if (*endptr == '\0')
        return YOS_PROCFS_PID_DIR;

    /* /proc/[pid]/something */
    if (*endptr != '/')
        return -ENOENT;

    const char *file = endptr + 1;
    /* Handle trailing slash: /proc/[pid]/ is same as /proc/[pid] */
    if (*file == '\0')
        return YOS_PROCFS_PID_DIR;
    if (strcmp(file, "stat") == 0)
        return YOS_PROCFS_STAT;
    if (strcmp(file, "cmdline") == 0)
        return YOS_PROCFS_CMDLINE;
    if (strcmp(file, "status") == 0)
        return YOS_PROCFS_STATUS;
    if (strcmp(file, "comm") == 0)
        return YOS_PROCFS_COMM;
    if (strcmp(file, "exe") == 0)
        return YOS_PROCFS_EXE;
    if (strcmp(file, "cwd") == 0)
        return YOS_PROCFS_CWD;

    return -ENOENT;
}

/*
 * Generate /proc/[pid]/stat content.
 * Format: pid (comm) state ppid pgrp session tty_nr tpgid flags ...
 */
static char *generate_stat(struct yos_exec_ctx *ctx, int32_t pid, size_t *len)
{
    struct yos_proc *proc = yos_proc_find(ctx->rt, pid);
    if (!proc) {
        *len = 0;
        return NULL;
    }

    char state;
    switch (proc->state) {
    case YOS_PROC_RUNNING: state = 'R'; break;
    case YOS_PROC_ZOMBIE:  state = 'Z'; break;
    default:               state = 'S'; break;
    }

    const char *comm = proc->comm;

    char *buf = (char *)malloc(512);
    if (!buf) {
        *len = 0;
        return NULL;
    }

    /* Minimal stat format - enough for ps to work */
    int n = snprintf(buf, 512,
        "%d (%s) %c %d %d %d 0 -1 0 "
        "0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
        proc->pid,
        comm,
        state,
        proc->ppid,
        proc->pgid,
        proc->sid);

    *len = (size_t)n;
    return buf;
}

/*
 * Generate /proc/[pid]/cmdline content.
 * Arguments separated by NUL bytes.
 */
static char *generate_cmdline(struct yos_exec_ctx *ctx, int32_t pid, size_t *len)
{
    struct yos_proc *proc = yos_proc_find(ctx->rt, pid);
    if (!proc || !proc->cmdline || proc->cmdline_argc <= 0) {
        *len = 0;
        return NULL;
    }

    /* Calculate total length */
    size_t total = 0;
    for (int i = 0; i < proc->cmdline_argc; i++)
        total += strlen(proc->cmdline[i]) + 1;

    char *buf = (char *)malloc(total);
    if (!buf) {
        *len = 0;
        return NULL;
    }

    /* Copy arguments with NUL separators */
    char *p = buf;
    for (int i = 0; i < proc->cmdline_argc; i++) {
        size_t l = strlen(proc->cmdline[i]) + 1;
        memcpy(p, proc->cmdline[i], l);
        p += l;
    }

    *len = total;
    return buf;
}

/*
 * Generate /proc/[pid]/status content.
 */
static char *generate_status(struct yos_exec_ctx *ctx, int32_t pid, size_t *len)
{
    struct yos_proc *proc = yos_proc_find(ctx->rt, pid);
    if (!proc) {
        *len = 0;
        return NULL;
    }

    const char *state_str;
    switch (proc->state) {
    case YOS_PROC_RUNNING: state_str = "R (running)"; break;
    case YOS_PROC_ZOMBIE:  state_str = "Z (zombie)"; break;
    default:               state_str = "S (sleeping)"; break;
    }

    char *buf = (char *)malloc(1024);
    if (!buf) {
        *len = 0;
        return NULL;
    }

    int n = snprintf(buf, 1024,
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%d\n"
        "PPid:\t%d\n"
        "Uid:\t0\t0\t0\t0\n"
        "Gid:\t0\t0\t0\t0\n",
        proc->comm,
        state_str,
        proc->pid,
        proc->ppid);

    *len = (size_t)n;
    return buf;
}

/*
 * Generate /proc/[pid]/comm content.
 */
static char *generate_comm(struct yos_exec_ctx *ctx, int32_t pid, size_t *len)
{
    struct yos_proc *proc = yos_proc_find(ctx->rt, pid);
    if (!proc) {
        *len = 0;
        return NULL;
    }

    size_t l = strlen(proc->comm);
    char *buf = (char *)malloc(l + 2);
    if (!buf) {
        *len = 0;
        return NULL;
    }

    memcpy(buf, proc->comm, l);
    buf[l] = '\n';
    buf[l + 1] = '\0';
    *len = l + 1;
    return buf;
}

/*
 * Generate /proc/mounts content.
 * Iterate our actual mount table.
 */
static char *generate_mounts(struct yos_exec_ctx *ctx, size_t *len)
{
    struct yos_mount_table *mt = (struct yos_mount_table *)ctx->rt->mount_table;
    if (!mt || mt->count == 0) {
        *len = 0;
        return NULL;
    }

    /* Estimate size: ~64 bytes per mount entry */
    size_t bufsize = mt->count * 64 + 1;
    char *buf = (char *)malloc(bufsize);
    if (!buf) {
        *len = 0;
        return NULL;
    }

    char *p = buf;
    size_t remaining = bufsize;
    for (int i = 0; i < mt->count; i++) {
        struct yos_mount *m = &mt->mounts[i];
        /* Format: source mountpoint fstype options dump fsck.
         * Linux /proc/mounts uses "proc" as the source; tests/ut/yos/
         * test_procfs.c (and userspace tools that grep /proc/mounts)
         * expect to find "proc /proc". */
        int n = snprintf(p, remaining, "proc %s proc rw 0 0\n", m->path);
        if (n > 0 && (size_t)n < remaining) {
            p += n;
            remaining -= n;
        }
    }

    *len = p - buf;
    return buf;
}

/* ============================================================================
 * File operations implementation
 * ============================================================================ */

static int32_t procfs_open(struct yos_exec_ctx *ctx, const char *path,
                           int32_t flags, int32_t mode)
{
    (void)flags;
    (void)mode;

    int32_t pid = 0;
    int type = parse_procfs_path(path, &pid);
    if (type < 0)
        return type;

    ydebug("procfs_open: path=%s type=%d pid=%d\n", path, type, pid);

    /* Verify pid exists if specified */
    if (pid > 0 && !yos_proc_find(ctx->rt, pid))
        return -ENOENT;

    /* Allocate file descriptor */
    struct yos_file_table *ft = (struct yos_file_table *)ctx->procfs_fds;
    if (!ft) {
        ft = (struct yos_file_table *)calloc(1, sizeof(struct yos_file_table));
        if (!ft)
            return -ENOMEM;
        yos_file_table_init(ft);
        ctx->procfs_fds = ft;
    }

    int32_t fd = yos_file_alloc(ft);
    if (fd < 0)
        return fd;

    struct yos_file *file = yos_file_get(ft, fd);
    file->ops = &yos_procfs_ops;
    file->procfs_pid = pid;
    file->procfs_file_type = type;

    /* Generate content for regular files */
    if (type == YOS_PROCFS_STAT)
        file->content = generate_stat(ctx, pid, &file->content_length);
    else if (type == YOS_PROCFS_CMDLINE)
        file->content = generate_cmdline(ctx, pid, &file->content_length);
    else if (type == YOS_PROCFS_STATUS)
        file->content = generate_status(ctx, pid, &file->content_length);
    else if (type == YOS_PROCFS_COMM)
        file->content = generate_comm(ctx, pid, &file->content_length);
    else if (type == YOS_PROCFS_MOUNTS)
        file->content = generate_mounts(ctx, &file->content_length);

    return fd;
}

static int32_t procfs_read(struct yos_exec_ctx *ctx, struct yos_file *file,
                           void *buf, size_t count)
{
    (void)ctx;

    if (!file->content)
        return 0;

    if (file->position >= file->content_length)
        return 0;

    size_t remaining = file->content_length - file->position;
    size_t to_read = count < remaining ? count : remaining;

    memcpy(buf, file->content + file->position, to_read);
    file->position += to_read;

    return (int32_t)to_read;
}

static int32_t procfs_close(struct yos_exec_ctx *ctx, struct yos_file *file)
{
    (void)ctx;
    (void)file;
    /* File cleanup happens in yos_file_free */
    return 0;
}

static int32_t procfs_stat(struct yos_exec_ctx *ctx, const char *path, void *statbuf)
{
    int32_t pid = 0;
    int type = parse_procfs_path(path, &pid);
    if (type < 0)
        return type;

    /* Verify pid exists if specified */
    if (pid > 0 && !yos_proc_find(ctx->rt, pid))
        return -ENOENT;

    /* Fill stat buffer - wasm32 stat64 layout */
    uint8_t *buf = (uint8_t *)statbuf;
    memset(buf, 0, 96);

    uint32_t mode;
    if (type == YOS_PROCFS_ROOT || type == YOS_PROCFS_PID_DIR)
        mode = S_IFDIR | 0555;
    else if (type == YOS_PROCFS_SELF || type == YOS_PROCFS_EXE || type == YOS_PROCFS_CWD)
        mode = S_IFLNK | 0777;
    else
        mode = S_IFREG | 0444;

    *(uint32_t *)(buf + 16) = mode;    /* st_mode */
    *(uint32_t *)(buf + 20) = 1;       /* st_nlink */

    return 0;
}

static int32_t procfs_getdents64(struct yos_exec_ctx *ctx, struct yos_file *file,
                                  void *dirp, size_t count)
{
    if (file->procfs_file_type != YOS_PROCFS_ROOT &&
        file->procfs_file_type != YOS_PROCFS_PID_DIR)
        return -ENOTDIR;

    uint8_t *buf = (uint8_t *)dirp;
    size_t pos = 0;

    if (file->procfs_file_type == YOS_PROCFS_ROOT) {
        /* List /proc - show "self" and all pids */
        int entry_idx = 0;

        /* Static entries in /proc */
        static const struct { const char *name; uint8_t type; } root_entries[] = {
            {"self", DT_LNK},
            {"mounts", DT_REG},
        };
        int num_root_entries = sizeof(root_entries) / sizeof(root_entries[0]);

        for (int i = 0; i < num_root_entries; i++) {
            if (file->dir_index <= entry_idx) {
                const char *name = root_entries[i].name;
                size_t name_len = strlen(name);
                size_t rec_len = (19 + name_len + 8) & ~7;

                if (pos + rec_len > count)
                    return (int32_t)pos;

                *(uint64_t *)(buf + pos) = entry_idx + 1;
                *(uint64_t *)(buf + pos + 8) = rec_len;
                *(uint16_t *)(buf + pos + 16) = rec_len;
                *(uint8_t *)(buf + pos + 18) = root_entries[i].type;
                memcpy(buf + pos + 19, name, name_len + 1);
                pos += rec_len;
                file->dir_index = entry_idx + 1;
            }
            entry_idx++;
        }

        /* List all running processes (not FREE or ZOMBIE) */
        for (int i = 0; i < YOS_MAX_PROCS; i++) {
            struct yos_proc *proc = &ctx->rt->procs[i];
            if (proc->state == YOS_PROC_FREE || proc->state == YOS_PROC_ZOMBIE)
                continue;

            if (file->dir_index <= entry_idx) {
                char name[16];
                int name_len = snprintf(name, sizeof(name), "%d", proc->pid);
                size_t rec_len = (19 + name_len + 8) & ~7;

                if (pos + rec_len > count)
                    return (int32_t)pos;

                *(uint64_t *)(buf + pos) = proc->pid;
                *(uint64_t *)(buf + pos + 8) = rec_len;
                *(uint16_t *)(buf + pos + 16) = rec_len;
                *(uint8_t *)(buf + pos + 18) = DT_DIR;
                memcpy(buf + pos + 19, name, name_len + 1);
                pos += rec_len;
                file->dir_index = entry_idx + 1;
            }
            entry_idx++;
        }
    } else if (file->procfs_file_type == YOS_PROCFS_PID_DIR) {
        /* List /proc/[pid] - files and symlinks */
        static const struct { const char *name; uint8_t type; } entries[] = {
            {"stat", DT_REG},
            {"cmdline", DT_REG},
            {"status", DT_REG},
            {"comm", DT_REG},
            {"exe", DT_LNK},
            {"cwd", DT_LNK},
        };
        int num_entries = sizeof(entries) / sizeof(entries[0]);

        for (int i = file->dir_index; i < num_entries; i++) {
            const char *name = entries[i].name;
            size_t name_len = strlen(name);
            size_t rec_len = (19 + name_len + 8) & ~7;

            if (pos + rec_len > count)
                return (int32_t)pos;

            *(uint64_t *)(buf + pos) = i + 1;
            *(uint64_t *)(buf + pos + 8) = rec_len;
            *(uint16_t *)(buf + pos + 16) = rec_len;
            *(uint8_t *)(buf + pos + 18) = entries[i].type;
            memcpy(buf + pos + 19, name, name_len + 1);
            pos += rec_len;
            file->dir_index = i + 1;
        }
    }

    return (int32_t)pos;
}

static int32_t procfs_readlink(struct yos_exec_ctx *ctx, const char *path,
                                char *buf, size_t bufsiz)
{
    int32_t pid = 0;
    int type = parse_procfs_path(path, &pid);

    switch (type) {
    case YOS_PROCFS_SELF:
        /* /proc/self -> current pid */
        return snprintf(buf, bufsiz, "%d", ctx->proc->pid);

    case YOS_PROCFS_EXE: {
        /* /proc/[pid]/exe -> executable path */
        struct yos_proc *proc = yos_proc_find(ctx->rt, pid);
        if (!proc || !proc->exe[0])
            return -ENOENT;
        size_t l = strlen(proc->exe);
        if (l >= bufsiz) l = bufsiz - 1;
        memcpy(buf, proc->exe, l);
        return (int32_t)l;
    }

    case YOS_PROCFS_CWD: {
        /* /proc/[pid]/cwd -> current working directory */
        struct yos_proc *proc = yos_proc_find(ctx->rt, pid);
        if (!proc || !proc->cwd[0])
            return -ENOENT;
        size_t l = strlen(proc->cwd);
        if (l >= bufsiz) l = bufsiz - 1;
        memcpy(buf, proc->cwd, l);
        return (int32_t)l;
    }

    default:
        return -EINVAL;
    }
}

const struct yos_file_operations yos_procfs_ops = {
    .open = procfs_open,
    .read = procfs_read,
    .close = procfs_close,
    .stat = procfs_stat,
    .getdents64 = procfs_getdents64,
    .readlink = procfs_readlink,
};
