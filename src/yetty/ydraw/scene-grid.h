/* scene-grid.h — opaque spatial index for scene-canvas.
 *
 * Module-private header. Only scene-canvas.c and scene-grid.c include
 * this. The grid owns the rows × cols `cell[]` array. Each cell holds a
 * flat list of (entity_slot, local_indices[]) buckets — one bucket per
 * entity that has placed a primitive in this cell.
 *
 * Entities and per-entity prim arenas live in scene-canvas.c. The grid
 * only stores entity_slot + local_idx pairs; it knows nothing about the
 * payload itself.
 */
#ifndef YETTY_YDRAW_SCENE_GRID_H
#define YETTY_YDRAW_SCENE_GRID_H

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_scene_grid;

YETTY_YRESULT_DECLARE(yetty_ydraw_scene_grid_ptr, struct yetty_ydraw_scene_grid *);

/* Called from `insert` for each cell where this `entity_slot` did not
 * yet have a bucket. The canvas records the (row, col) in the entity's
 * touched-cells list. */
typedef struct yetty_ycore_void_result (*yetty_ydraw_scene_grid_fresh_cb)(
    void *user, uint32_t row, uint32_t col);

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ydraw_scene_grid_ptr_result yetty_ydraw_scene_grid_create(void);

void yetty_ydraw_scene_grid_destroy(struct yetty_ydraw_scene_grid *grid);

/* Set the grid dimensions. Wipes existing content (buckets, indices).
 * Pass rows == 0 || cols == 0 to leave the grid empty. */
struct yetty_ycore_void_result yetty_ydraw_scene_grid_set_size(
    struct yetty_ydraw_scene_grid *grid, uint32_t rows, uint32_t cols);

/*===========================================================================
 * Cell mutation
 *===========================================================================*/

/* Insert `local_idx` into every cell overlapping
 * [row_min..row_max] × [col_min..col_max]. `on_fresh` fires for each
 * cell that didn't already have a bucket for this entity_slot. */
struct yetty_ycore_void_result yetty_ydraw_scene_grid_insert(
    struct yetty_ydraw_scene_grid *grid,
    uint32_t entity_slot, uint32_t local_idx,
    uint32_t row_min, uint32_t row_max, uint32_t col_min, uint32_t col_max,
    yetty_ydraw_scene_grid_fresh_cb on_fresh, void *user);

/* Drop the bucket belonging to `entity_slot` from cell (row, col), if
 * any. Idempotent. */
void yetty_ydraw_scene_grid_drop_at(
    struct yetty_ydraw_scene_grid *grid, uint32_t row, uint32_t col,
    uint32_t entity_slot);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_SCENE_GRID_H */
