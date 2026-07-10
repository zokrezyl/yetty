#include "mount.h"
#include <string.h>
#include <errno.h>

void yos_mount_table_init(struct yos_mount_table *table)
{
    memset(table, 0, sizeof(*table));
}

int yos_mount_add(struct yos_mount_table *table, const char *path,
                  const struct yos_file_operations *ops)
{
    if (table->count >= YOS_MAX_MOUNTS)
        return -ENOMEM;

    size_t len = strlen(path);
    if (len >= YOS_MOUNT_PATH_MAX)
        return -ENAMETOOLONG;

    struct yos_mount *m = &table->mounts[table->count];
    memcpy(m->path, path, len + 1);
    m->path_len = len;
    m->ops = ops;
    table->count++;

    return 0;
}

/*
 * Check if path matches mount point exactly or as a parent directory.
 * Returns length of mount path if match, 0 otherwise.
 *
 * Examples for mount at "/proc":
 *   "/proc"      -> matches, returns 5
 *   "/proc/"     -> matches, returns 5
 *   "/proc/1"    -> matches, returns 5
 *   "/procfs"    -> NO match (different directory)
 *   "/pro"       -> NO match
 */
static size_t mount_path_matches(const char *mount_path, size_t mount_len,
                                  const char *path)
{
    if (strncmp(path, mount_path, mount_len) != 0)
        return 0;

    /* After mount path, must be end of string, '/', or nothing */
    char next = path[mount_len];
    if (next == '\0' || next == '/')
        return mount_len;

    return 0;
}

const struct yos_file_operations *yos_mount_resolve(
    struct yos_mount_table *table,
    const char *path,
    const char **remaining)
{
    const struct yos_mount *best_match = NULL;
    size_t best_len = 0;

    /*
     * Find the LONGEST matching mount point.
     * This handles nested mounts correctly:
     *   /proc mounted
     *   /proc/sys/fs/binfmt_misc mounted
     * Path /proc/sys/fs/binfmt_misc/status matches the longer one.
     */
    for (int i = 0; i < table->count; i++) {
        const struct yos_mount *m = &table->mounts[i];
        size_t match_len = mount_path_matches(m->path, m->path_len, path);

        if (match_len > best_len) {
            best_match = m;
            best_len = match_len;
        }
    }

    if (!best_match) {
        *remaining = path;
        return NULL;
    }

    /* Set remaining path (skip mount point and leading slash) */
    const char *rest = path + best_len;
    if (*rest == '/')
        rest++;
    *remaining = rest;

    return best_match->ops;
}
