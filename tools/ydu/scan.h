/*
 * scan.h — ydu filesystem scan model.
 *
 * A `ydu_node` tree aggregates disk usage bottom-up: each directory node holds
 * the summed allocated bytes (st_blocks * 512), apparent bytes (st_size), and
 * recursive item count of everything beneath it. Hard links (nlink > 1) are
 * counted once per (device, inode). The tree is a plain heap structure with no
 * yetty dependency beyond the Result type, so it can be exercised headless via
 * `ydu --print`.
 */
#ifndef YDU_SCAN_H
#define YDU_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

struct ydu_node {
    char *name;              /* basename (heap-owned); root holds the scan path */
    uint64_t disk_bytes;     /* allocated bytes, aggregated (self + descendants) */
    uint64_t apparent_bytes; /* apparent st_size, aggregated */
    uint64_t item_count;     /* number of descendant entries (files + dirs) */
    int64_t mtime;           /* modification time, unix seconds */
    int is_dir;
    struct ydu_node *parent;
    struct ydu_node **children;
    size_t n_children;
    size_t cap_children;
};

/* Best-effort scan counters — permission-denied dirs bump `errors` and are
 * skipped rather than aborting the whole scan. */
struct ydu_scan_stats {
    uint64_t dirs;
    uint64_t files;
    uint64_t errors;
};

/*
 * Recursively scan `path`. On success `*out_root` is a heap tree owned by the
 * caller (free with ydu_node_destroy); `stats` receives the scan counters.
 * Fails only when the root path cannot be stat'd or on allocation failure —
 * per-entry errors are absorbed into `stats->errors`.
 */
struct yetty_ycore_void_result ydu_scan(const char *path, struct ydu_node **out_root,
                                        struct ydu_scan_stats *stats);

void ydu_node_destroy(struct ydu_node *node);

/* Sort order for a directory's children. */
enum ydu_sort_mode {
    YDU_SORT_DISK = 0, /* allocated size, largest first (default) */
    YDU_SORT_APPARENT, /* apparent size, largest first */
    YDU_SORT_ITEMS,    /* item count, most first */
    YDU_SORT_NAME,     /* name, A→Z */
    YDU_SORT_MTIME,    /* modification time, newest first */
    YDU_SORT_MODE_COUNT
};

void ydu_node_sort_children(struct ydu_node *node, enum ydu_sort_mode mode);
const char *ydu_sort_mode_name(enum ydu_sort_mode mode);

/* Human-readable IEC byte size into `out` (e.g. "1.4 GiB"). */
void ydu_fmt_bytes(uint64_t bytes, char *out, size_t out_size);

/* Reconstruct a node's path (root name joined with each descendant basename). */
void ydu_node_path(const struct ydu_node *node, char *out, size_t out_size);

#endif /* YDU_SCAN_H */
