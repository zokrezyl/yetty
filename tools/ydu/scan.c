/*
 * scan.c — recursive disk-usage scan for ydu.
 *
 * Walks a directory with opendir/readdir/lstat, building a ydu_node tree whose
 * directory nodes carry the aggregated allocated/apparent bytes and item counts
 * of their subtrees. Symlinks are not followed (lstat), and files with more
 * than one hard link are charged once per (device, inode) via a small
 * open-addressing set so a tree with hard links does not double-count.
 */
#define _DEFAULT_SOURCE 1

#include "scan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define YDU_PATH_CAP 4096

/* ------------------------------------------------------------------ */
/* Seen-inode set (hard-link dedup) — open addressing, power-of-two.   */
/* ------------------------------------------------------------------ */

struct ydu_inode_set {
    uint64_t *dev;
    uint64_t *ino;
    uint8_t *used;
    size_t cap;
    size_t count;
};

static size_t inode_hash(uint64_t dev, uint64_t ino, size_t mask)
{
    uint64_t hash = (ino * 1099511628211ULL) ^ (dev * 1469598103934665603ULL);
    return (size_t)hash & mask;
}

static int inode_set_init(struct ydu_inode_set *set, size_t cap)
{
    set->dev = calloc(cap, sizeof(*set->dev));
    set->ino = calloc(cap, sizeof(*set->ino));
    set->used = calloc(cap, sizeof(*set->used));
    set->cap = cap;
    set->count = 0;
    if (!set->dev || !set->ino || !set->used) {
        free(set->dev);
        free(set->ino);
        free(set->used);
        return 0;
    }
    return 1;
}

static void inode_set_free(struct ydu_inode_set *set)
{
    free(set->dev);
    free(set->ino);
    free(set->used);
    set->dev = NULL;
    set->ino = NULL;
    set->used = NULL;
    set->cap = 0;
    set->count = 0;
}

static int inode_set_grow(struct ydu_inode_set *set)
{
    size_t new_cap = set->cap * 2;
    uint64_t *new_dev = calloc(new_cap, sizeof(*new_dev));
    uint64_t *new_ino = calloc(new_cap, sizeof(*new_ino));
    uint8_t *new_used = calloc(new_cap, sizeof(*new_used));
    if (!new_dev || !new_ino || !new_used) {
        free(new_dev);
        free(new_ino);
        free(new_used);
        return 0;
    }
    size_t mask = new_cap - 1;
    for (size_t i = 0; i < set->cap; i++) {
        if (!set->used[i]) {
            continue;
        }
        size_t slot = inode_hash(set->dev[i], set->ino[i], mask);
        while (new_used[slot]) {
            slot = (slot + 1) & mask;
        }
        new_used[slot] = 1;
        new_dev[slot] = set->dev[i];
        new_ino[slot] = set->ino[i];
    }
    free(set->dev);
    free(set->ino);
    free(set->used);
    set->dev = new_dev;
    set->ino = new_ino;
    set->used = new_used;
    set->cap = new_cap;
    return 1;
}

/* Returns 1 if (dev, ino) was newly inserted, 0 if it was already present.
 * On allocation failure the entry is treated as new (counted), which at worst
 * double-counts a hard link rather than losing it. */
static int inode_set_add(struct ydu_inode_set *set, uint64_t dev, uint64_t ino)
{
    if ((set->count + 1) * 10 >= set->cap * 7) {
        if (!inode_set_grow(set)) {
            return 1;
        }
    }
    size_t mask = set->cap - 1;
    size_t slot = inode_hash(dev, ino, mask);
    while (set->used[slot]) {
        if (set->dev[slot] == dev && set->ino[slot] == ino) {
            return 0;
        }
        slot = (slot + 1) & mask;
    }
    set->used[slot] = 1;
    set->dev[slot] = dev;
    set->ino[slot] = ino;
    set->count++;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Node allocation                                                     */
/* ------------------------------------------------------------------ */

static struct ydu_node *node_new(const char *name, int is_dir)
{
    struct ydu_node *node = calloc(1, sizeof(*node));
    if (!node) {
        return NULL;
    }
    size_t len = strlen(name);
    node->name = malloc(len + 1);
    if (!node->name) {
        free(node);
        return NULL;
    }
    memcpy(node->name, name, len + 1);
    node->is_dir = is_dir;
    return node;
}

static int node_add_child(struct ydu_node *parent, struct ydu_node *child)
{
    if (parent->n_children == parent->cap_children) {
        size_t new_cap = parent->cap_children ? parent->cap_children * 2 : 16;
        struct ydu_node **grown = realloc(parent->children, new_cap * sizeof(*grown));
        if (!grown) {
            return 0;
        }
        parent->children = grown;
        parent->cap_children = new_cap;
    }
    child->parent = parent;
    parent->children[parent->n_children++] = child;
    return 1;
}

void ydu_node_destroy(struct ydu_node *node)
{
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->n_children; i++) {
        ydu_node_destroy(node->children[i]);
    }
    free(node->children);
    free(node->name);
    free(node);
}

/* ------------------------------------------------------------------ */
/* Recursive walk                                                      */
/* ------------------------------------------------------------------ */

static void scan_dir(struct ydu_node *dir, char *path, size_t path_len, struct ydu_inode_set *seen,
                     struct ydu_scan_stats *stats)
{
    DIR *dirp = opendir(path);
    if (!dirp) {
        stats->errors++;
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        const char *name = entry->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        size_t name_len = strlen(name);
        if (path_len + 1 + name_len + 1 > YDU_PATH_CAP) {
            stats->errors++;
            continue;
        }
        path[path_len] = '/';
        memcpy(path + path_len + 1, name, name_len + 1);
        size_t child_len = path_len + 1 + name_len;

        struct stat st;
        if (lstat(path, &st) != 0) {
            stats->errors++;
            path[path_len] = '\0';
            continue;
        }
        int is_dir = S_ISDIR(st.st_mode);
        struct ydu_node *child = node_new(name, is_dir);
        if (!child) {
            path[path_len] = '\0';
            continue;
        }
        child->mtime = (int64_t)st.st_mtime;

        int count_bytes = 1;
        if (st.st_nlink > 1 && !is_dir) {
            count_bytes = inode_set_add(seen, (uint64_t)st.st_dev, (uint64_t)st.st_ino);
        }

        if (is_dir) {
            stats->dirs++;
            scan_dir(child, path, child_len, seen, stats);
            /* The directory inode's own allocation, on top of its contents. */
            child->disk_bytes += (uint64_t)st.st_blocks * 512;
            child->apparent_bytes += (uint64_t)st.st_size;
        } else {
            stats->files++;
            if (count_bytes) {
                child->disk_bytes = (uint64_t)st.st_blocks * 512;
                child->apparent_bytes = (uint64_t)st.st_size;
            }
        }

        dir->disk_bytes += child->disk_bytes;
        dir->apparent_bytes += child->apparent_bytes;
        dir->item_count += 1 + child->item_count;
        if (!node_add_child(dir, child)) {
            ydu_node_destroy(child);
        }
        path[path_len] = '\0';
    }
    closedir(dirp);
}

struct yetty_ycore_void_result ydu_scan(const char *path, struct ydu_node **out_root,
                                        struct ydu_scan_stats *stats)
{
    if (!path || !out_root || !stats) {
        return YETTY_ERR(yetty_ycore_void, "ydu_scan: null argument");
    }
    memset(stats, 0, sizeof(*stats));

    struct stat st;
    if (lstat(path, &st) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydu_scan: cannot stat path");
    }
    int is_dir = S_ISDIR(st.st_mode);
    struct ydu_node *root = node_new(path, is_dir);
    if (!root) {
        return YETTY_ERR(yetty_ycore_void, "ydu_scan: out of memory");
    }
    root->mtime = (int64_t)st.st_mtime;

    if (is_dir) {
        struct ydu_inode_set seen;
        if (!inode_set_init(&seen, 1024)) {
            ydu_node_destroy(root);
            return YETTY_ERR(yetty_ycore_void, "ydu_scan: out of memory");
        }
        char buf[YDU_PATH_CAP];
        size_t path_len = strlen(path);
        if (path_len >= YDU_PATH_CAP) {
            inode_set_free(&seen);
            ydu_node_destroy(root);
            return YETTY_ERR(yetty_ycore_void, "ydu_scan: path too long");
        }
        memcpy(buf, path, path_len + 1);
        while (path_len > 1 && buf[path_len - 1] == '/') {
            buf[--path_len] = '\0';
        }
        stats->dirs++;
        scan_dir(root, buf, path_len, &seen, stats);
        root->disk_bytes += (uint64_t)st.st_blocks * 512;
        root->apparent_bytes += (uint64_t)st.st_size;
        inode_set_free(&seen);
    } else {
        stats->files++;
        root->disk_bytes = (uint64_t)st.st_blocks * 512;
        root->apparent_bytes = (uint64_t)st.st_size;
    }

    *out_root = root;
    return YETTY_OK_VOID();
}

/* ------------------------------------------------------------------ */
/* Sorting                                                             */
/* ------------------------------------------------------------------ */

static const struct ydu_node *node_of(const void *elem)
{
    return *(const struct ydu_node *const *)elem;
}

static int cmp_disk(const void *a, const void *b)
{
    const struct ydu_node *x = node_of(a), *y = node_of(b);
    if (x->disk_bytes != y->disk_bytes) {
        return y->disk_bytes > x->disk_bytes ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

static int cmp_apparent(const void *a, const void *b)
{
    const struct ydu_node *x = node_of(a), *y = node_of(b);
    if (x->apparent_bytes != y->apparent_bytes) {
        return y->apparent_bytes > x->apparent_bytes ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

static int cmp_items(const void *a, const void *b)
{
    const struct ydu_node *x = node_of(a), *y = node_of(b);
    if (x->item_count != y->item_count) {
        return y->item_count > x->item_count ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(node_of(a)->name, node_of(b)->name);
}

static int cmp_mtime(const void *a, const void *b)
{
    const struct ydu_node *x = node_of(a), *y = node_of(b);
    if (x->mtime != y->mtime) {
        return y->mtime > x->mtime ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

void ydu_node_sort_children(struct ydu_node *node, enum ydu_sort_mode mode)
{
    if (!node || node->n_children < 2) {
        return;
    }
    int (*cmp)(const void *, const void *) = cmp_disk;
    switch (mode) {
    case YDU_SORT_DISK:
        cmp = cmp_disk;
        break;
    case YDU_SORT_APPARENT:
        cmp = cmp_apparent;
        break;
    case YDU_SORT_ITEMS:
        cmp = cmp_items;
        break;
    case YDU_SORT_NAME:
        cmp = cmp_name;
        break;
    case YDU_SORT_MTIME:
        cmp = cmp_mtime;
        break;
    case YDU_SORT_MODE_COUNT:
        cmp = cmp_disk;
        break;
    }
    qsort(node->children, node->n_children, sizeof(node->children[0]), cmp);
}

const char *ydu_sort_mode_name(enum ydu_sort_mode mode)
{
    switch (mode) {
    case YDU_SORT_DISK:
        return "size";
    case YDU_SORT_APPARENT:
        return "apparent";
    case YDU_SORT_ITEMS:
        return "items";
    case YDU_SORT_NAME:
        return "name";
    case YDU_SORT_MTIME:
        return "mtime";
    case YDU_SORT_MODE_COUNT:
        break;
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Formatting helpers                                                  */
/* ------------------------------------------------------------------ */

void ydu_fmt_bytes(uint64_t bytes, char *out, size_t out_size)
{
    double value = (double)bytes;
    if (value >= 1024.0 * 1024 * 1024 * 1024) {
        snprintf(out, out_size, "%.2f TiB", value / (1024.0 * 1024 * 1024 * 1024));
    } else if (value >= 1024.0 * 1024 * 1024) {
        snprintf(out, out_size, "%.2f GiB", value / (1024.0 * 1024 * 1024));
    } else if (value >= 1024.0 * 1024) {
        snprintf(out, out_size, "%.1f MiB", value / (1024.0 * 1024));
    } else if (value >= 1024.0) {
        snprintf(out, out_size, "%.1f KiB", value / 1024.0);
    } else {
        snprintf(out, out_size, "%.0f B", value);
    }
}

void ydu_node_path(const struct ydu_node *node, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!node) {
        return;
    }
    const struct ydu_node *chain[256];
    int depth = 0;
    for (const struct ydu_node *cur = node; cur && depth < 256; cur = cur->parent) {
        chain[depth++] = cur;
    }
    size_t pos = 0;
    for (int i = depth - 1; i >= 0 && pos < out_size; i--) {
        const char *seg = chain[i]->name;
        int written;
        if (i == depth - 1) {
            written = snprintf(out + pos, out_size - pos, "%s", seg);
        } else {
            written = snprintf(out + pos, out_size - pos, "/%s", seg);
        }
        if (written < 0) {
            break;
        }
        pos += (size_t)written;
    }
}
