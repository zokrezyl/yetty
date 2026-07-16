/* commit-graph.h — DAG lane layout for a commit log.
 *
 * Assigns each commit a column (lane) and records, per row, which lanes carry
 * a vertical line — the model a renderer draws the commit graph from, whether
 * as text (the ygit CLI) or as GPU primitives (a future interactive pane).
 * This is deliberately independent of any renderer.
 *
 * Plain-C leaf helper (no object surface). The lane assignment also writes
 * each commit's `lane` field back into the log for callers that index by
 * commit rather than by row.
 */

#ifndef YETTY_YGIT_COMMIT_GRAPH_H
#define YETTY_YGIT_COMMIT_GRAPH_H

#include <yetty/ycore/result.h>
#include <yetty/ygit/git-backend.h>

#include <stddef.h>

struct yetty_ygit_graph_row {
    int column;              /* lane the row's commit sits in */
    int lane_count;          /* number of lanes to consider at this row */
    unsigned char *occupied; /* length lane_count; 1 where a lane carries a line */
};

struct yetty_ygit_graph {
    struct yetty_ygit_graph_row *rows; /* one per commit, same order as the log */
    size_t count;
    int width; /* max lane_count across all rows */
};

YETTY_YRESULT_DECLARE(yetty_ygit_graph_ptr, struct yetty_ygit_graph *);

/* Compute the lane layout for `log` (newest-first order). Writes each commit's
 * `lane` field. The returned graph is owned by the caller. */
struct yetty_ygit_graph_ptr_result yetty_ygit_graph_build(struct yetty_ygit_log *log);

void yetty_ygit_graph_destroy(struct yetty_ygit_graph *graph);

#endif /* YETTY_YGIT_COMMIT_GRAPH_H */
