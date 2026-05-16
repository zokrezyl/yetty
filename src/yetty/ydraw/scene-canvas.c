/* scene-canvas.c — non-scrolling canvas with named entities.
 *
 * See include/yetty/ydraw/scene-canvas.h for the public contract.
 *
 * Layout
 * ------
 *
 * Entities form a tree (parent / children). Each entity owns its own
 * primitive sequence (arena + prims[]) and a touched-cells list (which
 * (row, col) it has placed buckets in). The spatial index — cells with
 * (entity_slot, local_indices[]) buckets — lives behind an opaque
 * scene-grid (see scene-grid.h). The hierarchy never appears inside
 * cells; it lives only in the entities.
 *
 * Delete walks the entity's touched-cells, asks the grid to drop the
 * matching bucket from each, then recurses into children. No grid-wide
 * scan, no AABB recompute at delete time.
 *
 * This file is fully self-contained — it shares NOTHING with
 * scrolling-canvas. Both implementations fill in the same vtable from
 * <yetty/ydraw/canvas.h>; that is the only contact point.
 */

#include "scene-grid.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw/canvas.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw/scene-canvas.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ytrace/ytrace.h>

#define SCENE_ROOT_SLOT       0u
#define SCENE_INVALID_SLOT    UINT32_MAX

/*===========================================================================
 * Internal types
 *===========================================================================*/

struct scene_prim {
    uint32_t arena_offset;
    uint32_t word_count;
    uint32_t type;
};

struct scene_touched_cell {
    uint16_t row;
    uint16_t col;
};

struct yetty_ydraw_scene_entity {
    uint64_t external_id;
    uint32_t slot;
    uint32_t parent_slot;
    bool     in_use;
    uint32_t next_free;

    uint32_t *children;
    uint32_t  children_count;
    uint32_t  children_capacity;

    uint32_t *arena;
    uint32_t  arena_count;
    uint32_t  arena_capacity;

    struct scene_prim *prims;
    uint32_t           prim_count;
    uint32_t           prim_capacity;

    struct scene_touched_cell *touched_cells;
    uint32_t                   touched_cell_count;
    uint32_t                   touched_cell_capacity;
};

struct scene_canvas {
    /* Polymorphic handle MUST be first — vtable methods downcast at offset 0. */
    struct yetty_ydraw_canvas base;

    /* Dimensions + dirty flag. */
    struct yetty_ycore_pixel_size cell_size;
    struct yetty_ycore_grid_size  grid_size;
    bool                          dirty;

    /* GPU staging buffers (render path not yet wired). */
    uint32_t *grid_staging;
    uint32_t  grid_staging_count;
    uint32_t  grid_staging_capacity;
    uint32_t *prim_staging;
    uint32_t  prim_staging_count;
    uint32_t  prim_staging_capacity;

    /* Opaque spatial index. */
    struct yetty_ydraw_scene_grid *grid;

    /* Entity table + free-list. Slot 0 = implicit root. */
    struct yetty_ydraw_scene_entity *entities;
    uint32_t entity_capacity;
    uint32_t free_slot_head;
};

/* Forward decl — vtable is filled in at the bottom of the file. */
static const struct yetty_ydraw_canvas_ops scene_canvas_ops;

static inline struct scene_canvas *as_scene(struct yetty_ydraw_canvas *base)
{
    /* `base` is the FIRST member of struct scene_canvas — a plain cast
     * recovers the variant. */
    return (struct scene_canvas *)base;
}

static inline const struct scene_canvas *as_scene_const(const struct yetty_ydraw_canvas *base)
{
    return (const struct scene_canvas *)base;
}

/*===========================================================================
 * Growable arrays
 *===========================================================================*/

static struct yetty_ycore_void_result grow_u32(uint32_t **arr, uint32_t *cap,
                                               uint32_t need)
{
    if (need <= *cap) return YETTY_OK_VOID();
    uint32_t new_cap = *cap ? *cap * 2 : 8;
    while (new_cap < need) new_cap *= 2;
    uint32_t *grown = realloc(*arr, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: u32 grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grow_prims(struct scene_prim **arr,
                                                 uint32_t *cap, uint32_t need)
{
    if (need <= *cap) return YETTY_OK_VOID();
    uint32_t new_cap = *cap ? *cap * 2 : 8;
    while (new_cap < need) new_cap *= 2;
    struct scene_prim *grown = realloc(*arr, new_cap * sizeof(struct scene_prim));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: prims grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grow_touched(struct scene_touched_cell **arr,
                                                   uint32_t *cap, uint32_t need)
{
    if (need <= *cap) return YETTY_OK_VOID();
    uint32_t new_cap = *cap ? *cap * 2 : 16;
    while (new_cap < need) new_cap *= 2;
    struct scene_touched_cell *grown =
        realloc(*arr, new_cap * sizeof(struct scene_touched_cell));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: touched grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Entity slot allocator
 *===========================================================================*/

static void entity_init_empty(struct yetty_ydraw_scene_entity *e, uint32_t slot)
{
    memset(e, 0, sizeof(*e));
    e->slot        = slot;
    e->parent_slot = SCENE_INVALID_SLOT;
    e->next_free   = SCENE_INVALID_SLOT;
    e->in_use      = false;
}

static struct yetty_ycore_void_result scene_grow_entities(
    struct scene_canvas *sc, uint32_t need)
{
    if (need <= sc->entity_capacity) return YETTY_OK_VOID();
    uint32_t new_cap = sc->entity_capacity ? sc->entity_capacity * 2 : 16;
    while (new_cap < need) new_cap *= 2;
    struct yetty_ydraw_scene_entity *grown =
        realloc(sc->entities, new_cap * sizeof(struct yetty_ydraw_scene_entity));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: entities grow failed");
    }
    sc->entities = grown;
    for (uint32_t i = sc->entity_capacity; i < new_cap; i++) {
        entity_init_empty(&sc->entities[i], i);
    }
    sc->entity_capacity = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_alloc_slot(
    struct scene_canvas *sc, uint32_t *out)
{
    if (sc->free_slot_head != SCENE_INVALID_SLOT) {
        uint32_t slot = sc->free_slot_head;
        sc->free_slot_head = sc->entities[slot].next_free;
        sc->entities[slot].next_free = SCENE_INVALID_SLOT;
        sc->entities[slot].in_use    = true;
        *out = slot;
        return YETTY_OK_VOID();
    }
    uint32_t slot = sc->entity_capacity;
    struct yetty_ycore_void_result gr = scene_grow_entities(sc, slot + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene_alloc_slot: scene_grow_entities");
    sc->entities[slot].in_use = true;
    *out = slot;
    return YETTY_OK_VOID();
}

static void entity_free_storage(struct yetty_ydraw_scene_entity *e)
{
    free(e->arena);
    free(e->prims);
    free(e->touched_cells);
    free(e->children);
    e->arena = NULL;
    e->arena_count = e->arena_capacity = 0;
    e->prims = NULL;
    e->prim_count = e->prim_capacity = 0;
    e->touched_cells = NULL;
    e->touched_cell_count = e->touched_cell_capacity = 0;
    e->children = NULL;
    e->children_count = e->children_capacity = 0;
}

static void scene_release_slot(struct scene_canvas *sc, uint32_t slot)
{
    struct yetty_ydraw_scene_entity *e = &sc->entities[slot];
    entity_free_storage(e);
    e->in_use      = false;
    e->external_id = 0;
    e->parent_slot = SCENE_INVALID_SLOT;
    e->next_free   = sc->free_slot_head;
    sc->free_slot_head = slot;
}

/*===========================================================================
 * Entity clear — detach this entity's buckets from every touched cell.
 *===========================================================================*/

static void entity_clear_in_place(struct scene_canvas *sc,
                                  struct yetty_ydraw_scene_entity *e)
{
    for (uint32_t i = 0; i < e->touched_cell_count; i++) {
        yetty_ydraw_scene_grid_drop_at(sc->grid,
                                       e->touched_cells[i].row,
                                       e->touched_cells[i].col,
                                       e->slot);
    }
    e->touched_cell_count = 0;
    e->arena_count        = 0;
    e->prim_count         = 0;
    sc->dirty             = true;
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ydraw_canvas_ptr_result yetty_ydraw_scene_canvas_create(
    const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: context is NULL");
    }

    struct scene_canvas *sc = calloc(1, sizeof(struct scene_canvas));
    if (!sc) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: alloc failed");
    }
    sc->base.ops       = &scene_canvas_ops;
    sc->dirty          = true;
    sc->free_slot_head = SCENE_INVALID_SLOT;

    /* Reserve slot 0 for the root. */
    struct yetty_ycore_void_result gr = scene_grow_entities(sc, 1);
    if (YETTY_IS_ERR(gr)) {
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "scene-canvas: root reserve failed", gr);
    }
    sc->entities[SCENE_ROOT_SLOT].in_use      = true;
    sc->entities[SCENE_ROOT_SLOT].external_id = 0;
    sc->entities[SCENE_ROOT_SLOT].parent_slot = SCENE_INVALID_SLOT;

    /* Opaque grid (zero-size until set_grid_size). */
    struct yetty_ydraw_scene_grid_ptr_result grid_res = yetty_ydraw_scene_grid_create();
    if (YETTY_IS_ERR(grid_res)) {
        free(sc->entities);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "scene-canvas: grid create failed", grid_res);
    }
    sc->grid = grid_res.value;

    return YETTY_OK(yetty_ydraw_canvas_ptr, &sc->base);
}

static struct yetty_ycore_void_result scene_destroy(
    struct yetty_ydraw_canvas *base)
{
    if (!base) return YETTY_OK_VOID();
    struct scene_canvas *sc = as_scene(base);

    yetty_ydraw_scene_grid_destroy(sc->grid);
    sc->grid = NULL;

    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            entity_free_storage(&sc->entities[i]);
        }
    }
    free(sc->entities);
    free(sc->grid_staging);
    free(sc->prim_staging);
    free(sc);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Config
 *===========================================================================*/

static struct yetty_ycore_void_result scene_set_cell_size(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_pixel_size pixel_size)
{
    if (!base) return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    if (pixel_size.width <= 0.0f || pixel_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cell size must be > 0");
    }
    struct scene_canvas *sc = as_scene(base);
    sc->cell_size = pixel_size;
    sc->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_grid_size(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_size size)
{
    if (!base) return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    struct scene_canvas *sc = as_scene(base);

    /* Resizing wipes content: every cell's bucket layout depends on grid
     * dims. Easier than migrating. */
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            sc->entities[i].arena_count        = 0;
            sc->entities[i].prim_count         = 0;
            sc->entities[i].touched_cell_count = 0;
        }
    }

    sc->grid_size = size;
    struct yetty_ycore_void_result gr = yetty_ydraw_scene_grid_set_size(
        sc->grid, (uint32_t)size.rows, (uint32_t)size.cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: grid set_size");
    sc->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_pixel_size scene_get_cell_size(
    const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->cell_size
                : (struct yetty_ycore_pixel_size){0, 0};
}

static struct yetty_ycore_grid_size scene_get_grid_size(
    const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->grid_size
                : (struct yetty_ycore_grid_size){0, 0};
}

/*===========================================================================
 * Entity API
 *===========================================================================*/

struct yetty_ydraw_scene_entity *yetty_ydraw_scene_canvas_root(
    struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) return NULL;
    return &as_scene(canvas)->entities[SCENE_ROOT_SLOT];
}

struct yetty_ydraw_scene_entity *yetty_ydraw_scene_entity_lookup(
    struct yetty_ydraw_canvas *canvas, uint64_t external_id)
{
    if (!canvas) return NULL;
    struct scene_canvas *sc = as_scene(canvas);
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use && sc->entities[i].external_id == external_id) {
            return &sc->entities[i];
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result entity_push_child(
    struct yetty_ydraw_scene_entity *parent, uint32_t child_slot)
{
    struct yetty_ycore_void_result gr =
        grow_u32(&parent->children, &parent->children_capacity,
                 parent->children_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: push_child");
    parent->children[parent->children_count++] = child_slot;
    return YETTY_OK_VOID();
}

static void entity_remove_child(struct yetty_ydraw_scene_entity *parent,
                                uint32_t child_slot)
{
    for (uint32_t i = 0; i < parent->children_count; i++) {
        if (parent->children[i] == child_slot) {
            if (i + 1 != parent->children_count) {
                parent->children[i] = parent->children[parent->children_count - 1];
            }
            parent->children_count--;
            return;
        }
    }
}

struct yetty_ydraw_scene_entity_ptr_result yetty_ydraw_scene_entity_create(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_scene_entity *parent,
    uint64_t external_id)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr, "scene-canvas: NULL");
    }
    struct scene_canvas *sc = as_scene(canvas);
    if (!parent) parent = &sc->entities[SCENE_ROOT_SLOT];

    if (external_id != 0 &&
        yetty_ydraw_scene_entity_lookup(canvas, external_id) != NULL) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: external_id already in use");
    }

    uint32_t parent_slot = parent->slot;
    uint32_t slot = SCENE_INVALID_SLOT;
    struct yetty_ycore_void_result ar = scene_alloc_slot(sc, &slot);
    if (YETTY_IS_ERR(ar)) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: alloc slot failed", ar);
    }
    /* Re-fetch parent — scene_alloc_slot may have reallocated entities[]. */
    parent = &sc->entities[parent_slot];

    struct yetty_ydraw_scene_entity *e = &sc->entities[slot];
    e->external_id = external_id;
    e->parent_slot = parent_slot;

    struct yetty_ycore_void_result cr = entity_push_child(parent, slot);
    if (YETTY_IS_ERR(cr)) {
        scene_release_slot(sc, slot);
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: link to parent failed", cr);
    }
    return YETTY_OK(yetty_ydraw_scene_entity_ptr, e);
}

/*===========================================================================
 * Insert callback — record touched cell in the entity
 *===========================================================================*/

struct fresh_ctx {
    struct yetty_ydraw_scene_entity *entity;
};

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result on_fresh_cell(void *user, uint32_t row, uint32_t col)
{
    struct fresh_ctx *ctx = user;
    struct yetty_ycore_void_result tr = grow_touched(
        &ctx->entity->touched_cells, &ctx->entity->touched_cell_capacity,
        ctx->entity->touched_cell_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "scene-canvas: touched grow");
    ctx->entity->touched_cells[ctx->entity->touched_cell_count++] =
        (struct scene_touched_cell){
            .row = (uint16_t)row,
            .col = (uint16_t)col,
        };
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Add primitive
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_scene_entity_add_prim(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_scene_entity *entity,
    const struct yetty_ydraw_drawable_flyweight *fw)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to add_prim");
    }
    if (!fw || !fw->data || !fw->ops || !fw->ops->size || !fw->ops->aabb) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: invalid flyweight");
    }
    struct scene_canvas *sc = as_scene(canvas);
    if (sc->cell_size.width <= 0.0f || sc->cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cell size is 0");
    }
    if (sc->grid_size.cols == 0 || sc->grid_size.rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: grid size is 0");
    }

    struct yetty_ycore_size_result sz_res = fw->ops->size(fw->data);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sz_res, "scene-canvas: prim size");
    uint32_t word_count = (uint32_t)(sz_res.value / sizeof(uint32_t));
    if (word_count == 0) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: prim word_count is 0");
    }

    struct rectangle_result aabb_res = fw->ops->aabb(fw->data);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, aabb_res, "scene-canvas: prim aabb");
    struct yetty_ycore_rectangle aabb = aabb_res.value;

    if (aabb.min.y > aabb.max.y) {
        float tmp = aabb.min.y; aabb.min.y = aabb.max.y; aabb.max.y = tmp;
    }

    float    cell_w = sc->cell_size.width;
    float    cell_h = sc->cell_size.height;
    uint32_t cols   = sc->grid_size.cols;
    uint32_t rows   = sc->grid_size.rows;

    if (aabb.max.y < 0.0f || aabb.max.x < 0.0f) return YETTY_OK_VOID();
    if (aabb.min.y >= (float)rows * cell_h ||
        aabb.min.x >= (float)cols * cell_w) {
        return YETTY_OK_VOID();
    }
    if (aabb.min.y < 0.0f) aabb.min.y = 0.0f;
    if (aabb.min.x < 0.0f) aabb.min.x = 0.0f;

    uint32_t row_max = (uint32_t)floorf(aabb.max.y / cell_h);
    if (row_max >= rows) row_max = rows - 1;
    uint32_t row_min = (uint32_t)floorf(aabb.min.y / cell_h);
    if (row_min > row_max) return YETTY_OK_VOID();

    uint32_t col_max = (uint32_t)floorf(aabb.max.x / cell_w);
    if (col_max >= cols) col_max = cols - 1;
    uint32_t col_min = (uint32_t)floorf(aabb.min.x / cell_w);
    if (col_min > col_max) return YETTY_OK_VOID();

    /* Copy payload into entity arena. */
    struct yetty_ycore_void_result gar = grow_u32(
        &entity->arena, &entity->arena_capacity, entity->arena_count + word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gar, "scene-canvas: arena grow");
    uint32_t arena_offset = entity->arena_count;
    memcpy(&entity->arena[arena_offset], fw->data, word_count * sizeof(uint32_t));
    entity->arena_count += word_count;

    /* Append placement record. */
    struct yetty_ycore_void_result gpr = grow_prims(
        &entity->prims, &entity->prim_capacity, entity->prim_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpr, "scene-canvas: prims grow");
    uint32_t local_idx = entity->prim_count;
    entity->prims[local_idx] = (struct scene_prim){
        .arena_offset = arena_offset,
        .word_count   = word_count,
        .type         = fw->data[0],
    };
    entity->prim_count++;

    /* Insert into the grid via opaque API. */
    struct fresh_ctx ctx = {.entity = entity};
    struct yetty_ycore_void_result ir = yetty_ydraw_scene_grid_insert(
        sc->grid, entity->slot, local_idx,
        row_min, row_max, col_min, col_max,
        on_fresh_cell, &ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "scene-canvas: grid_insert");

    sc->dirty = true;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Clear / delete
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_scene_entity_clear(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_scene_entity *entity)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to clear");
    }
    entity_clear_in_place(as_scene(canvas), entity);
    return YETTY_OK_VOID();
}

static void entity_delete_recursive(struct scene_canvas *sc,
                                    struct yetty_ydraw_scene_entity *e)
{
    while (e->children_count > 0) {
        uint32_t child_slot = e->children[e->children_count - 1];
        e->children_count--;
        if (child_slot < sc->entity_capacity && sc->entities[child_slot].in_use) {
            entity_delete_recursive(sc, &sc->entities[child_slot]);
        }
    }
    entity_clear_in_place(sc, e);
    scene_release_slot(sc, e->slot);
}

struct yetty_ycore_void_result yetty_ydraw_scene_entity_delete(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_scene_entity *entity)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to delete");
    }
    if (entity->slot == SCENE_ROOT_SLOT) {
        return YETTY_ERR(yetty_ycore_void,
                         "scene-canvas: cannot delete root (use clear)");
    }
    struct scene_canvas *sc = as_scene(canvas);
    uint32_t parent_slot = entity->parent_slot;
    uint32_t self_slot   = entity->slot;
    entity_delete_recursive(sc, entity);
    if (parent_slot != SCENE_INVALID_SLOT &&
        parent_slot < sc->entity_capacity &&
        sc->entities[parent_slot].in_use) {
        entity_remove_child(&sc->entities[parent_slot], self_slot);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Vtable stubs — staging / OSC / scroll / fonts
 *
 * GPU staging output is not yet wired; rebuild_grid and
 * build_prim_staging produce empty buffers so the polymorphic surface
 * remains callable. Cursor/scroll are no-ops. Font ops return empty
 * since scene-canvas does not yet render glyphs.
 *===========================================================================*/

static struct yetty_ycore_void_result scene_process_input(
    struct yetty_ydraw_canvas *base, struct yetty_ywire_wire_statemachine *sm)
{
    (void)base;
    /* Scene-canvas's real wire-decode (entities + GROUP/DELETE/...) is not
     * yet implemented. Until it lands, drain the envelope so the SM
     * doesn't spin: pull bytes from the SM until it reports end-of-
     * envelope. This runs on the coro spawned by the SM; the read yields
     * back to the SM when no more body bytes are ready right now. */
    uint8_t scratch[4096];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "scene_process_input: read");
        if (rr.value > 0) {
            continue;
        }
        if (yetty_ywire_wire_statemachine_at_end(sm)) {
            return YETTY_OK_VOID();
        }
        yetty_yplatform_coro_yield();
    }
}

static struct yetty_ycore_void_result scene_clear(struct yetty_ydraw_canvas *base)
{
    if (!base) return YETTY_OK_VOID();
    struct scene_canvas *sc = as_scene(base);
    for (uint32_t slot = 1; slot < sc->entity_capacity; slot++) {
        if (sc->entities[slot].in_use) {
            entity_clear_in_place(sc, &sc->entities[slot]);
            scene_release_slot(sc, slot);
        }
    }
    entity_clear_in_place(sc, &sc->entities[SCENE_ROOT_SLOT]);
    sc->entities[SCENE_ROOT_SLOT].children_count = 0;
    sc->dirty = true;
    return YETTY_OK_VOID();
}

static uint32_t scene_primitive_count(const struct yetty_ydraw_canvas *base)
{
    const struct scene_canvas *sc = as_scene_const(base);
    if (!sc) return 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) total += sc->entities[i].prim_count;
    }
    return total;
}

static struct yetty_ycore_void_result scene_rebuild_grid(struct yetty_ydraw_canvas *base)
{
    if (!base) return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    struct scene_canvas *sc = as_scene(base);
    sc->grid_staging_count = 0;
    sc->dirty = false;
    return YETTY_OK_VOID();
}

static const uint32_t *scene_grid_data(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->grid_staging : NULL;
}

static uint32_t scene_grid_word_count(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->grid_staging_count : 0;
}

static struct yetty_ydraw_drawable_staging_result scene_build_prim_staging(
    struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ydraw_drawable_staging, "scene-canvas: NULL");
    }
    struct scene_canvas *sc = as_scene(base);
    sc->prim_staging_count = 0;
    struct yetty_ydraw_drawable_staging view = {
        .data = sc->prim_staging,
        .word_count = 0,
    };
    return YETTY_OK(yetty_ydraw_drawable_staging, view);
}

static uint32_t scene_prim_gpu_size(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->prim_staging_count * (uint32_t)sizeof(uint32_t) : 0;
}

static struct yetty_ycore_void_result scene_mark_dirty(struct yetty_ydraw_canvas *base)
{
    if (!base) return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    as_scene(base)->dirty = true;
    return YETTY_OK_VOID();
}

static bool scene_is_dirty(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->dirty : false;
}

static uint32_t scene_font_count(const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static uint32_t scene_font_generation(const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ydraw_font *scene_get_font_at(
    const struct yetty_ydraw_canvas *base, uint32_t slot)
{
    (void)base; (void)slot;
    return NULL;
}

static struct yetty_ydraw_font *scene_get_default_font(
    const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return NULL;
}

static const struct yetty_ydraw_flyweight_registry *scene_get_flyweight_registry(
    const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return NULL;
}

static struct yetty_ydraw_figure_factory *scene_get_figure_factory(
    const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return NULL;
}

static uint32_t scene_figure_count(const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ydraw_figure_instance *scene_get_figure(
    const struct yetty_ydraw_canvas *base, uint32_t index)
{
    (void)base; (void)index;
    return NULL;
}

static struct yetty_ycore_void_result scene_for_each_glyph(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    (void)base; (void)visitor; (void)user;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Cursor / scroll no-ops
 *===========================================================================*/

static struct yetty_ycore_void_result scene_set_cursor_pos(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_cursor_pos pos)
{
    (void)base; (void)pos;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_scroll_lines(
    struct yetty_ydraw_canvas *base, uint16_t num_lines)
{
    (void)base; (void)num_lines;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_view_top(
    struct yetty_ydraw_canvas *base, bool active, uint32_t view_top)
{
    (void)base; (void)active; (void)view_top;
    return YETTY_OK_VOID();
}

static uint32_t scene_rolling_row_0(struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static uint32_t scene_live_rolling_row_0(struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ycore_void_result scene_set_scroll_callback(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_scroll_callback callback, void *userdata)
{
    (void)base; (void)callback; (void)userdata;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_cursor_callback(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_cursor_callback callback, void *userdata)
{
    (void)base; (void)callback; (void)userdata;
    return YETTY_OK_VOID();
}

static const struct yetty_ydraw_canvas_ops scene_canvas_ops = {
    .name                = "scene",
    .destroy             = scene_destroy,
    .set_cell_size       = scene_set_cell_size,
    .set_grid_size       = scene_set_grid_size,
    .get_cell_size       = scene_get_cell_size,
    .get_grid_size       = scene_get_grid_size,
    .process_input       = scene_process_input,
    .set_cursor_pos      = scene_set_cursor_pos,
    .scroll_lines        = scene_scroll_lines,
    .set_view_top        = scene_set_view_top,
    .rolling_row_0       = scene_rolling_row_0,
    .live_rolling_row_0  = scene_live_rolling_row_0,
    .set_scroll_callback = scene_set_scroll_callback,
    .set_cursor_callback = scene_set_cursor_callback,
    .mark_dirty          = scene_mark_dirty,
    .is_dirty            = scene_is_dirty,
    .rebuild_grid        = scene_rebuild_grid,
    .grid_data           = scene_grid_data,
    .grid_word_count     = scene_grid_word_count,
    .build_prim_staging  = scene_build_prim_staging,
    .prim_gpu_size       = scene_prim_gpu_size,
    .clear               = scene_clear,
    .primitive_count     = scene_primitive_count,
    .font_count          = scene_font_count,
    .font_generation     = scene_font_generation,
    .get_font_at         = scene_get_font_at,
    .get_default_font    = scene_get_default_font,
    .get_flyweight_registry = scene_get_flyweight_registry,
    .get_figure_factory  = scene_get_figure_factory,
    .figure_count        = scene_figure_count,
    .get_figure          = scene_get_figure,
    .for_each_glyph      = scene_for_each_glyph,
};
