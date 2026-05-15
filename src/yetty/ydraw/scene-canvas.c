/* scene-canvas.c — non-scrolling canvas with named entities.
 *
 * See include/yetty/ydraw/scene-canvas.h for the public contract.
 *
 * Layout
 * ------
 *
 * The canvas owns:
 *   - cells[rows*cols]: each cell holds a flat array of buckets. One
 *     bucket per entity that has placed at least one primitive in this
 *     cell. The bucket carries the entity's slot id and the list of
 *     local indices into that entity's prims[].
 *   - entities[]: a flat slot table. Slot 0 is the implicit root.
 *     Recycled via a free-list. The slot id is the cell-side key.
 *
 * Each entity owns its own prim storage (arena + prims[]), a children
 * list (slot ids), and a touched-cells list (which (row,col) it has
 * placed buckets in). The hierarchy lives only in the entities — never
 * in the cells.
 *
 * Delete walks the entity's touched-cells, drops the matching bucket
 * from each cell, then recurses into children. No grid-wide scan, no
 * AABB recompute at delete time.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "canvas-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/complex-prim-types.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw/scene-canvas.h>
#include <yetty/ytrace/ytrace.h>

#define SCENE_ROOT_SLOT       0u
#define SCENE_INVALID_SLOT    UINT32_MAX

/*===========================================================================
 * Internal types
 *===========================================================================*/

/* One primitive's placement record. Payload lives at
 * entity->arena[arena_offset .. arena_offset + word_count). */
struct scene_prim {
    uint32_t arena_offset;
    uint32_t word_count;
    uint32_t type;            /* arena[arena_offset]; cached for fast dispatch */
};

/* A per-entity bucket inside a cell. Owns its local_indices array. */
struct scene_cell_bucket {
    uint32_t  entity_slot;
    uint32_t *local_indices;
    uint32_t  count;
    uint32_t  capacity;
};

/* One grid cell: unsorted flat array of buckets. */
struct scene_cell {
    struct scene_cell_bucket *buckets;
    uint32_t bucket_count;
    uint32_t bucket_capacity;
};

/* A cell coordinate touched by an entity. Recorded at insertion time.
 * May contain duplicates — clear/delete is idempotent against them. */
struct scene_touched_cell {
    uint16_t row;
    uint16_t col;
};

/* Entity. Slot 0 is the implicit root and is always in_use. */
struct yetty_ydraw_scene_entity {
    uint64_t external_id;
    uint32_t slot;            /* this entity's own slot */
    uint32_t parent_slot;     /* SCENE_INVALID_SLOT only for root */
    bool     in_use;

    /* Free-list link when !in_use (next free slot, or SCENE_INVALID_SLOT). */
    uint32_t next_free;

    /* Children — slot ids. Order is insertion order. */
    uint32_t *children;
    uint32_t  children_count;
    uint32_t  children_capacity;

    /* Primitive payload arena. */
    uint32_t *arena;
    uint32_t  arena_count;
    uint32_t  arena_capacity;

    /* Per-prim placement records. */
    struct scene_prim *prims;
    uint32_t           prim_count;
    uint32_t           prim_capacity;

    /* Cells where this entity has placed at least one bucket. */
    struct scene_touched_cell *touched_cells;
    uint32_t                   touched_cell_count;
    uint32_t                   touched_cell_capacity;
};

struct yetty_ydraw_scene_canvas {
    struct yetty_ydraw_canvas *base;

    /* Grid: row-major, cell_count = rows * cols. */
    struct scene_cell *cells;
    uint32_t           cell_count;

    /* Entity table + free-list. */
    struct yetty_ydraw_scene_entity *entities;
    uint32_t entity_capacity;
    uint32_t free_slot_head;   /* SCENE_INVALID_SLOT = empty */
};

/* Forward decls. */
static const struct yetty_ydraw_canvas_ops scene_canvas_ops;

/*===========================================================================
 * Small helpers — growable arrays
 *===========================================================================*/

static struct yetty_ycore_void_result grow_u32(uint32_t **arr, uint32_t *cap,
                                               uint32_t need)
{
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
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
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
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

static struct yetty_ycore_void_result grow_buckets(struct scene_cell_bucket **arr,
                                                   uint32_t *cap, uint32_t need)
{
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = *cap ? *cap * 2 : 4;
    while (new_cap < need) new_cap *= 2;
    struct scene_cell_bucket *grown =
        realloc(*arr, new_cap * sizeof(struct scene_cell_bucket));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: buckets grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grow_touched(struct scene_touched_cell **arr,
                                                   uint32_t *cap, uint32_t need)
{
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
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
 * Cell <-> entity bucket plumbing
 *===========================================================================*/

/* Find the bucket belonging to `slot` in this cell, or NULL. */
static struct scene_cell_bucket *cell_find_bucket(struct scene_cell *cell,
                                                  uint32_t slot)
{
    for (uint32_t i = 0; i < cell->bucket_count; i++) {
        if (cell->buckets[i].entity_slot == slot) {
            return &cell->buckets[i];
        }
    }
    return NULL;
}

/* Get-or-create the bucket for `slot` in this cell. */
static struct scene_cell_bucket *cell_ensure_bucket(struct scene_cell *cell,
                                                    uint32_t slot)
{
    struct scene_cell_bucket *b = cell_find_bucket(cell, slot);
    if (b) return b;
    struct yetty_ycore_void_result gr =
        grow_buckets(&cell->buckets, &cell->bucket_capacity, cell->bucket_count + 1);
    if (YETTY_IS_ERR(gr)) {
        yetty_ycore_error_destroy(gr.error);
        return NULL;
    }
    b = &cell->buckets[cell->bucket_count++];
    b->entity_slot   = slot;
    b->local_indices = NULL;
    b->count         = 0;
    b->capacity      = 0;
    return b;
}

/* Append `local_idx` to the bucket. */
static struct yetty_ycore_void_result bucket_push_index(struct scene_cell_bucket *b,
                                                        uint32_t local_idx)
{
    struct yetty_ycore_void_result gr =
        grow_u32(&b->local_indices, &b->capacity, b->count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: bucket push");
    b->local_indices[b->count++] = local_idx;
    return YETTY_OK_VOID();
}

/* Swap-remove a bucket from a cell, freeing its index list. */
static void cell_drop_bucket_at(struct scene_cell *cell, uint32_t idx)
{
    struct scene_cell_bucket *b = &cell->buckets[idx];
    free(b->local_indices);
    if (idx + 1 != cell->bucket_count) {
        cell->buckets[idx] = cell->buckets[cell->bucket_count - 1];
    }
    cell->bucket_count--;
}

/* Drop the bucket for `slot` from this cell, if present. */
static void cell_drop_slot(struct scene_cell *cell, uint32_t slot)
{
    for (uint32_t i = 0; i < cell->bucket_count; i++) {
        if (cell->buckets[i].entity_slot == slot) {
            cell_drop_bucket_at(cell, i);
            return;
        }
    }
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
    struct yetty_ydraw_scene_canvas *sc, uint32_t need)
{
    if (need <= sc->entity_capacity) {
        return YETTY_OK_VOID();
    }
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

/* Allocate a fresh slot. Returns SCENE_INVALID_SLOT on alloc failure. */
static uint32_t scene_alloc_slot(struct yetty_ydraw_scene_canvas *sc)
{
    if (sc->free_slot_head != SCENE_INVALID_SLOT) {
        uint32_t slot = sc->free_slot_head;
        sc->free_slot_head = sc->entities[slot].next_free;
        sc->entities[slot].next_free = SCENE_INVALID_SLOT;
        sc->entities[slot].in_use    = true;
        return slot;
    }
    uint32_t slot = sc->entity_capacity;
    struct yetty_ycore_void_result gr = scene_grow_entities(sc, slot + 1);
    if (YETTY_IS_ERR(gr)) {
        yetty_ycore_error_destroy(gr.error);
        return SCENE_INVALID_SLOT;
    }
    sc->entities[slot].in_use = true;
    return slot;
}

/* Free entity storage in place (does not return slot to free-list). */
static void entity_free_storage(struct yetty_ydraw_scene_entity *e)
{
    /* Index lists in the cells were freed via cell_drop_bucket_at by the
     * caller before this point. We only own arena/prims/touched/children
     * here. */
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

/* Push `slot` onto the free-list. */
static void scene_release_slot(struct yetty_ydraw_scene_canvas *sc, uint32_t slot)
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

static void entity_clear_in_place(struct yetty_ydraw_scene_canvas *sc,
                                  struct yetty_ydraw_scene_entity *e)
{
    uint32_t cols = sc->base->grid_size.cols;
    for (uint32_t i = 0; i < e->touched_cell_count; i++) {
        uint32_t row = e->touched_cells[i].row;
        uint32_t col = e->touched_cells[i].col;
        uint32_t idx = row * cols + col;
        if (idx >= sc->cell_count) continue;
        cell_drop_slot(&sc->cells[idx], e->slot);
    }
    e->touched_cell_count = 0;
    e->arena_count        = 0;
    e->prim_count         = 0;
    sc->base->dirty       = true;
}

/*===========================================================================
 * Grid lifecycle
 *===========================================================================*/

static void scene_free_cells(struct yetty_ydraw_scene_canvas *sc)
{
    if (!sc->cells) return;
    for (uint32_t i = 0; i < sc->cell_count; i++) {
        struct scene_cell *c = &sc->cells[i];
        for (uint32_t b = 0; b < c->bucket_count; b++) {
            free(c->buckets[b].local_indices);
        }
        free(c->buckets);
    }
    free(sc->cells);
    sc->cells = NULL;
    sc->cell_count = 0;
}

/*===========================================================================
 * Canvas lifecycle
 *===========================================================================*/

struct yetty_ydraw_canvas_ptr_result yetty_ydraw_scene_canvas_create(
    const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: context is NULL");
    }

    struct yetty_ydraw_scene_canvas *sc =
        calloc(1, sizeof(struct yetty_ydraw_scene_canvas));
    if (!sc) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: alloc failed");
    }
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

    struct yetty_ydraw_canvas_ptr_result base_res =
        ydraw_canvas_create(context, &scene_canvas_ops, sc);
    if (YETTY_IS_ERR(base_res)) {
        free(sc->entities);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "scene-canvas: base create failed", base_res);
    }
    sc->base = base_res.value;
    return YETTY_OK(yetty_ydraw_canvas_ptr, base_res.value);
}

static struct yetty_ycore_void_result scene_destroy_impl(
    struct yetty_ydraw_canvas *base)
{
    struct yetty_ydraw_scene_canvas *sc = base->impl;
    if (!sc) return YETTY_OK_VOID();

    /* Drop every cell's buckets first (frees index lists). */
    scene_free_cells(sc);

    /* Then free every entity's owned storage (arenas, prims, touched, children). */
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            entity_free_storage(&sc->entities[i]);
        }
    }
    free(sc->entities);
    free(sc);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_grid_size_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_size size)
{
    struct yetty_ydraw_scene_canvas *sc = base->impl;
    if (!sc) return YETTY_ERR(yetty_ycore_void, "scene-canvas: impl is NULL");

    /* Resizing wipes content: every cell's bucket layout depends on grid
     * dims. Easier than migrating. Caller can re-emit. */
    scene_free_cells(sc);
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            sc->entities[i].arena_count        = 0;
            sc->entities[i].prim_count         = 0;
            sc->entities[i].touched_cell_count = 0;
        }
    }

    base->grid_size = size;
    uint32_t total = (uint32_t)size.rows * (uint32_t)size.cols;
    if (total == 0) {
        base->dirty = true;
        return YETTY_OK_VOID();
    }

    sc->cells = calloc(total, sizeof(struct scene_cell));
    if (!sc->cells) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cells alloc failed");
    }
    sc->cell_count = total;
    base->dirty = true;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Entity API
 *===========================================================================*/

struct yetty_ydraw_scene_entity *yetty_ydraw_scene_canvas_root(
    struct yetty_ydraw_canvas *canvas)
{
    if (!canvas || !canvas->impl) return NULL;
    struct yetty_ydraw_scene_canvas *sc = canvas->impl;
    return &sc->entities[SCENE_ROOT_SLOT];
}

struct yetty_ydraw_scene_entity *yetty_ydraw_scene_entity_lookup(
    struct yetty_ydraw_canvas *canvas, uint64_t external_id)
{
    if (!canvas || !canvas->impl) return NULL;
    struct yetty_ydraw_scene_canvas *sc = canvas->impl;
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
    if (!canvas || !canvas->impl) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: canvas is NULL");
    }
    struct yetty_ydraw_scene_canvas *sc = canvas->impl;
    if (!parent) parent = &sc->entities[SCENE_ROOT_SLOT];

    if (external_id != 0 &&
        yetty_ydraw_scene_entity_lookup(canvas, external_id) != NULL) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: external_id already in use");
    }

    uint32_t parent_slot = parent->slot;
    uint32_t slot = scene_alloc_slot(sc);
    if (slot == SCENE_INVALID_SLOT) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: out of entity slots");
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
 * Add a primitive to an entity.
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_scene_entity_add_prim(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_scene_entity *entity,
    const struct yetty_ydraw_core_prim_flyweight *fw)
{
    if (!canvas || !canvas->impl || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to add_prim");
    }
    if (!fw || !fw->data || !fw->ops || !fw->ops->size || !fw->ops->aabb) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: invalid flyweight");
    }
    struct yetty_ydraw_scene_canvas *sc = canvas->impl;
    struct yetty_ydraw_canvas *base = sc->base;

    if (base->cell_size.width <= 0.0f || base->cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cell size is 0");
    }
    if (base->grid_size.cols == 0 || base->grid_size.rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: grid size is 0");
    }

    /* Size + AABB via the flyweight's vtable. */
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

    float    cell_w   = base->cell_size.width;
    float    cell_h   = base->cell_size.height;
    uint32_t cols     = base->grid_size.cols;
    uint32_t rows     = base->grid_size.rows;

    /* Drop fully out-of-grid prims. */
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
    struct yetty_ycore_void_result gar =
        grow_u32(&entity->arena, &entity->arena_capacity,
                 entity->arena_count + word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gar, "scene-canvas: arena grow");
    uint32_t arena_offset = entity->arena_count;
    memcpy(&entity->arena[arena_offset], fw->data, word_count * sizeof(uint32_t));
    entity->arena_count += word_count;

    /* Append placement record. */
    struct yetty_ycore_void_result gpr =
        grow_prims(&entity->prims, &entity->prim_capacity, entity->prim_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpr, "scene-canvas: prims grow");
    uint32_t local_idx = entity->prim_count;
    entity->prims[local_idx] = (struct scene_prim){
        .arena_offset = arena_offset,
        .word_count   = word_count,
        .type         = fw->data[0],
    };
    entity->prim_count++;

    /* Insert this prim's local index into every overlapping cell. */
    for (uint32_t r = row_min; r <= row_max; r++) {
        for (uint32_t c = col_min; c <= col_max; c++) {
            struct scene_cell *cell = &sc->cells[r * cols + c];
            bool fresh = (cell_find_bucket(cell, entity->slot) == NULL);
            struct scene_cell_bucket *b = cell_ensure_bucket(cell, entity->slot);
            if (!b) {
                return YETTY_ERR(yetty_ycore_void,
                                 "scene-canvas: cell bucket alloc failed");
            }
            struct yetty_ycore_void_result pr = bucket_push_index(b, local_idx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "scene-canvas: bucket push");

            if (fresh) {
                struct yetty_ycore_void_result tr = grow_touched(
                    &entity->touched_cells, &entity->touched_cell_capacity,
                    entity->touched_cell_count + 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, tr,
                                    "scene-canvas: touched grow");
                entity->touched_cells[entity->touched_cell_count++] =
                    (struct scene_touched_cell){
                        .row = (uint16_t)r,
                        .col = (uint16_t)c,
                    };
            }
        }
    }

    base->dirty = true;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Clear / delete
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_scene_entity_clear(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_scene_entity *entity)
{
    if (!canvas || !canvas->impl || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to clear");
    }
    entity_clear_in_place(canvas->impl, entity);
    return YETTY_OK_VOID();
}

static void entity_delete_recursive(struct yetty_ydraw_scene_canvas *sc,
                                    struct yetty_ydraw_scene_entity *e)
{
    /* Snapshot children list because scene_release_slot frees it. */
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
    if (!canvas || !canvas->impl || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to delete");
    }
    if (entity->slot == SCENE_ROOT_SLOT) {
        return YETTY_ERR(yetty_ycore_void,
                         "scene-canvas: cannot delete root (use clear)");
    }
    struct yetty_ydraw_scene_canvas *sc = canvas->impl;
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
 * Vtable stubs — staging / OSC / scroll
 *
 * GPU staging output is not yet wired; rebuild_grid and
 * build_prim_staging produce empty buffers so the polymorphic surface
 * remains callable without crashing. The render path will be filled in
 * once the data model is reviewed.
 *===========================================================================*/

static struct yetty_ycore_void_result scene_process_input_impl(
    struct yetty_ydraw_canvas *base, struct yetty_yterm_osc_statemachine *sm)
{
    (void)base;
    (void)sm;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_clear_impl(
    struct yetty_ydraw_canvas *base)
{
    struct yetty_ydraw_scene_canvas *sc = base->impl;
    if (!sc) return YETTY_OK_VOID();

    /* Drop every non-root entity. Walk the table directly so we don't
     * depend on the children list being well-formed. */
    for (uint32_t slot = 1; slot < sc->entity_capacity; slot++) {
        if (sc->entities[slot].in_use) {
            entity_clear_in_place(sc, &sc->entities[slot]);
            scene_release_slot(sc, slot);
        }
    }
    /* And clear the root in place. */
    entity_clear_in_place(sc, &sc->entities[SCENE_ROOT_SLOT]);
    sc->entities[SCENE_ROOT_SLOT].children_count = 0;
    base->dirty = true;
    return YETTY_OK_VOID();
}

static uint32_t scene_primitive_count_impl(const struct yetty_ydraw_canvas *base)
{
    const struct yetty_ydraw_scene_canvas *sc = base->impl;
    if (!sc) return 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) total += sc->entities[i].prim_count;
    }
    return total;
}

static struct yetty_ycore_void_result scene_rebuild_grid_impl(
    struct yetty_ydraw_canvas *base)
{
    /* Empty grid staging until the render path is wired. */
    base->grid_staging_count = 0;
    base->dirty = false;
    return YETTY_OK_VOID();
}

static struct yetty_ydraw_prim_staging_result scene_build_prim_staging_impl(
    struct yetty_ydraw_canvas *base)
{
    base->prim_staging_count = 0;
    struct yetty_ydraw_prim_staging view = {
        .data = base->prim_staging,
        .word_count = 0,
    };
    return YETTY_OK(yetty_ydraw_prim_staging, view);
}

static uint32_t scene_prim_gpu_size_impl(const struct yetty_ydraw_canvas *base)
{
    return base ? base->prim_staging_count * (uint32_t)sizeof(uint32_t) : 0;
}

static uint32_t scene_complex_prim_count_impl(const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ydraw_core_complex_prim_instance *scene_get_complex_prim_impl(
    const struct yetty_ydraw_canvas *base, uint32_t index)
{
    (void)base;
    (void)index;
    return NULL;
}

static void scene_for_each_glyph_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    (void)base;
    (void)visitor;
    (void)user;
}

static struct yetty_ycore_void_result scene_set_cursor_pos_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_cursor_pos pos)
{
    (void)base; (void)pos;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_scroll_lines_impl(
    struct yetty_ydraw_canvas *base, uint16_t num_lines)
{
    (void)base; (void)num_lines;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_view_top_impl(
    struct yetty_ydraw_canvas *base, bool active, uint32_t view_top)
{
    (void)base; (void)active; (void)view_top;
    return YETTY_OK_VOID();
}

static uint32_t scene_rolling_row_0_impl(struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static uint32_t scene_live_rolling_row_0_impl(struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ycore_void_result scene_set_scroll_callback_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_scroll_callback callback, void *userdata)
{
    (void)base; (void)callback; (void)userdata;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_cursor_callback_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_cursor_callback callback, void *userdata)
{
    (void)base; (void)callback; (void)userdata;
    return YETTY_OK_VOID();
}

static const struct yetty_ydraw_canvas_ops scene_canvas_ops = {
    .name                = "scene",
    .destroy             = scene_destroy_impl,
    .set_grid_size       = scene_set_grid_size_impl,
    .process_input       = scene_process_input_impl,
    .clear               = scene_clear_impl,
    .primitive_count     = scene_primitive_count_impl,
    .rebuild_grid        = scene_rebuild_grid_impl,
    .build_prim_staging  = scene_build_prim_staging_impl,
    .prim_gpu_size       = scene_prim_gpu_size_impl,
    .complex_prim_count  = scene_complex_prim_count_impl,
    .get_complex_prim    = scene_get_complex_prim_impl,
    .for_each_glyph      = scene_for_each_glyph_impl,
    .set_cursor_pos      = scene_set_cursor_pos_impl,
    .scroll_lines        = scene_scroll_lines_impl,
    .set_view_top        = scene_set_view_top_impl,
    .rolling_row_0       = scene_rolling_row_0_impl,
    .live_rolling_row_0  = scene_live_rolling_row_0_impl,
    .set_scroll_callback = scene_set_scroll_callback_impl,
    .set_cursor_callback = scene_set_cursor_callback_impl,
};
