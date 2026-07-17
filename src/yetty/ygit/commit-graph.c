/* commit-graph.c — DAG lane assignment.
 *
 * Standard commit-graph layout: walk commits newest-first, keeping a set of
 * "active lanes" where each lane is waiting for a particular commit (its
 * pending descendant's parent). When a commit is reached, it takes the lane
 * that was waiting for it (or a fresh lane if it is a branch tip); that lane
 * then advances to wait for the commit's first parent, and each additional
 * parent (a merge) opens a new lane. Lanes waiting for the same commit collapse
 * onto one. Each row records which lanes carry a vertical line so a renderer
 * can draw the graph.
 */

#include <yetty/ygit/commit-graph.h>

#include <stdlib.h>
#include <string.h>

/* Duplicate a string with malloc so every allocation in this file pairs with
 * the same free() the process's allocator provides — glibc strdup() would bind
 * libc's own malloc, which mismatches an interposed allocator on free. */
static char *ygit_graph_strdup(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

/* Active-lane vector: active[k] is the full hash the lane is waiting for, or
 * NULL for a free lane. */
struct ygit_lane_set {
    char **waiting;
    size_t count;
    size_t capacity;
};

static int ygit_lane_find(const struct ygit_lane_set *lanes, const char *hash)
{
    for (size_t index = 0; index < lanes->count; index++) {
        if (lanes->waiting[index] && strcmp(lanes->waiting[index], hash) == 0) {
            return (int)index;
        }
    }
    return -1;
}

/* Reserve a free lane (a NULL slot, or a freshly appended one) and return its
 * index, leaving the slot NULL. Returns -1 on allocation failure. */
static int ygit_lane_reserve(struct ygit_lane_set *lanes)
{
    for (size_t index = 0; index < lanes->count; index++) {
        if (!lanes->waiting[index]) {
            return (int)index;
        }
    }
    if (lanes->count == lanes->capacity) {
        size_t new_capacity = lanes->capacity ? lanes->capacity * 2 : 16;
        char **grown = realloc(lanes->waiting, new_capacity * sizeof(char *));
        if (!grown) {
            return -1;
        }
        lanes->waiting = grown;
        lanes->capacity = new_capacity;
    }
    lanes->waiting[lanes->count] = NULL;
    return (int)lanes->count++;
}

static void ygit_lane_set_release(struct ygit_lane_set *lanes)
{
    for (size_t index = 0; index < lanes->count; index++) {
        free(lanes->waiting[index]);
    }
    free(lanes->waiting);
    lanes->waiting = NULL;
    lanes->count = 0;
    lanes->capacity = 0;
}

void yetty_ygit_graph_destroy(struct yetty_ygit_graph *graph)
{
    if (!graph) {
        return;
    }
    for (size_t index = 0; index < graph->count; index++) {
        free(graph->rows[index].occupied);
    }
    free(graph->rows);
    free(graph);
}

struct yetty_ygit_graph_ptr_result yetty_ygit_graph_build(struct yetty_ygit_log *log)
{
    if (!log) {
        return YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: NULL log");
    }

    struct yetty_ygit_graph *graph = calloc(1, sizeof(*graph));
    if (!graph) {
        return YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
    }
    if (log->count == 0) {
        return YETTY_OK(yetty_ygit_graph_ptr, graph);
    }
    graph->rows = calloc(log->count, sizeof(*graph->rows));
    if (!graph->rows) {
        free(graph);
        return YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
    }

    struct ygit_lane_set lanes = {0};
    struct yetty_ygit_graph_ptr_result result =
        YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: unreached");

    for (size_t row_index = 0; row_index < log->count; row_index++) {
        struct yetty_ygit_commit *commit = &log->commits[row_index];

        int column = ygit_lane_find(&lanes, commit->full_hash);
        if (column < 0) {
            column = ygit_lane_reserve(&lanes);
            if (column < 0) {
                result = YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
                goto cleanup;
            }
        }
        /* This commit now occupies `column` for the snapshot. */
        if (!lanes.waiting[column]) {
            lanes.waiting[column] = ygit_graph_strdup(commit->full_hash);
            if (!lanes.waiting[column]) {
                result = YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
                goto cleanup;
            }
        }
        /* Collapse any other lane also waiting for this commit (a merge point
         * reached from several descendants). */
        for (size_t index = 0; index < lanes.count; index++) {
            if ((int)index != column && lanes.waiting[index] &&
                strcmp(lanes.waiting[index], commit->full_hash) == 0) {
                free(lanes.waiting[index]);
                lanes.waiting[index] = NULL;
            }
        }

        /* Snapshot occupancy, trimming trailing empty lanes. */
        int highest = column;
        for (size_t index = 0; index < lanes.count; index++) {
            if (lanes.waiting[index] && (int)index > highest) {
                highest = (int)index;
            }
        }
        int lane_count = highest + 1;
        struct yetty_ygit_graph_row *row = &graph->rows[row_index];
        row->column = column;
        row->lane_count = lane_count;
        row->occupied = calloc((size_t)lane_count, sizeof(unsigned char));
        if (!row->occupied) {
            result = YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
            goto cleanup;
        }
        for (int index = 0; index < lane_count; index++) {
            row->occupied[index] = ((size_t)index < lanes.count && lanes.waiting[index]) ? 1 : 0;
        }
        row->occupied[column] = 1;
        commit->lane = column;
        if (lane_count > graph->width) {
            graph->width = lane_count;
        }

        /* Advance `column` to the first parent; branch new lanes for the rest. */
        free(lanes.waiting[column]);
        lanes.waiting[column] = NULL;
        if (commit->parent_count > 0) {
            lanes.waiting[column] = ygit_graph_strdup(commit->parent_hashes[0]);
            if (!lanes.waiting[column]) {
                result = YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
                goto cleanup;
            }
            for (size_t parent_index = 1; parent_index < commit->parent_count; parent_index++) {
                /* Reuse an existing lane already waiting for this parent, else
                 * open a new one, so parallel merges don't fan out endlessly. */
                if (ygit_lane_find(&lanes, commit->parent_hashes[parent_index]) >= 0) {
                    continue;
                }
                int branch_column = ygit_lane_reserve(&lanes);
                if (branch_column < 0) {
                    result = YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
                    goto cleanup;
                }
                lanes.waiting[branch_column] =
                    ygit_graph_strdup(commit->parent_hashes[parent_index]);
                if (!lanes.waiting[branch_column]) {
                    result = YETTY_ERR(yetty_ygit_graph_ptr, "ygit graph: out of memory");
                    goto cleanup;
                }
            }
        }
    }

    graph->count = log->count;
    result = YETTY_OK(yetty_ygit_graph_ptr, graph);
    graph = NULL;

cleanup:
    ygit_lane_set_release(&lanes);
    yetty_ygit_graph_destroy(graph);
    return result;
}
