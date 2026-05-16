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

static struct cell_bucket *cell_find_bucket(struct scene_cell *cell,
                                            uint32_t entity_slot)
{
    for (uint32_t i = 0; i < cell->bucket_count; i++) {
        if (cell->buckets[i].entity_slot == entity_slot) {
            return &cell->buckets[i];
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result cell_ensure_bucket(
    struct scene_cell *cell, uint32_t entity_slot, struct cell_bucket **out)
{
    struct cell_bucket *b = cell_find_bucket(cell, entity_slot);
    if (b) {
        *out = b;
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result gr =
        grow_buckets(&cell->buckets, &cell->bucket_capacity, cell->bucket_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "cell_ensure_bucket: grow");
    b = &cell->buckets[cell->bucket_count++];
    b->entity_slot   = entity_slot;
    b->local_indices = NULL;
    b->count         = 0;
    b->capacity      = 0;
    *out = b;
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
    if (idx + 1 != cell->bucket_count) {
        cell->buckets[idx] = cell->buckets[cell->bucket_count - 1];
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

    for (uint32_t r = row_min; r <= row_max; r++) {
        for (uint32_t c = col_min; c <= col_max; c++) {
            struct scene_cell *cell = &grid->cells[r * grid->cols + c];
            bool fresh = (cell_find_bucket(cell, entity_slot) == NULL);
            struct cell_bucket *b = NULL;
            struct yetty_ycore_void_result br = cell_ensure_bucket(cell, entity_slot, &b);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "scene-grid: ensure_bucket");
            struct yetty_ycore_void_result pr = bucket_push_index(b, local_idx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "scene-grid: push_index");
            if (fresh && on_fresh) {
                struct yetty_ycore_void_result fr = on_fresh(user, r, c);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "scene-grid: on_fresh");
            }
        }
    }
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
