#include "file.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

void yos_file_table_init(struct yos_file_table *table)
{
    memset(table, 0, sizeof(*table));
}

int32_t yos_file_alloc(struct yos_file_table *table)
{
    for (int i = 0; i < YOS_MAX_VFS_FILES; i++) {
        if (!table->files[i].in_use) {
            memset(&table->files[i], 0, sizeof(struct yos_file));
            table->files[i].in_use = 1;
            return YOS_VFS_FD_BASE + i;
        }
    }
    return -EMFILE;
}

void yos_file_free(struct yos_file_table *table, int32_t fd)
{
    if (fd < YOS_VFS_FD_BASE)
        return;

    int idx = fd - YOS_VFS_FD_BASE;
    if (idx < 0 || idx >= YOS_MAX_VFS_FILES)
        return;

    struct yos_file *f = &table->files[idx];
    if (f->content) {
        free(f->content);
        f->content = NULL;
    }
    f->in_use = 0;
}

struct yos_file *yos_file_get(struct yos_file_table *table, int32_t fd)
{
    if (fd < YOS_VFS_FD_BASE)
        return NULL;

    int idx = fd - YOS_VFS_FD_BASE;
    if (idx < 0 || idx >= YOS_MAX_VFS_FILES)
        return NULL;

    struct yos_file *f = &table->files[idx];
    if (!f->in_use)
        return NULL;

    return f;
}
