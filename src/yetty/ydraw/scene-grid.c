/* scene-grid.c — opaque grid implementation for scene-canvas.
 *
 * Owns the rows × cols `cell[]` array. Each cell holds a flat list of
 * (entity_slot, local_indices[]) buckets.
 */

#include "scene-grid.h"

#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>

/*===========================================================================
 * Private types
 *===========================================================================*/

struct cell_bucket {
    uint32_t  entity_slot;
    uint32_t *local_indices;
    uint32_t  count;
    uint32_t  capacity;
};

struct scene_cell {
    struct cell_bucket *buckets;
    uint32_t bucket_count;
    uint32_t bucket_capacity;
};

struct yetty_ydraw_scene_grid {
    struct scene_cell *cells;
    uint32_t           cell_count;
    uint32_t           rows;
    uint32_t           cols;
};

/*===========================================================================
 * Small helpers
 *===========================================================================*/

static struct yetty_ycore_void_result grow_u32(uint32_t **arr, uint32_t *cap,
                                               uint32_t need)
{
    if (need <= *cap) return YETTY_OK_VOID();
    uint32_t new_cap = *cap ? *cap * 2 : 8;
    while (new_cap < need) new_cap *= 2;
    uint32_t *grown = realloc(*arr, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-grid: u32 grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grow_buckets(struct cell_bucket **arr,
                                                   uint32_t *cap, uint32_t need)
{
    if (need <= *cap) return YETTY_OK_VOID();
    uint32_t new_cap = *cap ? *cap * 2 : 4;
    while (new_cap < need) new_cap *= 2;
    struct cell_bucket *grown =
        realloc(*arr, new_cap * sizeof(struct cell_bucket));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-grid: buckets grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

/* Buckets within a cell are kept SORTED by entity_slot ascending. The
 * staging build walks them in array order and the shader renders the
 * resulting drawable list in that order, so cell-bucket order is the
 * inter-entity painter's-algorithm order.
 *
 * Insertion order would otherwise be unstable across DELETE+GROUP: a
 * re-emitted entity's bucket would be appended to the end of each cell
 * it touches, so its drawables would paint on top of every sibling
 * entity that wasn't re-emitted this frame. Sorting by entity_slot
 * reuses the natural slot ordering — slot 0 = ROOT, slots 1+ are
 * creation order, and the freelist returns the same slot to a
 * DELETE+GROUP of the same external_id — which matches the producer's
 * tree-walk emit order for the common case. */
static struct cell_bucket *cell_find_bucket(struct scene_cell *cell,
                                            uint32_t entity_slot)
{
    uint32_t lo = 0, hi = cell->bucket_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t slot = cell->buckets[mid].entity_slot;
        if (slot == entity_slot) {
            return &cell->buckets[mid];
        }
        if (slot < entity_slot) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result cell_ensure_bucket(
    struct scene_cell *cell, uint32_t entity_slot, struct cell_bucket **out, bool *out_fresh)
{
    /* Locate either the existing bucket or the insertion index for a
     * fresh one, keeping the sorted invariant. Linear scan is fine
     * here — cells with many buckets are rare in practice. */
    uint32_t pos = 0;
    while (pos < cell->bucket_count && cell->buckets[pos].entity_slot < entity_slot) {
        pos++;
    }
    if (pos < cell->bucket_count && cell->buckets[pos].entity_slot == entity_slot) {
        *out = &cell->buckets[pos];
        *out_fresh = false;
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result gr =
        grow_buckets(&cell->buckets, &cell->bucket_capacity, cell->bucket_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "cell_ensure_bucket: grow");
    if (pos < cell->bucket_count) {
        memmove(&cell->buckets[pos + 1], &cell->buckets[pos],
                (cell->bucket_count - pos) * sizeof(struct cell_bucket));
    }
    cell->bucket_count++;
    struct cell_bucket *b = &cell->buckets[pos];
    b->entity_slot   = entity_slot;
    b->local_indices = NULL;
    b->count         = 0;
    b->capacity      = 0;
    *out = b;
    *out_fresh = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result bucket_push_index(struct cell_bucket *b,
                                                        uint32_t local_idx)
{
    struct yetty_ycore_void_result gr =
        grow_u32(&b->local_indices, &b->capacity, b->count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-grid: bucket push");
    b->local_indices[b->count++] = local_idx;
    return YETTY_OK_VOID();
}

static void cell_drop_bucket_at(struct scene_cell *cell, uint32_t idx)
{
    struct cell_bucket *b = &cell->buckets[idx];
    free(b->local_indices);
    /* Shift tail left to preserve the ascending-slot invariant —
     * swap-with-last would corrupt cell-bucket order, which the staging
     * build relies on for inter-entity paint ordering. */
    if (idx + 1u < cell->bucket_count) {
        memmove(&cell->buckets[idx], &cell->buckets[idx + 1u],
                (cell->bucket_count - idx - 1u) * sizeof(struct cell_bucket));
    }
    cell->bucket_count--;
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ydraw_scene_grid_ptr_result yetty_ydraw_scene_grid_create(void)
{
    struct yetty_ydraw_scene_grid *grid =
        calloc(1, sizeof(struct yetty_ydraw_scene_grid));
    if (!grid) {
        return YETTY_ERR(yetty_ydraw_scene_grid_ptr, "scene-grid: alloc failed");
    }
    return YETTY_OK(yetty_ydraw_scene_grid_ptr, grid);
}

static void grid_free_cells(struct yetty_ydraw_scene_grid *grid)
{
    if (!grid->cells) return;
    for (uint32_t i = 0; i < grid->cell_count; i++) {
        struct scene_cell *c = &grid->cells[i];
        for (uint32_t b = 0; b < c->bucket_count; b++) {
            free(c->buckets[b].local_indices);
        }
        free(c->buckets);
    }
    free(grid->cells);
    grid->cells = NULL;
    grid->cell_count = 0;
}

void yetty_ydraw_scene_grid_destroy(struct yetty_ydraw_scene_grid *grid)
{
    if (!grid) return;
    grid_free_cells(grid);
    free(grid);
}

struct yetty_ycore_void_result yetty_ydraw_scene_grid_set_size(
    struct yetty_ydraw_scene_grid *grid, uint32_t rows, uint32_t cols)
{
    if (!grid) return YETTY_ERR(yetty_ycore_void, "scene-grid: NULL");
    grid_free_cells(grid);
    grid->rows = rows;
    grid->cols = cols;
    uint32_t total = rows * cols;
    if (total == 0) {
        return YETTY_OK_VOID();
    }
    grid->cells = calloc(total, sizeof(struct scene_cell));
    if (!grid->cells) {
        return YETTY_ERR(yetty_ycore_void, "scene-grid: cells alloc failed");
    }
    grid->cell_count = total;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Cell mutation
 *===========================================================================*/

struct insert_undo {
    uint32_t row;
    uint32_t col;
    bool was_fresh;
};

enum { INSERT_UNDO_STACK = 64 };

struct yetty_ycore_void_result yetty_ydraw_scene_grid_insert(
    struct yetty_ydraw_scene_grid *grid,
    uint32_t entity_slot, uint32_t local_idx,
    uint32_t row_min, uint32_t row_max, uint32_t col_min, uint32_t col_max,
    yetty_ydraw_scene_grid_fresh_cb on_fresh, void *user)
{
    if (!grid) return YETTY_ERR(yetty_ycore_void, "scene-grid: NULL");
    if (grid->cell_count == 0 || grid->cols == 0) return YETTY_OK_VOID();
    if (row_max >= grid->rows) row_max = grid->rows - 1;
    if (col_max >= grid->cols) col_max = grid->cols - 1;
    if (row_min > row_max || col_min > col_max) return YETTY_OK_VOID();

    /* Atomic: either every cell in the rectangle gets local_idx (and
     * on_fresh fires for each newly-bucketed cell), or none do. The
     * undo log tracks committed iterations so we can roll back on any
     * allocation failure. on_fresh is contractually infallible — the
     * caller pre-grows whatever buffer it appends to. */
    uint64_t rect = (uint64_t)(row_max - row_min + 1) * (col_max - col_min + 1);
    struct insert_undo stack_undo[INSERT_UNDO_STACK];
    struct insert_undo *undo = stack_undo;
    bool heap_undo = false;
    if (rect > INSERT_UNDO_STACK) {
        undo = calloc((size_t)rect, sizeof(struct insert_undo));
        if (!undo) {
            return YETTY_ERR(yetty_ycore_void, "scene-grid: insert undo alloc");
        }
        heap_undo = true;
    }
    uint32_t undo_count = 0;
    struct yetty_ycore_void_result final_err = YETTY_OK_VOID();
    bool aborted = false;

    for (uint32_t r = row_min; r <= row_max && !aborted; r++) {
        for (uint32_t c = col_min; c <= col_max && !aborted; c++) {
            struct scene_cell *cell = &grid->cells[r * grid->cols + c];
            struct cell_bucket *b = NULL;
            bool fresh = false;
            struct yetty_ycore_void_result br =
                cell_ensure_bucket(cell, entity_slot, &b, &fresh);
            if (YETTY_IS_ERR(br)) {
                final_err = YETTY_ERR(yetty_ycore_void, "scene-grid: ensure_bucket", br);
                aborted = true;
                break;
            }
            struct yetty_ycore_void_result pr = bucket_push_index(b, local_idx);
            if (YETTY_IS_ERR(pr)) {
                /* If the bucket was created in this iteration it's now
                 * empty and ours alone — drop before falling into the
                 * shared rollback path. With sorted insertion the fresh
                 * bucket is at a position determined by entity_slot,
                 * not necessarily the end of the array, so locate it
                 * by slot rather than indexing the last slot. */
                if (fresh) {
                    for (uint32_t bi = 0; bi < cell->bucket_count; bi++) {
                        if (cell->buckets[bi].entity_slot == entity_slot) {
                            cell_drop_bucket_at(cell, bi);
                            break;
                        }
                    }
                }
                final_err = YETTY_ERR(yetty_ycore_void, "scene-grid: push_index", pr);
                aborted = true;
                break;
            }
            if (fresh && on_fresh) {
                struct yetty_ycore_void_result fr = on_fresh(user, r, c);
                if (YETTY_IS_ERR(fr)) {
                    /* Caller violated the pre-grow contract. Roll back
                     * this iteration's commit so the shared rollback
                     * loop only sees prior committed cells. */
                    for (uint32_t bi = 0; bi < cell->bucket_count; bi++) {
                        if (cell->buckets[bi].entity_slot == entity_slot) {
                            cell_drop_bucket_at(cell, bi);
                            break;
                        }
                    }
                    final_err = YETTY_ERR(yetty_ycore_void, "scene-grid: on_fresh", fr);
                    aborted = true;
                    break;
                }
            }
            undo[undo_count++] = (struct insert_undo){.row = r, .col = c, .was_fresh = fresh};
        }
    }

    if (aborted) {
        for (uint32_t i = undo_count; i-- > 0; ) {
            struct scene_cell *cell = &grid->cells[undo[i].row * grid->cols + undo[i].col];
            for (uint32_t bi = 0; bi < cell->bucket_count; bi++) {
                if (cell->buckets[bi].entity_slot != entity_slot) continue;
                if (undo[i].was_fresh) {
                    cell_drop_bucket_at(cell, bi);
                } else if (cell->buckets[bi].count > 0) {
                    cell->buckets[bi].count--;
                }
                break;
            }
        }
        if (heap_undo) free(undo);
        return final_err;
    }

    if (heap_undo) free(undo);
    return YETTY_OK_VOID();
}

void yetty_ydraw_scene_grid_drop_at(
    struct yetty_ydraw_scene_grid *grid, uint32_t row, uint32_t col,
    uint32_t entity_slot)
{
    if (!grid || row >= grid->rows || col >= grid->cols) return;
    uint32_t idx = row * grid->cols + col;
    if (idx >= grid->cell_count) return;
    struct scene_cell *cell = &grid->cells[idx];
    for (uint32_t i = 0; i < cell->bucket_count; i++) {
        if (cell->buckets[i].entity_slot == entity_slot) {
            cell_drop_bucket_at(cell, i);
            return;
        }
    }
}

/*===========================================================================
 * Staging build — emit the per-cell drawable-index table.
 *
 * Format matches scrolling-grid's grid_staging exactly so the shader is
 * shared. See scene-grid.h for the layout.
 *===========================================================================*/

static struct yetty_ycore_void_result staging_ensure_cap(uint32_t **buf, uint32_t *cap,
                                                         uint32_t need)
{
    if (need <= *cap) return YETTY_OK_VOID();
    uint32_t new_cap = *cap ? *cap * 2 : 256;
    while (new_cap < need) new_cap *= 2;
    uint32_t *grown = realloc(*buf, new_cap * sizeof(uint32_t));
    if (!grown) return YETTY_ERR(yetty_ycore_void, "scene-grid: staging realloc");
    *buf = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scene_grid_rebuild_staging(
    const struct yetty_ydraw_scene_grid *grid, const uint32_t *entity_base,
    uint32_t entity_base_count, uint32_t **inout_buf, uint32_t *inout_capacity,
    uint32_t *out_count)
{
    if (!grid || !inout_buf || !inout_capacity || !out_count) {
        return YETTY_ERR(yetty_ycore_void, "scene-grid: rebuild_staging null arg");
    }
    if (grid->cell_count == 0) {
        *out_count = 0;
        return YETTY_OK_VOID();
    }
    if (entity_base_count > 0 && !entity_base) {
        return YETTY_ERR(yetty_ycore_void, "scene-grid: rebuild_staging null entity_base");
    }

    /* Pre-pass: compute the exact total word count we'll emit
     * (num_cells header offsets + one count-slot per cell + every
     * global drawable index). One staging_ensure_cap upfront then the
     * emit loop runs allocation-free, avoiding the realloc + copy
     * chain the doubling-grow version would do on the first dense
     * frame. */
    uint32_t num_cells = grid->cell_count;
    uint64_t total = (uint64_t)num_cells;
    for (uint32_t cell_idx = 0; cell_idx < num_cells; cell_idx++) {
        const struct scene_cell *cell = &grid->cells[cell_idx];
        total += 1; /* cell_count slot */
        for (uint32_t b = 0; b < cell->bucket_count; b++) {
            const struct cell_bucket *bk = &cell->buckets[b];
            if (bk->entity_slot >= entity_base_count) continue; /* defensive */
            total += bk->count;
        }
    }
    if (total > UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "scene-grid: staging size overflow");
    }
    struct yetty_ycore_void_result e1 =
        staging_ensure_cap(inout_buf, inout_capacity, (uint32_t)total);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, e1, "scene-grid: ensure staging size");

    /* Emit pass — staging is pre-sized so no further reallocs. */
    uint32_t count = num_cells;
    for (uint32_t cell_idx = 0; cell_idx < num_cells; cell_idx++) {
        const struct scene_cell *cell = &grid->cells[cell_idx];
        (*inout_buf)[cell_idx] = count;
        uint32_t count_pos = count++;
        uint32_t emitted = 0;

        for (uint32_t b = 0; b < cell->bucket_count; b++) {
            const struct cell_bucket *bk = &cell->buckets[b];
            if (bk->entity_slot >= entity_base_count) continue; /* defensive */
            uint32_t base = entity_base[bk->entity_slot];
            for (uint32_t i = 0; i < bk->count; i++) {
                (*inout_buf)[count++] = base + bk->local_indices[i];
                emitted++;
            }
        }
        (*inout_buf)[count_pos] = emitted;
    }
    *out_count = count;
    return YETTY_OK_VOID();
}
