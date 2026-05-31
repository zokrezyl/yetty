/*
 * ygrid — figure kind: spatial-bucketed batch of SDF primitives.
 *
 * Standalone implementation: owns its prim storage, spatial bucketing,
 * GPU pipeline, binder, and inline shader. Depends only on foundational
 * modules:
 *   ycore        — Result + buffer + rectangle types
 *   yfigure      — figure base type
 *   yrender      — pipeline + binder + resource set machinery
 *   ydraw-core   — flyweight registry (wire-format parsing primitives)
 *   ysdf         — SDF handler (size + aabb) and ysdf.gen.wgsl (SDF math)
 *
 * No coupling to scene-canvas / scrolling-canvas / ydraw-layer. Those
 * modules are kept alive only for backward compatibility while the
 * compositor migration is in flight; they will be retired once the new
 * stack is complete.
 */

#include <yetty/ygrid/ygrid.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/yconfig/config.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ysdf/handler.h>
#include <yetty/yfont/font.h>
#include <yetty/yrender/font-dispatcher.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender/types.h>
#include <yetty/ytrace/ytrace.h>
/* yclass types referenced by the override impls at the foot of this
 * file. The codegen-generated grid.gen.c at the foot needs both the
 * class machinery (yclass_ctx, yclass_object) and the per-class
 * accessor/typedef declarations. */
#include <yclass/class.h>
#include <yetty/ygrid/grid.h>
#include <yetty/yetty/yetty.h>

/* GLYPH primitive type — matches ydraw-layer.wgsl's YDRAW_SDF_GLYPH. */
#define YGRID_GLYPH_TYPE 200u

/* Font dispatcher generation lives below the struct definition. */

/*===========================================================================
 * Per-prim metadata (parsed once from wire bytes at add time)
 *=========================================================================*/

struct ygrid_prim_meta {
    /* Offset in `bytes[]` to the wire record's TYPE+PAYLOAD_SIZE header
     * (the FAM `[type|payload_size|bytes...]` block). */
    uint32_t record_offset;
    /* Offset (within `bytes[]`) to the prim's first payload word — what
     * evaluate_sdf_2d expects to receive as drawable_offset (after rebase
     * into the prim_staging buffer). I.e. `record_offset + 8` bytes. */
    uint32_t prim_payload_offset;
    /* Prim payload size in u32 words. */
    uint32_t prim_payload_words;
    uint32_t type;
    /* AABB in figure-local pixel coords. */
    float min_x, min_y, max_x, max_y;
    /* Entity that owns this prim (slot index into ygrid->entities[]).
     * Root entity = slot 0 = SCENE_ROOT_SLOT. When an entity is deleted
     * its prims are tombstoned by setting entity_slot = UINT32_MAX; the
     * staging rebuild skips tombstoned prims. */
    uint32_t entity_slot;
};

/*===========================================================================
 * Per-cell bucket (list of prim indices into the prims[] array)
 *=========================================================================*/

struct ygrid_cell {
    uint32_t *indices;
    uint32_t count;
    uint32_t cap;
};

/*===========================================================================
 * Entity tree — ports scene-canvas's grouping model into ygrid.
 *
 * One ygrid figure now holds a tree of named entities (== ygui widgets),
 * each owning a list of prim indices into the shared bytes / prims
 * arrays. CMD_GROUP(id, payload) opens or re-opens an entity scope and
 * recurses into the payload. CMD_DELETE(id) drops the entity and its
 * prims. CMD_ZERO clears the whole ygrid. Plain ADD records land in
 * the entity whose scope is currently open (root if no enclosing
 * CMD_GROUP). This lets one ygrid hold every widget's prims for a
 * whole window — no more "one ygrid figure per widget".
 *=========================================================================*/

#define YGRID_ROOT_SLOT 0u
#define YGRID_INVALID_SLOT UINT32_MAX

struct ygrid_entity {
    uint64_t external_id;
    uint32_t slot;
    uint32_t parent_slot;
    bool in_use;
    uint32_t next_free;

    /* Direct children of this entity (subtree links). */
    uint32_t *children;
    uint32_t children_count;
    uint32_t children_capacity;

    /* Prim indices into ygrid->prims[]. Deleted by entity_clear /
     * entity_delete via tombstoning in g->prims[]. */
    uint32_t *prim_indices;
    uint32_t prim_count;
    uint32_t prim_capacity;
};

/* Open-addressing hash entry: external_id → slot. external_id == 0 is
 * the root sentinel (not stored); UINT64_MAX is the tombstone. */
struct ygrid_id_index_entry {
    uint64_t external_id;
    uint32_t slot;
};

#define YGRID_ID_INDEX_EMPTY 0u
#define YGRID_ID_INDEX_TOMBSTONE UINT64_MAX

/*===========================================================================
 * Uniform layout
 *=========================================================================*/

/* Mirrors ydraw-layer.c — same order, same names, same shader. */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_ROLLING_ROW_0 2
#define U_PRIM_COUNT 3
#define U_VZ_SCALE 4
#define U_VZ_OFF 5
#define U_CZ_SCALE 6
#define U_CZ_OFF 7
/* On-screen rect size in px. The vertex maps the rect's NDC quad onto a
 * rect-sized window of the (possibly larger) content; cells + bounds use
 * grid_size*cell_size = the content extent. When content == rect this is
 * the same value, so non-scrolling figures are unaffected. */
#define U_VIEW_SIZE 8
#define U_COUNT 9

/*===========================================================================
 * The figure
 *=========================================================================*/

struct [[clang::annotate("class@ygrid:grid")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_ygrid_grid {
    struct yetty_yfigure_figure base;

    /* Owned. Built at create time. Used by process_input to walk the
     * routed-record payload as a stream of SDF/glyph/TEXT_SPAN records
     * and feed each one into the ygrid's flat byte buffer. */
    struct yetty_ydraw_flyweight_registry *registry;

    uint32_t grid_cols;
    uint32_t grid_rows;

    /* Content extent in px. The cell grid + prim bucketing span this, not
     * the on-screen rect — so content can be taller/wider than the
     * viewport. 0 means "same as the rect" (the figure fills itself; the
     * common, non-scrolling case). */
    float content_w;
    float content_h;
    /* Scroll offset in px: the content coordinate shown at the rect's
     * top-left. The shader maps the rect to the content window starting
     * here; the per-figure scissor clips. 0 = top-left. */
    float scroll_x;
    float scroll_y;
    /* HiDPI scale = framebuffer px / logical px, captured from the host's
     * gpu context at create time (1.0 on non-HiDPI, and on the headless
     * test path). Producers (ygui chrome, …) author and ship their wire
     * envelope in display-independent LOGICAL pixels; this receiver
     * multiplies every incoming coordinate by content_scale at
     * add-record time so the same envelope renders at the correct
     * physical size on whatever display it lands on. A remote receiver
     * applies its OWN display's scale to the identical envelope. */
    float content_scale;
    /* Coordinate mode. When set, prim coords are ABSOLUTE screen pixels
     * (the chrome grid + ygui figures via make_figure, whose subtrees are
     * laid out in absolute coords): the grid spans the whole target and
     * clips to its own rect with the GPU scissor, so a sub-rect figure
     * renders its absolute-coord content clipped to its box — no
     * re-origin, so layout / hit-test / paint need no special-casing.
     * When clear, coords are LOCAL to the figure rect (producer figures:
     * yimage/yplot/… draw from 0,0). Set by the factory per kind. */
    int absolute_coords;

    /* Wire bytes — concatenated records, copied verbatim from
     * yetty_ygrid_add_record callers. */
    uint8_t *bytes;
    size_t bytes_len;
    size_t bytes_cap;

    struct ygrid_prim_meta *prims;
    uint32_t prim_count;
    uint32_t prim_cap;

    /* grid_cols * grid_rows cells. NULL until first set_size. */
    struct ygrid_cell *cells;

    /* GPU */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Staging buffers — rebuilt when `staging_dirty`. */
    uint32_t *grid_staging;
    size_t grid_staging_words;
    size_t grid_staging_cap;
    uint32_t *prim_staging;
    size_t prim_staging_words;
    size_t prim_staging_cap;

    /* Resource set + child SDF lib */
    struct yetty_ydraw_gpu_resource_set rs;
    struct yetty_ydraw_gpu_resource_set sdf_lib_rs;
    struct yetty_ycore_buffer sdf_lib_code;
    /* ydraw-layer.wgsl raw bytes, loaded from paths/shaders. Combined
     * shader = stub font dispatcher + this file. */
    struct yetty_ycore_buffer layer_shader_code;
    char *combined_shader;
    size_t combined_shader_size;

    /* Own-pipeline binder — flattens the rs tree, computes buffer
     * offsets, compiles the shader with those offsets baked in, and
     * uploads to those same offsets. One source of truth: any structural
     * change (size growth past a power-of-2 cap, etc.) triggers a
     * refinalize that re-derives offsets AND recompiles the shader. */
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;

    /* Font slots. Slot 0 is the default font (font_id=0 in GLYPH wire
     * payload). Pointers are borrowed; ygrid does not destroy.
     *
     * font_generation bumps on every set_font call; last_emitted_gen
     * tracks the value the dispatcher was last regenerated for. The
     * dispatcher rebuild path triggers a shader-hash change which makes
     * the binder refinalize on the next update(). */
    struct yetty_ydraw_font *fonts[YETTY_YRENDER_RS_MAX_CHILDREN - 1];
    uint32_t font_count;
    uint32_t font_generation;
    uint32_t last_emitted_font_generation;

    int staging_dirty;

    /* Complex-prim support — borrowed factory pointer (lifetime owned
     * by the host: terminal / yui registers types yplot/yimage/yvideo/
     * yzoo/yjungle once, then hands the same pointer to every ygrid
     * via the factory args bundle). Each instance lives until the
     * next clear() / destroy(). */
    struct yetty_ydraw_raw_figure_factory *figure_factory;
    struct yetty_ydraw_figure **figure_instances;
    uint32_t figure_instance_count;
    uint32_t figure_instance_cap;

    /* Entity table — slot 0 is the implicit root, allocated at create
     * time. entity_high_water is one past the highest slot ever issued
     * (free list reuses released slots before bumping the mark);
     * entity_capacity is the physical array size. id_index is an open-
     * addressing hash from external_id → slot for O(1) lookup. */
    struct ygrid_entity *entities;
    uint32_t entity_capacity;
    uint32_t entity_high_water;
    uint32_t free_slot_head;
    struct ygrid_id_index_entry *id_index;
    uint32_t id_index_capacity;
    uint32_t id_index_count;

    /* Scratch: the entity slot currently in scope during a
     * process_bytes recursion. Set by process_group_body when it
     * enters a CMD_GROUP scope, read by parse_and_index_record when
     * it attaches a newly-parsed prim to its owning entity. Defaults
     * to YGRID_ROOT_SLOT so plain (no-CMD_GROUP) traffic still works. */
    uint32_t current_entity_slot;
};

/*===========================================================================
 * Entity helpers — ported from scene-canvas (commit 39adaca).
 *=========================================================================*/

static void entity_init_empty(struct ygrid_entity *e, uint32_t slot)
{
    memset(e, 0, sizeof(*e));
    e->slot = slot;
    e->parent_slot = YGRID_INVALID_SLOT;
    e->next_free = YGRID_INVALID_SLOT;
    e->in_use = false;
}

static void entity_free_storage(struct ygrid_entity *e)
{
    free(e->children);
    free(e->prim_indices);
    e->children = NULL;
    e->children_count = e->children_capacity = 0;
    e->prim_indices = NULL;
    e->prim_count = e->prim_capacity = 0;
}

static struct yetty_ycore_void_result entities_grow(struct yetty_ygrid_grid *g, uint32_t need)
{
    if (need <= g->entity_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = g->entity_capacity ? g->entity_capacity * 2u : 16u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    struct ygrid_entity *grown =
        (struct ygrid_entity *)realloc(g->entities, new_cap * sizeof(struct ygrid_entity));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: entities grow failed");
    }
    g->entities = grown;
    for (uint32_t i = g->entity_capacity; i < new_cap; i++) {
        entity_init_empty(&g->entities[i], i);
    }
    g->entity_capacity = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result entity_alloc_slot(struct yetty_ygrid_grid *g, uint32_t *out)
{
    if (g->free_slot_head != YGRID_INVALID_SLOT) {
        uint32_t slot = g->free_slot_head;
        g->free_slot_head = g->entities[slot].next_free;
        g->entities[slot].next_free = YGRID_INVALID_SLOT;
        g->entities[slot].in_use = true;
        *out = slot;
        return YETTY_OK_VOID();
    }
    uint32_t slot = g->entity_high_water;
    if (slot >= g->entity_capacity) {
        struct yetty_ycore_void_result gr = entities_grow(g, slot + 1u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "entity_alloc_slot: entities_grow");
    }
    g->entities[slot].in_use = true;
    g->entity_high_water = slot + 1u;
    *out = slot;
    return YETTY_OK_VOID();
}

static uint32_t id_hash(uint64_t id, uint32_t capacity_mask)
{
    uint64_t h = id * 0x9E3779B97F4A7C15ULL;
    return (uint32_t)((h ^ (h >> 32)) & capacity_mask);
}

static struct yetty_ycore_void_result id_index_grow(struct yetty_ygrid_grid *g, uint32_t want)
{
    if ((want + 1u) * 10u <= g->id_index_capacity * 7u) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = g->id_index_capacity ? g->id_index_capacity * 2u : 16u;
    while ((want + 1u) * 10u > new_cap * 7u) {
        new_cap *= 2u;
    }
    struct ygrid_id_index_entry *new_index =
        (struct ygrid_id_index_entry *)calloc(new_cap, sizeof(struct ygrid_id_index_entry));
    if (!new_index) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: id_index alloc");
    }
    uint32_t mask = new_cap - 1u;
    struct ygrid_id_index_entry *old = g->id_index;
    uint32_t old_cap = g->id_index_capacity;
    g->id_index = new_index;
    g->id_index_capacity = new_cap;
    g->id_index_count = 0;
    for (uint32_t i = 0; i < old_cap; i++) {
        uint64_t k = old[i].external_id;
        if (k == YGRID_ID_INDEX_EMPTY || k == YGRID_ID_INDEX_TOMBSTONE) {
            continue;
        }
        uint32_t j = id_hash(k, mask);
        while (new_index[j].external_id != YGRID_ID_INDEX_EMPTY) {
            j = (j + 1u) & mask;
        }
        new_index[j].external_id = k;
        new_index[j].slot = old[i].slot;
        g->id_index_count++;
    }
    free(old);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result id_index_insert(struct yetty_ygrid_grid *g,
                                                      uint64_t external_id, uint32_t slot)
{
    struct yetty_ycore_void_result gr = id_index_grow(g, g->id_index_count + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "ygrid: id_index_grow");
    uint32_t mask = g->id_index_capacity - 1u;
    uint32_t i = id_hash(external_id, mask);
    uint32_t first_tomb = UINT32_MAX;
    while (1) {
        uint64_t key = g->id_index[i].external_id;
        if (key == YGRID_ID_INDEX_EMPTY) {
            break;
        }
        if (key == YGRID_ID_INDEX_TOMBSTONE) {
            if (first_tomb == UINT32_MAX) {
                first_tomb = i;
            }
        } else if (key == external_id) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: id_index duplicate insert");
        }
        i = (i + 1u) & mask;
    }
    if (first_tomb != UINT32_MAX) {
        i = first_tomb;
    }
    g->id_index[i].external_id = external_id;
    g->id_index[i].slot = slot;
    g->id_index_count++;
    return YETTY_OK_VOID();
}

static void id_index_remove(struct yetty_ygrid_grid *g, uint64_t external_id)
{
    if (g->id_index_capacity == 0) {
        return;
    }
    uint32_t mask = g->id_index_capacity - 1u;
    uint32_t i = id_hash(external_id, mask);
    for (uint32_t probes = 0; probes < g->id_index_capacity; probes++) {
        uint64_t key = g->id_index[i].external_id;
        if (key == YGRID_ID_INDEX_EMPTY) {
            return;
        }
        if (key == external_id) {
            g->id_index[i].external_id = YGRID_ID_INDEX_TOMBSTONE;
            g->id_index[i].slot = 0;
            g->id_index_count--;
            return;
        }
        i = (i + 1u) & mask;
    }
}

static uint32_t id_index_lookup(const struct yetty_ygrid_grid *g, uint64_t external_id)
{
    if (g->id_index_capacity == 0) {
        return YGRID_INVALID_SLOT;
    }
    uint32_t mask = g->id_index_capacity - 1u;
    uint32_t i = id_hash(external_id, mask);
    for (uint32_t probes = 0; probes < g->id_index_capacity; probes++) {
        uint64_t key = g->id_index[i].external_id;
        if (key == YGRID_ID_INDEX_EMPTY) {
            return YGRID_INVALID_SLOT;
        }
        if (key == external_id) {
            return g->id_index[i].slot;
        }
        i = (i + 1u) & mask;
    }
    return YGRID_INVALID_SLOT;
}

static struct yetty_ycore_void_result entity_push_child(struct ygrid_entity *parent,
                                                        uint32_t child_slot)
{
    if (parent->children_count == parent->children_capacity) {
        uint32_t cap = parent->children_capacity ? parent->children_capacity * 2u : 4u;
        uint32_t *grown = (uint32_t *)realloc(parent->children, cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: entity children oom");
        }
        parent->children = grown;
        parent->children_capacity = cap;
    }
    parent->children[parent->children_count++] = child_slot;
    return YETTY_OK_VOID();
}

static void entity_remove_child(struct ygrid_entity *parent, uint32_t child_slot)
{
    for (uint32_t i = 0; i < parent->children_count; i++) {
        if (parent->children[i] == child_slot) {
            parent->children[i] = parent->children[--parent->children_count];
            return;
        }
    }
}

static struct yetty_ycore_void_result entity_push_prim(struct ygrid_entity *e, uint32_t prim_index)
{
    if (e->prim_count == e->prim_capacity) {
        uint32_t cap = e->prim_capacity ? e->prim_capacity * 2u : 8u;
        uint32_t *grown = (uint32_t *)realloc(e->prim_indices, cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: entity prim_indices oom");
        }
        e->prim_indices = grown;
        e->prim_capacity = cap;
    }
    e->prim_indices[e->prim_count++] = prim_index;
    return YETTY_OK_VOID();
}

/* Forward decl — bucket_prim is defined further down (next to
 * parse_and_index_record). rebucket_subtree_to_back below calls it. */
static struct yetty_ycore_void_result bucket_prim(struct yetty_ygrid_grid *g, uint32_t prim_index);

/* Detach this entity's prims from every cell that holds them. Used
 * both by `entity_drop_prims` (which then tombstones + clears the
 * entity's prim list) AND by `rebucket_subtree_to_back` (which keeps
 * the entity's prim list intact so the prims can be re-bucketed at
 * the end of every cell). Returns nothing — caller decides what to
 * do with the surviving prim_indices array. */
static void entity_drop_from_cells(struct yetty_ygrid_grid *g, struct ygrid_entity *e)
{
    if (e->prim_count == 0) {
        return;
    }
    size_t total_cells = (size_t)g->grid_cols * (size_t)g->grid_rows;
    for (size_t ci = 0; ci < total_cells && g->cells; ci++) {
        struct ygrid_cell *cell = &g->cells[ci];
        uint32_t w = 0;
        for (uint32_t r = 0; r < cell->count; r++) {
            uint32_t pi = cell->indices[r];
            bool drop = false;
            for (uint32_t k = 0; k < e->prim_count; k++) {
                if (e->prim_indices[k] == pi) {
                    drop = true;
                    break;
                }
            }
            if (!drop) {
                cell->indices[w++] = pi;
            }
        }
        cell->count = w;
    }
}

/* Detach this entity's prims from every cell that holds them, then
 * tombstone the prims so the staging rebuild skips them. The cells
 * scan is O(grid cells * entity prims) — acceptable for now;
 * touched_cells optimization will land in Phase 1b. */
static void entity_drop_prims(struct yetty_ygrid_grid *g, struct ygrid_entity *e)
{
    if (e->prim_count == 0) {
        return;
    }
    entity_drop_from_cells(g, e);
    for (uint32_t k = 0; k < e->prim_count; k++) {
        uint32_t pi = e->prim_indices[k];
        if (pi < g->prim_count) {
            g->prims[pi].entity_slot = YGRID_INVALID_SLOT;
        }
    }
    e->prim_count = 0;
    g->staging_dirty = 1;
}

/* Move every descendant entity's prims to the END of their cells,
 * in entity-tree DFS order (parent-of-descendants first, then their
 * children, etc.). Called from process_group_body after a CMD_GROUP
 * for an EXISTING entity has been processed — at that point the
 * parent's NEW chrome prims have been appended to cells, but the
 * descendants' (already-existing) prims are still sitting at their
 * ORIGINAL cell positions from the prior frame, which is BEFORE the
 * parent's new chrome. Without this re-bucket the parent's chrome
 * gets painted ON TOP of its own children — exactly the
 * "section-open-but-body-BG-hides-the-buttons" bug.
 *
 * Re-bucketing is a drop-from-cells + bucket_prim cycle. The prim's
 * meta and entity ownership are untouched; only its position in
 * `cell->indices[]` changes (moves from somewhere in the middle to
 * the end). After this walk, cell order matches tree order:
 *
 *   [unrelated entities' prims] [parent's NEW prims] [descendant 1]
 *   [descendant 1's children] [descendant 2] [descendant 2's children]
 *
 * which is what the shader expects (composite back-to-front, parent
 * first, leaves on top). */
static void rebucket_subtree_to_back(struct yetty_ygrid_grid *g, uint32_t slot)
{
    if (slot >= g->entity_capacity) {
        return;
    }
    struct ygrid_entity *e = &g->entities[slot];
    if (!e->in_use) {
        return;
    }
    for (uint32_t i = 0; i < e->children_count; i++) {
        uint32_t cs = e->children[i];
        if (cs >= g->entity_capacity) {
            continue;
        }
        struct ygrid_entity *child = &g->entities[cs];
        if (!child->in_use) {
            continue;
        }
        /* Move child's own prims to the back of every cell holding
         * them. entity_drop_from_cells preserves the entity's
         * prim_indices list, so a second bucket_prim() round just
         * re-appends each prim at the cell tail. */
        if (child->prim_count > 0) {
            entity_drop_from_cells(g, child);
            for (uint32_t k = 0; k < child->prim_count; k++) {
                uint32_t pi = child->prim_indices[k];
                if (pi < g->prim_count && g->prims[pi].entity_slot != YGRID_INVALID_SLOT) {
                    struct yetty_ycore_void_result br = bucket_prim(g, pi);
                    if (YETTY_IS_ERR(br)) {
                        /* OOM during re-bucket — best-effort log,
                         * skip this prim. The visible effect is one
                         * widget temporarily painted under its
                         * parent's chrome, which the next emit
                         * will repair. */
                        yetty_ycore_error_destroy(br.error);
                    }
                }
            }
            g->staging_dirty = 1;
        }
        /* Recurse so grandchildren get pushed to the back too,
         * preserving tree order all the way down. */
        rebucket_subtree_to_back(g, cs);
    }
}

/* Recursive subtree delete. Frees the slot AND removes the entry from
 * the parent's children[]. The root slot cannot be deleted (clear it
 * via CMD_ZERO instead). */
static void entity_delete_subtree(struct yetty_ygrid_grid *g, uint32_t slot);

static void entity_release_slot(struct yetty_ygrid_grid *g, uint32_t slot)
{
    struct ygrid_entity *e = &g->entities[slot];
    if (e->external_id != 0) {
        id_index_remove(g, e->external_id);
    }
    entity_free_storage(e);
    e->in_use = false;
    e->external_id = 0;
    e->parent_slot = YGRID_INVALID_SLOT;
    e->next_free = g->free_slot_head;
    g->free_slot_head = slot;
}

static void entity_delete_subtree(struct yetty_ygrid_grid *g, uint32_t slot)
{
    if (slot == YGRID_ROOT_SLOT) {
        return;
    }
    struct ygrid_entity *e = &g->entities[slot];
    if (!e->in_use) {
        return;
    }
    /* Children first (caller-recursive). Walk a snapshot since
     * entity_release_slot zeroes the children list. */
    while (e->children_count > 0) {
        uint32_t child = e->children[e->children_count - 1u];
        e->children_count--;
        entity_delete_subtree(g, child);
    }
    /* Detach prims from cells, tombstone, then unlink from parent. */
    entity_drop_prims(g, e);
    uint32_t parent_slot = e->parent_slot;
    if (parent_slot != YGRID_INVALID_SLOT && parent_slot < g->entity_capacity &&
        g->entities[parent_slot].in_use) {
        entity_remove_child(&g->entities[parent_slot], slot);
    }
    entity_release_slot(g, slot);
}

static struct ygrid_entity *entity_lookup(struct yetty_ygrid_grid *g, uint64_t external_id)
{
    if (external_id == 0) {
        return &g->entities[YGRID_ROOT_SLOT];
    }
    uint32_t slot = id_index_lookup(g, external_id);
    if (slot == YGRID_INVALID_SLOT) {
        return NULL;
    }
    return &g->entities[slot];
}

/* On exit, *out_was_existing is 1 when the id was already bound (we
 * re-opened the entity's scope and dropped its prims) and 0 when a
 * fresh entity was minted. `out_was_existing` may be NULL — callers
 * that don't care pass NULL. The flag drives `rebucket_subtree_to_back`
 * in process_group_body: only re-opened entities need their
 * descendants moved to the back of cells (newly-minted entities have
 * no descendants yet). */
static struct yetty_ycore_void_result entity_lookup_or_create(struct yetty_ygrid_grid *g,
                                                              uint32_t parent_slot,
                                                              uint64_t external_id,
                                                              uint32_t *out_slot,
                                                              int *out_was_existing)
{
    if (out_was_existing) {
        *out_was_existing = 0;
    }
    if (external_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: entity_create with external_id=0 is reserved");
    }
    if (external_id == YGRID_ID_INDEX_TOMBSTONE) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygrid: entity_create with external_id=UINT64_MAX is reserved");
    }
    uint32_t existing = id_index_lookup(g, external_id);
    if (existing != YGRID_INVALID_SLOT) {
        /* Re-opening an existing scope. The producer's incremental
         * path emits CMD_GROUP(id, new_body) when a widget is dirty;
         * the body's prims REPLACE the entity's previous content, so
         * drop the old prims (out of cells + tombstone in g->prims +
         * reset the entity's prim_indices list) before returning. The
         * entity's children are NOT touched — dirty children re-emit
         * via their own CMD_GROUP scope (which lands here and clears
         * them in turn); clean children's prims survive untouched.
         *
         * After the body's new prims are added, the caller must call
         * `rebucket_subtree_to_back` so the descendants' (stale)
         * cell entries get moved to AFTER the new prims — otherwise
         * the parent's chrome (just appended at the tail) gets painted
         * on top of its own children's prims, which still sit at the
         * head from the prior frame. */
        entity_drop_prims(g, &g->entities[existing]);
        *out_slot = existing;
        if (out_was_existing) {
            *out_was_existing = 1;
        }
        return YETTY_OK_VOID();
    }
    uint32_t slot;
    struct yetty_ycore_void_result ar = entity_alloc_slot(g, &slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ygrid: entity_alloc_slot");
    struct ygrid_entity *e = &g->entities[slot];
    e->external_id = external_id;
    e->parent_slot = parent_slot;
    struct yetty_ycore_void_result ir = id_index_insert(g, external_id, slot);
    if (YETTY_IS_ERR(ir)) {
        entity_release_slot(g, slot);
        return YETTY_ERR(yetty_ycore_void, "ygrid: id_index_insert", ir);
    }
    struct yetty_ycore_void_result cr = entity_push_child(&g->entities[parent_slot], slot);
    if (YETTY_IS_ERR(cr)) {
        entity_release_slot(g, slot);
        return YETTY_ERR(yetty_ycore_void, "ygrid: entity_push_child", cr);
    }
    *out_slot = slot;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Cell helpers
 *=========================================================================*/

static struct yetty_ycore_void_result cell_push(struct ygrid_cell *cell, uint32_t prim_index)
{
    if (cell->count == cell->cap) {
        uint32_t cap = cell->cap ? cell->cap * 2u : 4u;
        uint32_t *grown = (uint32_t *)realloc(cell->indices, cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: cell index oom");
        }
        cell->indices = grown;
        cell->cap = cap;
    }
    cell->indices[cell->count++] = prim_index;
    return YETTY_OK_VOID();
}

static void cells_free(struct ygrid_cell *cells, size_t n)
{
    if (!cells) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        free(cells[i].indices);
    }
    free(cells);
}

static struct yetty_ycore_void_result cells_alloc(struct yetty_ygrid_grid *g)
{
    size_t n = (size_t)g->grid_cols * (size_t)g->grid_rows;
    cells_free(g->cells, n);
    g->cells = (struct ygrid_cell *)calloc(n, sizeof(struct ygrid_cell));
    if (!g->cells) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: cells alloc oom");
    }
    return YETTY_OK_VOID();
}

static void cells_clear(struct yetty_ygrid_grid *g)
{
    size_t n = (size_t)g->grid_cols * (size_t)g->grid_rows;
    for (size_t i = 0; i < n; ++i) {
        g->cells[i].count = 0;
    }
}

/*===========================================================================
 * Spatial bucketing — insert one prim into every cell it overlaps.
 *=========================================================================*/

static struct yetty_ycore_void_result bucket_prim(struct yetty_ygrid_grid *g, uint32_t prim_index)
{
    const struct ygrid_prim_meta *p = &g->prims[prim_index];
    /* Cell size spans the content extent (matches the shader's
     * grid_size*cell_size), so prims past the visible rect bucket into
     * real cell rows instead of clamping into the last visible one.
     * Content defaults to the rect when unset. */
    float content_w = g->content_w > 0.0f ? g->content_w : (g->base.rect.max.x - g->base.rect.min.x);
    float content_h = g->content_h > 0.0f ? g->content_h : (g->base.rect.max.y - g->base.rect.min.y);
    float cw = content_w / (float)g->grid_cols;
    float ch = content_h / (float)g->grid_rows;
    if (cw <= 0.0f || ch <= 0.0f) {
        return YETTY_OK_VOID();
    }

    int col_min = (int)(p->min_x / cw);
    int col_max = (int)(p->max_x / cw);
    int row_min = (int)(p->min_y / ch);
    int row_max = (int)(p->max_y / ch);
    if (col_min < 0) {
        col_min = 0;
    }
    if (row_min < 0) {
        row_min = 0;
    }
    if (col_max >= (int)g->grid_cols) {
        col_max = (int)g->grid_cols - 1;
    }
    if (row_max >= (int)g->grid_rows) {
        row_max = (int)g->grid_rows - 1;
    }
    ydebug("ygrid: bucket prim_index=%u aabb=(%.1f,%.1f)-(%.1f,%.1f) cw=%.2f ch=%.2f → cells "
           "(%d..%d, %d..%d)",
           prim_index, p->min_x, p->min_y, p->max_x, p->max_y, cw, ch, col_min, col_max, row_min,
           row_max);
    if (col_max < col_min || row_max < row_min) {
        return YETTY_OK_VOID();
    }

    for (int r = row_min; r <= row_max; ++r) {
        for (int c = col_min; c <= col_max; ++c) {
            struct yetty_ycore_void_result pr =
                cell_push(&g->cells[(size_t)r * g->grid_cols + (size_t)c], prim_index);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid: cell_push");
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Wire-record parsing — append one record's metadata to prims[].
 *
 * Record sizes are NOT uniform on the wire. Two shapes coexist:
 *
 *   SDF prims (ysdf types):  FIXED size by type (no payload_size word).
 *                              Layout: [type, z_order, fill, stroke,
 *                                       stroke_w, geom_words...]
 *                              Size derived from `yetty_ysdf_word_count(type)`.
 *
 *   FAM prims (TEXT_SPAN/FONT etc.):  Self-describing.
 *                              Layout: [type, payload_size, payload...]
 *                              Size = 8 + payload_size bytes.
 *
 * The caller hands us the FULL record size as `record_len` (computed
 * upstream by the iterator's ops->size or by command_parse). We don't
 * try to re-derive it — that's where my earlier bug was: I read
 * hdr[1] as payload_size, but for SDF that slot is z_order. Wrong.
 *
 * Staging layout per prim (matches ydraw-layer.wgsl expectations):
 *   word 0: rolling_row (= 0 for compositor figures, no scrolling)
 *   word 1: type
 *   word 2..N: rest of the wire record (z_order onwards), copied as-is.
 *
 * We store `prim_payload_offset = record_offset` (BYTE offset to the
 * start of the wire record — the TYPE word) and
 * `prim_payload_words = record_len / 4` (total record size in u32).
 * Staging build memcpy's the full record after the rolling_row prefix
 * — no need to synthesize the type separately.
 *=========================================================================*/

/* Forward decls — the TEXT_SPAN expansion below uses
 * parse_and_index_record to bucket each generated glyph record, and
 * grow_bytes to extend grid->bytes for the new GLYPH records. The
 * normal SDF/GLYPH parse loop and the expansion call into each other. */
static struct yetty_ycore_void_result parse_and_index_record(struct yetty_ygrid_grid *g,
                                                             uint32_t record_offset,
                                                             size_t record_len);
static struct yetty_ycore_void_result grow_bytes(struct yetty_ygrid_grid *grid, size_t need);
static struct yetty_ycore_void_result ygrid_reset_content(struct yetty_yfigure_figure *self);

/* Decode one UTF-8 codepoint at *ptr (clamped by `end`). Advances *ptr
 * past the consumed bytes and returns the codepoint, or 0xFFFD on a
 * malformed prefix (still consumes one byte to make forward progress).
 * Matches the same decode shape scene-canvas uses for TEXT_SPAN. */
static uint32_t decode_utf8(const uint8_t **ptr, const uint8_t *end)
{
    const uint8_t *cursor = *ptr;
    if (cursor >= end) {
        return 0;
    }
    uint8_t lead = *cursor;
    uint32_t codepoint;
    if ((lead & 0x80u) == 0u) {
        codepoint = lead;
        cursor += 1;
    } else if ((lead & 0xE0u) == 0xC0u) {
        codepoint = (uint32_t)(lead & 0x1Fu) << 6;
        cursor += 1;
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
        }
    } else if ((lead & 0xF0u) == 0xE0u) {
        codepoint = (uint32_t)(lead & 0x0Fu) << 12;
        cursor += 1;
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 6;
        }
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
        }
    } else if ((lead & 0xF8u) == 0xF0u) {
        codepoint = (uint32_t)(lead & 0x07u) << 18;
        cursor += 1;
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 12;
        }
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 6;
        }
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
        }
    } else {
        codepoint = 0xFFFDu;
        cursor += 1;
    }
    *ptr = cursor;
    return codepoint;
}

/* Expand one TEXT_SPAN wire record into glyph records, mirroring
 * scene-canvas's scene_expand_text_span_to_glyphs. The TEXT_SPAN's
 * font_id maps directly to a ygrid font slot (-1 means slot 0, the
 * default). Each generated glyph is appended to grid->bytes as its own
 * 7-word GLYPH wire record and bucketed via parse_and_index_record so
 * the rest of the pipeline treats it like any other glyph.
 *
 * `text_run` and `text_run_len` are passed in instead of read from the
 * view because the view's text pointer lives inside grid->bytes, which
 * may be realloc'd by grow_bytes when each generated glyph is emitted.
 * The caller takes a heap copy first to keep the pointer stable. */
static struct yetty_ycore_void_result expand_text_span(
    struct yetty_ygrid_grid *grid, const struct yetty_ydraw_text_span_drawable_view *span,
    const uint8_t *text_run, uint32_t text_run_len)
{
    /* font_id < 0 means "default" → slot 0. Otherwise the producer chose
     * an explicit slot via the slot-indexed set_font API. Out-of-range
     * or NULL-slot fonts are dropped silently — same as scene-canvas's
     * "no font registered yet" path. */
    uint32_t slot = (span->font_id < 0) ? 0u : (uint32_t)span->font_id;
    if (slot >= grid->font_count || !grid->fonts[slot]) {
        ydebug("ygrid: TEXT_SPAN font_id=%d -> slot %u has no font; dropped", span->font_id, slot);
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_font *font = grid->fonts[slot];

    struct yetty_yrender_gpu_resource_set_result font_rs_result =
        font->ops->get_gpu_resource_set(font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_rs_result, "ygrid: text_span font rs");
    const struct yetty_ydraw_gpu_resource_set *font_rs = font_rs_result.value;
    if (font_rs->buffer_count == 0 || !font_rs->buffers[0].data) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: text_span: font has no glyph metadata");
    }
    /* msdf-font's rs.buffers[0] is an array of 6-float entries:
     *   size_x, size_y, bearing_x, bearing_y, advance, cell_idx
     * The font lazily allocates a new metadata slot whenever
     * get_glyph_index sees a codepoint it hasn't rasterized yet, so
     * `font_rs->buffers[0].size` grows DURING this loop. Re-fetch it
     * after every get_glyph_index call (same pattern scene-canvas's
     * expand uses) so the bounds check stays in sync. */
    (void)font_rs; /* the per-iteration re-fetch supersedes this snapshot */

    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0.0f) ? span->font_size / base_size : 1.0f;
    float cursor_x = span->x;

    const uint8_t *cursor = text_run;
    const uint8_t *end = text_run + text_run_len;
    while (cursor < end) {
        uint32_t codepoint = decode_utf8(&cursor, end);
        if (codepoint == 0) {
            break;
        }

        struct uint32_result glyph_idx_result = font->ops->get_glyph_index(font, codepoint);
        if (YETTY_IS_ERR(glyph_idx_result)) {
            /* No glyph for this codepoint — match scene-canvas's
             * fallback: advance by a quarter em + spacing. */
            yetty_ycore_error_destroy(glyph_idx_result.error);
            cursor_x += (span->font_size * 0.25f) + span->char_spacing;
            if (codepoint == 0x20) {
                cursor_x += span->word_spacing;
            }
            continue;
        }
        uint32_t glyph_index = glyph_idx_result.value;

        /* Re-fetch the metadata view AFTER get_glyph_index so any
         * lazy slot allocation it triggered is visible here. */
        struct yetty_yrender_gpu_resource_set_result fresh_rs_result =
            font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fresh_rs_result, "ygrid: text_span font rs refetch");
        const struct yetty_ydraw_gpu_resource_set *fresh_rs = fresh_rs_result.value;
        const float *meta = (const float *)fresh_rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(fresh_rs->buffers[0].size / (6u * sizeof(float)));
        if (glyph_index >= meta_count) {
            return YETTY_ERR(yetty_ycore_void,
                             "ygrid: text_span glyph_index out of metadata range");
        }

        const float *glyph_meta = meta + glyph_index * 6u;
        float size_x = glyph_meta[0];
        float size_y = glyph_meta[1];
        float bearing_x = glyph_meta[2];
        float bearing_y = glyph_meta[3];
        float advance = glyph_meta[4];

        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * scale + span->char_spacing;
            if (codepoint == 0x20) {
                cursor_x += span->word_spacing;
            }
            continue;
        }

        float glyph_x = cursor_x + bearing_x * scale;
        float glyph_y = span->y - bearing_y * scale;

        /* Build the 7-word GLYPH wire record into a stack buffer,
         * append to grid->bytes (which may realloc), then bucket via
         * parse_and_index_record. Re-reading text_run / span from
         * grid->bytes after the realloc would be unsafe — that's why
         * the caller passed in a heap-stable text copy. */
        uint32_t glyph_record[7];
        glyph_record[0] = YGRID_GLYPH_TYPE;
        glyph_record[1] = 0u; /* z_order */
        memcpy(&glyph_record[2], &glyph_x, sizeof(float));
        memcpy(&glyph_record[3], &glyph_y, sizeof(float));
        memcpy(&glyph_record[4], &span->font_size, sizeof(float));
        glyph_record[5] = (glyph_index & 0xFFFFu) | ((slot + 1u) << 16);
        glyph_record[6] = span->color;

        size_t glyph_bytes = sizeof(glyph_record);
        struct yetty_ycore_void_result grow_result = grow_bytes(grid, glyph_bytes);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, grow_result, "ygrid: text_span grow_bytes");
        uint32_t glyph_offset = (uint32_t)grid->bytes_len;
        memcpy(grid->bytes + grid->bytes_len, glyph_record, glyph_bytes);
        grid->bytes_len += glyph_bytes;

        struct yetty_ycore_void_result index_result =
            parse_and_index_record(grid, glyph_offset, glyph_bytes);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, index_result, "ygrid: text_span index glyph");

        cursor_x += advance * scale + span->char_spacing;
        if (codepoint == 0x20) {
            cursor_x += span->word_spacing;
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result parse_and_index_record(struct yetty_ygrid_grid *g,
                                                             uint32_t record_offset,
                                                             size_t record_len)
{
    if (record_len < 4u || record_len % 4u != 0) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: record_len must be a non-zero u32-multiple");
    }
    if ((size_t)record_offset + record_len > g->bytes_len) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: record overruns byte buffer");
    }
    const uint32_t *hdr = (const uint32_t *)(g->bytes + record_offset);
    uint32_t type = hdr[0];
    (void)hdr; /* hdr[1] is NOT payload_size for SDF prims — see comment. */

    /* Compute aabb. ygrid currently handles two prim categories:
     *
     *   SDF prims (handler returns OK on ysdf tier types)
     *     — aabb derived from prim geometry
     *
     *   GLYPH prims (type 200 — ydraw's GLYPH layout, no separate
     *     handler in ysdf since glyphs aren't SDF) — aabb derived from
     *     [x, y, font_size]; glyph_size unknown without font, so use
     *     font_size as a conservative square (refined when font support
     *     lands).
     *
     * Anything else (TEXT_SPAN, FONT, complex prims) is RECORDED in
     * the byte buffer but NOT indexed: no entry in prims[] or the
     * spatial buckets, so it doesn't reach the shader. Returning OK
     * keeps the wire decoder flowing — ygui sends a mix of records
     * and dropping the unrendered ones silently is the v1 contract.
     * The bytes consumed by these records aren't reclaimed; they live
     * until the next yetty_ygrid_clear. */
    /* TEXT_SPAN: not a rendered prim itself. Expand into one GLYPH
     * record per codepoint (same shape as scene-canvas's expansion);
     * each generated glyph is appended + bucketed normally and shows
     * up in prim_count/staging. The TEXT_SPAN bytes themselves stay in
     * grid->bytes but produce no prim entry. */
    if (type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        struct yetty_ydraw_text_span_drawable_view view;
        if (yetty_ydraw_text_span_drawable_parse(hdr, &view) != 0) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: TEXT_SPAN parse failed");
        }
        /* `view.text` aliases into g->bytes; grow_bytes inside the
         * expansion may realloc that buffer and dangle the pointer.
         * Take a heap copy so each glyph emission has a stable read. */
        uint8_t *text_copy = NULL;
        if (view.text_len > 0) {
            text_copy = (uint8_t *)malloc(view.text_len);
            if (!text_copy) {
                return YETTY_ERR(yetty_ycore_void, "ygrid: TEXT_SPAN text copy oom");
            }
            memcpy(text_copy, view.text, view.text_len);
        }
        struct yetty_ycore_void_result expand_result =
            expand_text_span(g, &view, text_copy, view.text_len);
        free(text_copy);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, expand_result, "ygrid: TEXT_SPAN expand");
        return YETTY_OK_VOID();
    }

    struct rectangle_result ar;
    if (type == YGRID_GLYPH_TYPE) {
        /* GLYPH wire layout — 7 words, identical to scrolling-canvas's
         * YDRAW_GLYPH_WORDS so the same ydraw-layer.wgsl shader reads
         * both producers:
         *   word 0 type            (= 200)
         *   word 1 z_order
         *   word 2 x               ← read here
         *   word 3 y
         *   word 4 font_size
         *   word 5 packed          (glyph_idx | (slot+1) << 16)
         *   word 6 color
         * rebuild_prim_staging prepends a rolling_row=0 word, so
         * storage_buffer[drawable_offset+3] lands on word 2 (x) — which
         * is what glyph_read_x in the shader expects. */
        if (record_len < 7u * sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: GLYPH record truncated");
        }
        float gx = *(const float *)&hdr[2];
        float gy = *(const float *)&hdr[3];
        float gs = *(const float *)&hdr[4];
        ar = YETTY_OK(rectangle, ((struct yetty_ycore_rectangle){
                                     .min = {.x = gx, .y = gy},
                                     .max = {.x = gx + gs, .y = gy + gs},
                                 }));
    } else if (yetty_ydraw_is_figure(type) && g->figure_factory) {
        /* Complex prim (yplot / yimage / yvideo / yzoo / yjungle …).
         * Mint a figure instance via the host-supplied factory and
         * stash it in figure_instances; the render path will paint
         * it after the SDF / glyph pass. We do NOT also push the prim
         * into prims[] — complex prims are rendered through the
         * instance's own pipeline, not through the unified shader. */
        if (g->figure_instance_count == g->figure_instance_cap) {
            uint32_t cap = g->figure_instance_cap ? g->figure_instance_cap * 2u : 4u;
            struct yetty_ydraw_figure **grown = (struct yetty_ydraw_figure **)realloc(
                g->figure_instances, cap * sizeof(struct yetty_ydraw_figure *));
            if (!grown) {
                return YETTY_ERR(yetty_ycore_void, "ygrid: figure_instances oom");
            }
            g->figure_instances = grown;
            g->figure_instance_cap = cap;
        }
        struct yetty_ydraw_figure_ptr_result ir = yetty_ydraw_raw_figure_factory_create_instance(
            g->figure_factory, hdr, record_len, /*rolling_row=*/0u);
        if (YETTY_IS_ERR(ir)) {
            ydebug("ygrid: figure_factory create_instance failed for type=0x%08x: %s", type,
                   ir.error.msg);
            yetty_ycore_error_destroy(ir.error);
            return YETTY_OK_VOID();
        }
        g->figure_instances[g->figure_instance_count++] = ir.value;
        ir.value->dirty = 1;
        g->base.dirty = 1;
        return YETTY_OK_VOID();
    } else {
        struct yetty_ydraw_drawable_base_ops_ptr_result ops_r = yetty_ysdf_handler(type);
        if (YETTY_IS_ERR(ops_r)) {
            /* Not an SDF type and not a glyph — drop the error,
             * leave the wire bytes in place, and report success.
             * v1 renders nothing for unsupported types. */
            ydebug("ygrid: drop unrenderable type=0x%08x len=%zu (figure_factory=%p)", type,
                   record_len, (void *)g->figure_factory);
            yetty_ycore_error_destroy(ops_r.error);
            return YETTY_OK_VOID();
        }
        const struct yetty_ydraw_drawable_base_ops *ops = ops_r.value;
        ar = ops->aabb(hdr);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ygrid: SDF prim aabb");
        ydebug("ygrid: SDF type=0x%08x aabb=(%.1f,%.1f)-(%.1f,%.1f) len=%zu", type, ar.value.min.x,
               ar.value.min.y, ar.value.max.x, ar.value.max.y, record_len);
    }

    if (g->prim_count == g->prim_cap) {
        uint32_t cap = g->prim_cap ? g->prim_cap * 2u : 16u;
        struct ygrid_prim_meta *grown =
            (struct ygrid_prim_meta *)realloc(g->prims, cap * sizeof(struct ygrid_prim_meta));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ygrid: prims table oom");
        }
        g->prims = grown;
        g->prim_cap = cap;
    }

    struct ygrid_prim_meta *meta = &g->prims[g->prim_count];
    meta->record_offset = record_offset;
    /* prim_payload_offset is BYTE offset of the start of the WHOLE wire
     * record (= the TYPE word). At staging time we prefix a single
     * rolling_row=0 word, then memcpy the whole record verbatim — that
     * lands the type at staging word 1 (where the shader expects it). */
    meta->prim_payload_offset = record_offset;
    meta->prim_payload_words = (uint32_t)(record_len / 4u);
    meta->type = type;
    meta->min_x = ar.value.min.x;
    meta->min_y = ar.value.min.y;
    meta->max_x = ar.value.max.x;
    meta->max_y = ar.value.max.y;
    meta->entity_slot = g->current_entity_slot;

    uint32_t prim_index = g->prim_count++;
    /* Record the prim's index on its owning entity so a future
     * CMD_DELETE on that entity can drop the prim from cells and
     * tombstone the prim_meta. */
    if (meta->entity_slot < g->entity_capacity && g->entities[meta->entity_slot].in_use) {
        struct yetty_ycore_void_result epr =
            entity_push_prim(&g->entities[meta->entity_slot], prim_index);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, epr, "ygrid: entity_push_prim");
    }
    return bucket_prim(g, prim_index);
}

/*===========================================================================
 * Staging — rebuild grid_staging + prim_staging u32 buffers
 *=========================================================================*/

static struct yetty_ycore_void_result ensure_words(uint32_t **buf, size_t *cap, size_t want)
{
    if (*cap >= want) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = *cap ? *cap : 64u;
    while (new_cap < want) {
        new_cap *= 2u;
    }
    uint32_t *grown = (uint32_t *)realloc(*buf, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: staging buffer oom");
    }
    *buf = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_grid_staging(struct yetty_ygrid_grid *g)
{
    size_t num_cells = (size_t)g->grid_cols * (size_t)g->grid_rows;
    /* Layout:
     *   [0..num_cells)      per-cell start offset (in u32 words inside
     *                        this same buffer)
     *   then concatenated   [count, idx_0, idx_1, ...] blocks per cell.
     * Empty cells share a single sentinel block holding count=0. */
    size_t need = num_cells + 1u; /* sentinel block (one word = count=0) */
    for (size_t i = 0; i < num_cells; ++i) {
        if (g->cells[i].count > 0) {
            need += 1u + g->cells[i].count;
        }
    }
    struct yetty_ycore_void_result r = ensure_words(&g->grid_staging, &g->grid_staging_cap, need);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ygrid: grid_staging ensure_words");

    /* Write the sentinel block right after the offset table. */
    uint32_t sentinel_off = (uint32_t)num_cells;
    g->grid_staging[sentinel_off] = 0u; /* count = 0 */
    uint32_t cursor = sentinel_off + 1u;

    for (size_t i = 0; i < num_cells; ++i) {
        const struct ygrid_cell *cell = &g->cells[i];
        if (cell->count == 0) {
            g->grid_staging[i] = sentinel_off;
            continue;
        }
        g->grid_staging[i] = cursor;
        g->grid_staging[cursor++] = cell->count;
        for (uint32_t k = 0; k < cell->count; ++k) {
            g->grid_staging[cursor++] = cell->indices[k];
        }
    }
    g->grid_staging_words = need;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_prim_staging(struct yetty_ygrid_grid *g)
{
    /* Layout (matches ydraw-layer.wgsl's expectations exactly):
     *   [0..prim_count)   offset table: each entry = data_offset in
     *                      u32 words from end of table to that prim's
     *                      first word.
     *   after table:      concatenated prim records, each shaped as
     *                      [rolling_row=0, FULL_WIRE_RECORD_WORDS...]
     *                      where the wire record starts with `type` at
     *                      its word 0 (followed by z_order, fill,
     *                      stroke, stroke_w, geometry — for SDF — or
     *                      payload_size + bytes for FAM prims). The
     *                      rolling_row prefix puts the type at staging
     *                      word 1, where evaluate_sdf_2d expects it
     *                      (drawable_offset+1u in the shader). The
     *                      rolling_row constant 0 is the no-scroll
     *                      contract — see feedback_rolling_row_scope. */
    size_t total_record_words = 0;
    for (uint32_t i = 0; i < g->prim_count; ++i) {
        total_record_words += 1u /* rolling_row */ + g->prims[i].prim_payload_words;
    }

    size_t need = (size_t)g->prim_count + total_record_words;
    struct yetty_ycore_void_result r = ensure_words(&g->prim_staging, &g->prim_staging_cap, need);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ygrid: prim_staging ensure_words");

    uint32_t cursor = (uint32_t)g->prim_count; /* offset table comes first */
    for (uint32_t i = 0; i < g->prim_count; ++i) {
        const struct ygrid_prim_meta *m = &g->prims[i];
        uint32_t data_offset = cursor - (uint32_t)g->prim_count;
        g->prim_staging[i] = data_offset;

        g->prim_staging[cursor++] = 0u; /* rolling_row prefix */
        /* Copy the WHOLE wire record (starting from the type word). */
        memcpy(&g->prim_staging[cursor], g->bytes + m->prim_payload_offset,
               (size_t)m->prim_payload_words * sizeof(uint32_t));
        cursor += m->prim_payload_words;
    }
    g->prim_staging_words = need;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure ops
 *=========================================================================*/

static struct yetty_ycore_void_result ygrid_destroy(struct yetty_yfigure_figure *self)
{
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)self;

    if (g->binder) {
        g->binder->ops->destroy(g->binder);
    }
    if (g->registry) {
        yetty_ydraw_flyweight_registry_destroy(g->registry);
    }

    free(g->sdf_lib_code.data);
    free(g->layer_shader_code.data);
    free(g->combined_shader);
    free(g->grid_staging);
    free(g->prim_staging);
    if (g->cells) {
        cells_free(g->cells, (size_t)g->grid_cols * (size_t)g->grid_rows);
    }
    free(g->prims);
    free(g->bytes);

    /* Entity table teardown. Free per-entity storage (children list,
     * prim_indices) for every slot ever issued; then free the array. */
    for (uint32_t i = 0; i < g->entity_high_water; i++) {
        entity_free_storage(&g->entities[i]);
    }
    free(g->entities);
    free(g->id_index);

    /* figure_factory itself is BORROWED; only the instances we minted
     * through it are ours to free. */
    if (g->figure_instances) {
        for (uint32_t i = 0; i < g->figure_instance_count; i++) {
            if (g->figure_instances[i]) {
                yetty_ydraw_figure_destroy(g->figure_instances[i]);
            }
        }
        free(g->figure_instances);
    }

    /* Free the yclass allocation (header + body); the body began at
     * obj + 1, so recover the object header by stepping back one. */
    return yetty_yclass_object_free((struct yetty_yclass_object *)self - 1);
}

void yetty_ygrid_set_figure_factory(struct yetty_ygrid_grid *grid,
                                    struct yetty_ydraw_raw_figure_factory *factory)
{
    if (!grid) {
        return;
    }
    grid->figure_factory = factory;
}

void yetty_ygrid_set_content_size(struct yetty_ygrid_grid *grid, float content_w, float content_h)
{
    if (!grid) {
        return;
    }
    grid->content_w = content_w > 0.0f ? content_w : 0.0f;
    grid->content_h = content_h > 0.0f ? content_h : 0.0f;
    /* Cell layout depends on the content extent; force a re-bucket +
     * re-stage on the next render (resize_grid_dims_if_needed picks up the
     * new dims). */
    grid->staging_dirty = 1;
}

void yetty_ygrid_set_scroll(struct yetty_ygrid_grid *grid, float scroll_x, float scroll_y)
{
    if (!grid) {
        return;
    }
    /* Pure view shift — the shader reads it as cz_off; content + buckets
     * are unchanged, so no re-stage, just a redraw. */
    grid->scroll_x = scroll_x;
    grid->scroll_y = scroll_y;
}

/* Defined further down (alongside the other shader-build helpers, after
 * the resource_set / pipeline setup section). Declared here so the
 * render path can pull it in when the font set changes mid-frame. */
static struct yetty_ycore_void_result rebuild_font_dispatcher(struct yetty_ygrid_grid *grid);

/* Defined further down (in the factory section). Declared here so the
 * render path can recompute grid dims when the figure's rect changes. */
static void ygrid_dims_from_rect(struct yetty_ycore_rectangle rect, uint32_t *out_cols,
                                 uint32_t *out_rows);

/* Recompute grid_cols/grid_rows from the figure's current rect (which
 * may have changed since create / last resize). When the dims drift,
 * free the old cells, allocate fresh ones at the new layout, and
 * re-bucket every live prim. Tombstoned prims (entity_slot ==
 * YGRID_INVALID_SLOT) are skipped — they were dropped from the
 * staging walk already. Called from the render path so the next
 * staging rebuild sees correctly-bucketed cells. */
static struct yetty_ycore_void_result resize_grid_dims_if_needed(struct yetty_ygrid_grid *g)
{
    uint32_t want_cols;
    uint32_t want_rows;
    /* Size the cell grid to the content, not the on-screen rect, so a
     * scrolling figure's off-screen prims bucket into real cells. Content
     * defaults to the rect when unset (non-scrolling figures: identical). */
    float cw = g->content_w > 0.0f ? g->content_w : (g->base.rect.max.x - g->base.rect.min.x);
    float ch = g->content_h > 0.0f ? g->content_h : (g->base.rect.max.y - g->base.rect.min.y);
    struct yetty_ycore_rectangle content_rect = {{0.0f, 0.0f}, {cw, ch}};
    ygrid_dims_from_rect(content_rect, &want_cols, &want_rows);
    if (want_cols == g->grid_cols && want_rows == g->grid_rows) {
        return YETTY_OK_VOID();
    }
    size_t old_n = (size_t)g->grid_cols * (size_t)g->grid_rows;
    cells_free(g->cells, old_n);
    g->cells = NULL;
    g->grid_cols = want_cols;
    g->grid_rows = want_rows;
    struct yetty_ycore_void_result ar = cells_alloc(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ygrid: cells_alloc after dim change");
    for (uint32_t i = 0; i < g->prim_count; i++) {
        if (g->prims[i].entity_slot == YGRID_INVALID_SLOT) {
            continue;
        }
        struct yetty_ycore_void_result br = bucket_prim(g, i);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ygrid: re-bucket after resize");
    }
    g->staging_dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ygrid_render(struct yetty_yfigure_figure *self,
                                                   struct yetty_ydraw_target *target)
{
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)self;
    ydebug("ygrid_render: rect=(%.1f,%.1f)-(%.1f,%.1f) prims=%u staging_dirty=%d", self->rect.min.x,
           self->rect.min.y, self->rect.max.x, self->rect.max.y, g->prim_count, g->staging_dirty);

    /* Absolute-coords figures span the whole target (their prims are in
     * screen coords); the GPU scissor below clips to the figure's own rect.
     * Drive the cell grid + bounds off the target extent so absolute prims
     * bucket correctly. */
    if (g->absolute_coords && target) {
        g->content_w = target->viewport.w;
        g->content_h = target->viewport.h;
    }

    struct yetty_ycore_void_result rr = resize_grid_dims_if_needed(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ygrid_render: resize_grid_dims_if_needed");

    if (g->staging_dirty) {
        struct yetty_ycore_void_result gr = rebuild_grid_staging(g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "ygrid_render: grid staging");
        struct yetty_ycore_void_result pr = rebuild_prim_staging(g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid_render: prim staging");
        g->staging_dirty = 0;
    }

    /* Font set changed since last render — regenerate the dispatcher and
     * rs.children. The shader-code hash change triggers a binder
     * refinalize on the next update(). */
    if (g->font_generation != g->last_emitted_font_generation) {
        struct yetty_ycore_void_result dispatcher_result = rebuild_font_dispatcher(g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result,
                            "ygrid_render: rebuild font dispatcher");
    }

    /* Wire current staging into the rs and re-submit to the binder. */
    g->rs.buffers[0].data = (uint8_t *)g->grid_staging;
    g->rs.buffers[0].size = g->grid_staging_words * sizeof(uint32_t);
    g->rs.buffers[0].dirty = 1;
    g->rs.buffers[1].data = (uint8_t *)g->prim_staging;
    g->rs.buffers[1].size = g->prim_staging_words * sizeof(uint32_t);
    g->rs.buffers[1].dirty = 1;

    g->rs.uniforms[U_GRID_SIZE].vec2[0] = (float)g->grid_cols;
    g->rs.uniforms[U_GRID_SIZE].vec2[1] = (float)g->grid_rows;
    float w = self->rect.max.x - self->rect.min.x;
    float h = self->rect.max.y - self->rect.min.y;
    /* Cells span the content, not the rect: grid_size * cell_size is the
     * content extent the shader bounds-checks + buckets against. Content
     * defaults to the rect when unset (the figure fills itself). */
    float cw = g->content_w > 0.0f ? g->content_w : w;
    float ch = g->content_h > 0.0f ? g->content_h : h;
    g->rs.uniforms[U_CELL_SIZE].vec2[0] = cw / (float)g->grid_cols;
    g->rs.uniforms[U_CELL_SIZE].vec2[1] = ch / (float)g->grid_rows;
    /* View size = what the NDC quad maps onto. Local figures map the rect;
     * absolute figures map the whole target so prim coords are screen
     * coords (content_w/h was set to the target above). cz_off is the
     * (local-mode) scroll offset; the per-figure scissor clips either way. */
    g->rs.uniforms[U_VIEW_SIZE].vec2[0] = g->absolute_coords ? cw : w;
    g->rs.uniforms[U_VIEW_SIZE].vec2[1] = g->absolute_coords ? ch : h;
    g->rs.uniforms[U_CZ_OFF].vec2[0] = g->scroll_x;
    g->rs.uniforms[U_CZ_OFF].vec2[1] = g->scroll_y;
    g->rs.uniforms[U_PRIM_COUNT].u32 = g->prim_count;
    g->rs.pixel_size.width = w;
    g->rs.pixel_size.height = h;

    struct yetty_ycore_void_result sr = g->binder->ops->submit(g->binder, &g->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ygrid_render: binder submit");
    if (!g->binder_finalized) {
        struct yetty_ycore_void_result fr = g->binder->ops->finalize(g->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ygrid_render: binder finalize");
        g->binder_finalized = 1;
    }
    struct yetty_ycore_void_result ur = g->binder->ops->update(g->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "ygrid_render: binder update");

    /* Draw — yplot-style direct wgpu, scissored + viewport-mapped to
     * the figure's rect. */
    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "ygrid_render: target view NULL");
    }

    float vx = self->rect.min.x;
    float vy = self->rect.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    ydebug("ygrid_render: view=%p target=%p target_vp=(%.1f,%.1f,%.1f,%.1f) "
           "viewport=(%.1f,%.1f,%.1fx%.1f) prim_count=%u",
           (void *)view, (void *)target, target->viewport.x, target->viewport.y, target->viewport.w,
           target->viewport.h, vx, vy, w, h, g->prim_count);

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g->device, &enc_desc);
    if (!enc) {
        return YETTY_ERR(yetty_ycore_void, "ygrid_render: encoder create");
    }

    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pass_desc);
    /* Absolute figures span the whole target (prim coords are screen
     * coords); local figures map the NDC quad onto their own rect. The
     * scissor below clips to the figure rect regardless. */
    if (g->absolute_coords) {
        wgpuRenderPassEncoderSetViewport(pass, target->viewport.x, target->viewport.y,
                                         target->viewport.w, target->viewport.h, 0.0f, 1.0f);
    } else {
        wgpuRenderPassEncoderSetViewport(pass, vx, vy, w, h, 0.0f, 1.0f);
    }
    /* SetScissorRect MUST stay within the render-area bounds. The
     * figure's rect may extend slightly beyond the pane (window's
     * absolute screen rect can exceed the target by a few pixels —
     * rounding, or the producer not knowing the target size). Clamp
     * to the target's viewport before submitting. */
    float tx0 = target->viewport.x;
    float ty0 = target->viewport.y;
    float tx1 = target->viewport.x + target->viewport.w;
    float ty1 = target->viewport.y + target->viewport.h;
    float sx0 = vx > tx0 ? vx : tx0;
    float sy0 = vy > ty0 ? vy : ty0;
    float sx1 = (vx + w) < tx1 ? (vx + w) : tx1;
    float sy1 = (vy + h) < ty1 ? (vy + h) : ty1;
    if (sx1 <= sx0 || sy1 <= sy0) {
        /* Entirely off-pane — nothing visible. Skip draw cleanly. */
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBufferDescriptor cb_desc_skip = {0};
        WGPUCommandBuffer cb_skip = wgpuCommandEncoderFinish(enc, &cb_desc_skip);
        wgpuQueueSubmit(g->queue, 1, &cb_skip);
        wgpuCommandBufferRelease(cb_skip);
        wgpuCommandEncoderRelease(enc);
        self->dirty = 0;
        return YETTY_OK_VOID();
    }
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)sx0, (uint32_t)sy0, (uint32_t)(sx1 - sx0),
                                        (uint32_t)(sy1 - sy0));

    WGPURenderPipeline pipe = g->binder->ops->get_pipeline(g->binder);
    WGPUBuffer quad_vb = g->binder->ops->get_quad_vertex_buffer(g->binder);
    ydebug("ygrid_render: pipe=%p quad_vb=%p scissor=(%u,%u,%u,%u)", (void *)pipe, (void *)quad_vb,
           (uint32_t)sx0, (uint32_t)sy0, (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0));
    wgpuRenderPassEncoderSetPipeline(pass, pipe);
    if (quad_vb) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);
    }
    struct yetty_ycore_void_result br = g->binder->ops->bind(g->binder, pass, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ygrid_render: binder bind");
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cb_desc);
    wgpuQueueSubmit(g->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    /* Complex-prim pass — each instance has its own pipeline. Anchor at
     * the figure's rect origin (rect.min.x, rect.min.y); the instance's
     * own bounds_x/y within its prim payload is already widget-local,
     * so the on-screen position is figure_rect.min + bounds. */
    for (uint32_t i = 0; i < g->figure_instance_count; i++) {
        struct yetty_ydraw_figure *inst = g->figure_instances[i];
        if (!inst || !inst->render) {
            continue;
        }
        float sx = self->rect.min.x + inst->bounds.min.x;
        float sy = self->rect.min.y + inst->bounds.min.y;
        struct yetty_ycore_void_result fr = inst->render(inst, target, sx, sy);
        if (YETTY_IS_ERR(fr)) {
            ydebug("ygrid_render: figure instance render failed: %s", fr.error.msg);
            yetty_ycore_error_destroy(fr.error);
        }
        inst->dirty = 0;
    }

    self->dirty = 0;
    return YETTY_OK_VOID();
}

/* Add a flat ADD record to the entity in `parent_slot`. Handles the
 * u32-alignment padding ygrid_add_record requires. */
static struct yetty_ycore_void_result process_add_record(struct yetty_ygrid_grid *g,
                                                         uint32_t parent_slot, const uint8_t *bytes,
                                                         size_t rec_size)
{
    uint32_t saved = g->current_entity_slot;
    g->current_entity_slot = parent_slot;
    size_t padded = (rec_size + 3u) & ~(size_t)3u;
    struct yetty_ycore_void_result ar;
    if (padded == rec_size) {
        ar = yetty_ygrid_add_record_local(g, bytes, rec_size);
    } else {
        uint8_t scratch[64];
        if (padded <= sizeof(scratch)) {
            memcpy(scratch, bytes, rec_size);
            memset(scratch + rec_size, 0, padded - rec_size);
            ar = yetty_ygrid_add_record_local(g, scratch, padded);
        } else {
            uint8_t *heap = (uint8_t *)malloc(padded);
            if (!heap) {
                g->current_entity_slot = saved;
                return YETTY_ERR(yetty_ycore_void, "ygrid_process_bytes: pad oom");
            }
            memcpy(heap, bytes, rec_size);
            memset(heap + rec_size, 0, padded - rec_size);
            ar = yetty_ygrid_add_record_local(g, heap, padded);
            free(heap);
        }
    }
    g->current_entity_slot = saved;
    return ar;
}

/* Walk a stream of records as the body of an entity scope (= parent
 * entity == `parent_slot`). Dispatches CMD_ZERO / CMD_DELETE /
 * CMD_GROUP and routes plain ADD records to process_add_record under
 * the current parent. Recurses for nested CMD_GROUP. */
static struct yetty_ycore_void_result process_group_body(struct yetty_ygrid_grid *g,
                                                         uint32_t parent_slot, const uint8_t *bytes,
                                                         size_t bytes_len)
{
    uint32_t off = 0;
    while (off < bytes_len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result pr = yetty_ydraw_drawable_command_parse(
            g->registry, bytes + off, (uint32_t)(bytes_len - off), &cmd);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid_process_bytes: parse");
        if (pr.value == 0) {
            return YETTY_ERR(yetty_ycore_void, "ygrid_process_bytes: parser made no progress");
        }

        if (cmd.kind == YETTY_YDRAW_COMMAND_DELETE) {
            struct ygrid_entity *target = entity_lookup(g, (uint64_t)cmd.id);
            if (target && target->slot != YGRID_ROOT_SLOT) {
                entity_delete_subtree(g, target->slot);
            }
            off += (uint32_t)pr.value;
            continue;
        }
        if (cmd.kind == YETTY_YDRAW_COMMAND_UPDATE) {
            /* TODO Phase 1b: in-place prim update without delete+re-add. */
            off += (uint32_t)pr.value;
            continue;
        }

        /* cmd.kind == ADD */
        uint32_t drawable_type = cmd.flyweight.data ? cmd.flyweight.data[0] : 0u;
        if (drawable_type == YETTY_YDRAW_CMD_ZERO) {
            struct yetty_ycore_void_result rc = ygrid_reset_content(&g->base);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rc, "ygrid_process_bytes: CMD_ZERO");
            /* reset_content rebuilds the root slot — recur to ROOT for any
             * records following CMD_ZERO in this body. */
            parent_slot = YGRID_ROOT_SLOT;
            off += (uint32_t)pr.value;
            continue;
        }
        if (drawable_type == YETTY_YDRAW_CMD_GROUP) {
            uint32_t id;
            uint32_t payload_size;
            memcpy(&id, &cmd.flyweight.data[1], sizeof(id));
            memcpy(&payload_size, &cmd.flyweight.data[2], sizeof(payload_size));
            uint32_t child_slot;
            int was_existing = 0;
            struct yetty_ycore_void_result cr =
                entity_lookup_or_create(g, parent_slot, (uint64_t)id, &child_slot, &was_existing);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "ygrid_process_bytes: CMD_GROUP");
            const uint8_t *body = (const uint8_t *)cmd.flyweight.data + 12u;
            struct yetty_ycore_void_result rr =
                process_group_body(g, child_slot, body, payload_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ygrid_process_bytes: CMD_GROUP body");
            /* Re-bucket descendants to the back of their cells. With
             * the parent's chrome just appended (sitting at the tail
             * of every cell it touches), any pre-existing child entity
             * whose prims were already in those cells is now BEFORE
             * the parent — wrong z-order, child gets painted under
             * the parent's body BG. The walk re-bucket fixes that:
             * for each descendant, drop its prims from cells and
             * re-append, leaving the cell tail as
             *   [parent NEW] [child NEW] [grandchild NEW] …
             * which matches the entity tree's render order. */
            if (was_existing) {
                rebucket_subtree_to_back(g, child_slot);
            }
            off += (uint32_t)pr.value;
            continue;
        }

        struct yetty_ycore_void_result ar =
            process_add_record(g, parent_slot, bytes + off, (size_t)pr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ygrid_process_bytes: add");
        off += (uint32_t)pr.value;
    }
    return YETTY_OK_VOID();
}

/* Wire entry point — body is consumed in the implicit root entity's
 * scope. CMD_GROUP records inside open named child scopes. */
static struct yetty_ycore_void_result ygrid_process_bytes(struct yetty_yfigure_figure *self,
                                                          const uint8_t *bytes, size_t bytes_len)
{
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)self;
    return process_group_body(g, YGRID_ROOT_SLOT, bytes, bytes_len);
}

/* Drop content (records, prims, complex-prim instances, per-cell
 * buckets) WITHOUT touching the GPU resource set, binder, pipeline,
 * atlases, or the font slots. Lets CREATE_CHILD on an existing id
 * refresh a widget's content in place — followed by process_bytes
 * with the new payload — so the binder cache survives and the next
 * render is a cheap upload instead of a full pipeline rebuild. */
static struct yetty_ycore_void_result ygrid_reset_content(struct yetty_yfigure_figure *self)
{
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)self;
    g->bytes_len = 0;
    g->prim_count = 0;
    for (uint32_t i = 0; i < g->figure_instance_count; i++) {
        if (g->figure_instances[i]) {
            yetty_ydraw_figure_destroy(g->figure_instances[i]);
            g->figure_instances[i] = NULL;
        }
    }
    g->figure_instance_count = 0;
    if (g->cells) {
        cells_clear(g);
    }
    /* Entity table reset: free every slot ever issued (incl. root),
     * clear the id_index, then re-allocate the implicit root at
     * slot 0 so the next process_bytes has a parent to attach to. */
    for (uint32_t i = 0; i < g->entity_high_water; i++) {
        entity_free_storage(&g->entities[i]);
        entity_init_empty(&g->entities[i], i);
    }
    g->entity_high_water = 0;
    g->free_slot_head = YGRID_INVALID_SLOT;
    if (g->id_index) {
        memset(g->id_index, 0, g->id_index_capacity * sizeof(struct ygrid_id_index_entry));
        g->id_index_count = 0;
    }
    uint32_t root_slot;
    struct yetty_ycore_void_result ar = entity_alloc_slot(g, &root_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ygrid_reset_content: alloc root slot");
    g->entities[root_slot].parent_slot = YGRID_INVALID_SLOT;
    g->staging_dirty = 1;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Dump — local string builder (same shape as the one in container.c,
 * kept local so ygrid doesn't pull yfigure_container internals).
 *=========================================================================*/

#include <stdarg.h>

static char *ygrid_dump_appendf(char *buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!buf) {
        *len = 0;
        *cap = 128;
        buf = (char *)malloc(*cap);
        if (!buf) {
            va_end(ap);
            return NULL;
        }
        buf[0] = '\0';
    }
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (need < 0) {
        va_end(ap);
        return buf;
    }
    size_t want = *len + (size_t)need + 1u;
    if (want > *cap) {
        size_t ncap = *cap ? *cap : 128;
        while (ncap < want) {
            ncap *= 2;
        }
        char *grown = (char *)realloc(buf, ncap);
        if (!grown) {
            free(buf);
            va_end(ap);
            return NULL;
        }
        buf = grown;
        *cap = ncap;
    }
    int wrote = vsnprintf(buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    if (wrote < 0) {
        return buf;
    }
    *len += (size_t)wrote;
    return buf;
}

static void ygrid_dump_pad(char *buf, size_t cap, int indent)
{
    int n = indent < 0 ? 0 : indent;
    if ((size_t)n + 1u > cap) {
        n = (int)cap - 1;
    }
    for (int i = 0; i < n; i++) {
        buf[i] = ' ';
    }
    buf[n] = '\0';
}

/* Count live prims (non-tombstoned) — a tombstoned entry sets
 * entity_slot = YGRID_INVALID_SLOT and is skipped by the staging walk. */
static uint32_t ygrid_live_prim_count(const struct yetty_ygrid_grid *g)
{
    uint32_t live = 0;
    for (uint32_t i = 0; i < g->prim_count; i++) {
        if (g->prims[i].entity_slot != YGRID_INVALID_SLOT) {
            live++;
        }
    }
    return live;
}

static char *ygrid_dump(const struct yetty_yfigure_figure *self, int indent)
{
    const struct yetty_ygrid_grid *g = (const struct yetty_ygrid_grid *)self;
    char pad[64];
    ygrid_dump_pad(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = ygrid_dump_appendf(buf, &len, &cap, "%skind: ygrid\n", pad);
    if (!buf) {
        return NULL;
    }
    buf =
        ygrid_dump_appendf(buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad,
                           self->rect.min.x, self->rect.min.y, self->rect.max.x, self->rect.max.y);
    if (!buf) {
        return NULL;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad, self->dirty);
    if (!buf) {
        return NULL;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sgrid_cols: %u\n", pad, g->grid_cols);
    if (!buf) {
        return NULL;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sgrid_rows: %u\n", pad, g->grid_rows);
    if (!buf) {
        return NULL;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sprim_count: %u\n", pad, ygrid_live_prim_count(g));
    if (!buf) {
        return NULL;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sprim_count_with_tombstones: %u\n", pad,
                             g->prim_count);
    if (!buf) {
        return NULL;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sbytes_len: %zu\n", pad, g->bytes_len);
    if (!buf) {
        return NULL;
    }
    buf =
        ygrid_dump_appendf(buf, &len, &cap, "%sentity_high_water: %u\n", pad, g->entity_high_water);
    if (!buf) {
        return NULL;
    }
    /* Entities. Walk every slot up to entity_high_water; skip released
     * (free-list) slots — those have in_use=false. external_id=0 is the
     * implicit root and is always present at slot 0. The dump uses
     * id_index_lookup-style ids so a test can reason about them
     * symbolically (matches what the producer-side group_id is). */
    int any = 0;
    for (uint32_t s = 0; s < g->entity_high_water; s++) {
        if (g->entities[s].in_use) {
            any = 1;
            break;
        }
    }
    if (!any) {
        buf = ygrid_dump_appendf(buf, &len, &cap, "%sentities: []\n", pad);
        return buf;
    }
    buf = ygrid_dump_appendf(buf, &len, &cap, "%sentities:\n", pad);
    if (!buf) {
        return NULL;
    }
    for (uint32_t s = 0; s < g->entity_high_water; s++) {
        const struct ygrid_entity *e = &g->entities[s];
        if (!e->in_use) {
            continue;
        }
        buf = ygrid_dump_appendf(buf, &len, &cap, "%s  - slot: %u\n", pad, s);
        if (!buf) {
            return NULL;
        }
        buf = ygrid_dump_appendf(buf, &len, &cap, "%s    external_id: %llu\n", pad,
                                 (unsigned long long)e->external_id);
        if (!buf) {
            return NULL;
        }
        if (e->parent_slot == YGRID_INVALID_SLOT) {
            buf = ygrid_dump_appendf(buf, &len, &cap, "%s    parent_slot: ~\n", pad);
        } else {
            buf =
                ygrid_dump_appendf(buf, &len, &cap, "%s    parent_slot: %u\n", pad, e->parent_slot);
        }
        if (!buf) {
            return NULL;
        }
        buf = ygrid_dump_appendf(buf, &len, &cap, "%s    prim_count: %u\n", pad, e->prim_count);
        if (!buf) {
            return NULL;
        }
        if (e->children_count == 0) {
            buf = ygrid_dump_appendf(buf, &len, &cap, "%s    children: []\n", pad);
        } else {
            buf = ygrid_dump_appendf(buf, &len, &cap, "%s    children: [", pad);
            if (!buf) {
                return NULL;
            }
            for (uint32_t i = 0; i < e->children_count; i++) {
                buf = ygrid_dump_appendf(buf, &len, &cap, "%s%u", i ? ", " : "", e->children[i]);
                if (!buf) {
                    return NULL;
                }
            }
            buf = ygrid_dump_appendf(buf, &len, &cap, "]\n");
        }
        if (!buf) {
            return NULL;
        }
    }
    return buf;
}

/* yclass cross-domain override of the yfigure:render slot. Recovers the
 * typed body from the object header (body begins at obj + 1) and forwards
 * to the existing render impl. */
[[clang::annotate("override@ygrid:grid:yfigure:render")]]
static struct yetty_ycore_void_result ygrid_render_slot(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_target *target)
{
    (void)ctx;
    return ygrid_render((struct yetty_yfigure_figure *)(obj + 1), target);
}

/* yclass cross-domain override of yfigure:destroy. Body sits at obj + 1. */
[[clang::annotate("override@ygrid:grid:yfigure:destroy")]]
static struct yetty_ycore_void_result ygrid_destroy_slot(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj)
{
    (void)ctx;
    return ygrid_destroy((struct yetty_yfigure_figure *)(obj + 1));
}

static const struct yetty_yfigure_figure_ops *ygrid_ops(void)
{
    static const struct yetty_yfigure_figure_ops ops = {
        .process_bytes = ygrid_process_bytes,
        .reset_content = ygrid_reset_content,
        .dump = ygrid_dump,
    };
    return &ops;
}

/*===========================================================================
 * Resource set + pipeline setup
 *=========================================================================*/

/* Mirrors ydraw-layer.c::init_uniforms — same names, same order, same
 * types. With namespace "ydraw" the binder generates the shader-side
 * uniform field names ydraw_ydraw_grid_size, ydraw_ydraw_cell_size,
 * etc., which is what ydraw-layer.wgsl references. */
static void init_uniforms(struct yetty_ydraw_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;
    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_ROLLING_ROW_0] =
        (struct yetty_yrender_uniform){"ydraw_rolling_row_0", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_PRIM_COUNT] =
        (struct yetty_yrender_uniform){"ydraw_drawable_count", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_CZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_VIEW_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_view_size", YETTY_YRENDER_UNIFORM_VEC2};

    /* Defaults: no zoom; scroll (cz_off) and view_size are set per-frame
     * in render from the figure's rect + scroll offset. */
    rs->uniforms[U_ROLLING_ROW_0].u32 = 0;
    rs->uniforms[U_VZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_VZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_VZ_OFF].vec2[1] = 0.0f;
    rs->uniforms[U_CZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_CZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_CZ_OFF].vec2[1] = 0.0f;
    rs->uniforms[U_VIEW_SIZE].vec2[0] = 0.0f;
    rs->uniforms[U_VIEW_SIZE].vec2[1] = 0.0f;
}

/* Load the raw ydraw-layer.wgsl bytes into layer_shader_code. The
 * combined shader (font-dispatcher + this file) is assembled lazily by
 * rebuild_font_dispatcher() and updated whenever the font set changes. */
static struct yetty_ycore_void_result load_layer_shader(struct yetty_ygrid_grid *grid,
                                                        const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];
    snprintf(path, sizeof(path), "%s/ygrid.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result file_result = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, file_result, "ygrid: read ygrid.wgsl");
    grid->layer_shader_code = file_result.value;
    return YETTY_OK_VOID();
}

/* Regenerate combined shader (dispatcher + layer code) and refresh
 * rs.children with the currently active fonts. Called from ygrid_render
 * when font_generation differs from last_emitted_font_generation. The
 * resulting shader-code hash change makes the binder refinalize on the
 * next update(), which recompiles with the new dispatcher in place.
 *
 * The dispatcher itself comes from yetty_yrender_build_font_dispatcher_wgsl
 * (shared with ydraw-layer); this function deals only with collecting
 * per-slot namespaces, concatenating the dispatcher with the layer shader,
 * and wiring the active font rs's into rs.children. */
static struct yetty_ycore_void_result rebuild_font_dispatcher(struct yetty_ygrid_grid *grid)
{
    /* Resolve each active slot's font rs once. The rs pointers are
     * reused below for both the dispatcher namespace list and the
     * rs.children attachment, so we don't double-call the font op. */
    const struct yetty_ydraw_gpu_resource_set *font_rs[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    const char *slot_namespaces[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t slot = 0; slot < grid->font_count; slot++) {
        if (!grid->fonts[slot]) {
            continue;
        }
        struct yetty_yrender_gpu_resource_set_result font_rs_result =
            grid->fonts[slot]->ops->get_gpu_resource_set(grid->fonts[slot]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_rs_result, "ygrid: font get_gpu_resource_set");
        font_rs[slot] = font_rs_result.value;
        slot_namespaces[slot] = font_rs_result.value->namespace;
    }

    char *dispatcher_wgsl = NULL;
    size_t dispatcher_size = 0;
    struct yetty_ycore_void_result dispatcher_result = yetty_yrender_build_font_dispatcher_wgsl(
        slot_namespaces, grid->font_count, &dispatcher_wgsl, &dispatcher_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result, "ygrid: build font dispatcher");

    size_t combined_size = dispatcher_size + grid->layer_shader_code.size;
    char *combined_buffer = (char *)malloc(combined_size + 1u);
    if (!combined_buffer) {
        free(dispatcher_wgsl);
        return YETTY_ERR(yetty_ycore_void, "ygrid: combined shader oom");
    }
    memcpy(combined_buffer, dispatcher_wgsl, dispatcher_size);
    memcpy(combined_buffer + dispatcher_size, grid->layer_shader_code.data,
           grid->layer_shader_code.size);
    combined_buffer[combined_size] = '\0';
    free(dispatcher_wgsl);

    free(grid->combined_shader);
    grid->combined_shader = combined_buffer;
    grid->combined_shader_size = combined_size;
    yetty_yrender_shader_code_set(&grid->rs.shader, grid->combined_shader,
                                  grid->combined_shader_size);

    /* rs.children[0] = sdf_lib, [1..N] = active font rs in slot order.
     * NULL slots are skipped — the dispatcher's switch cases are sparse
     * (skipped slots fall through to the default). */
    size_t children_used = 0;
    grid->rs.children[children_used++] = &grid->sdf_lib_rs;
    for (uint32_t slot = 0; slot < grid->font_count; slot++) {
        if (!font_rs[slot]) {
            continue;
        }
        if (children_used >= YETTY_YRENDER_RS_MAX_CHILDREN) {
            break;
        }
        grid->rs.children[children_used++] = (struct yetty_ydraw_gpu_resource_set *)font_rs[slot];
    }
    grid->rs.children_count = children_used;
    grid->last_emitted_font_generation = grid->font_generation;
    ydebug("ygrid: rebuilt dispatcher fonts=%u children=%zu shader=%zuB", grid->font_count,
           children_used, grid->combined_shader_size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result load_sdf_lib(struct yetty_ygrid_grid *g,
                                                   const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];
    snprintf(path, sizeof(path), "%s/ysdf.gen.wgsl", shaders_dir);
    ydebug("ygrid: load_sdf_lib: shaders_dir='%s' path='%s'", shaders_dir, path);
    struct yetty_ycore_buffer_result fr = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ygrid: read ysdf.gen.wgsl");
    g->sdf_lib_code = fr.value;
    strncpy(g->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&g->sdf_lib_rs.shader, (const char *)g->sdf_lib_code.data,
                                  g->sdf_lib_code.size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_binder(struct yetty_ygrid_grid *grid)
{
    /* Mirror ydraw-layer's rs shape exactly so ydraw-layer.wgsl works
     * as our shader without modification. namespace "ydraw" gives the
     * binder-generated uniform field names the shader expects
     * (ydraw_ydraw_grid_size etc.). */
    strncpy(grid->rs.namespace, "ydraw", YETTY_YRENDER_NAME_MAX - 1);
    grid->rs.buffer_count = 2;
    strncpy(grid->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(grid->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    grid->rs.buffers[0].readonly = 1;
    strncpy(grid->rs.buffers[1].name, "prims", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(grid->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    grid->rs.buffers[1].readonly = 1;

    init_uniforms(&grid->rs);

    grid->rs.instance_count = 1;

    /* Build the initial (no-font) dispatcher → combined shader →
     * rs.children. With no fonts attached this is just the SDF lib
     * + the layer code with stub-default helpers; the shader compiles
     * and renders SDF prims correctly. set_font() rebuilds later. */
    struct yetty_ycore_void_result dispatcher_result = rebuild_font_dispatcher(grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result, "ygrid: initial dispatcher");

    struct yetty_yrender_gpu_resource_binder_result binder_result =
        yetty_yrender_gpu_resource_binder_create(grid->device, grid->queue, grid->target_format,
                                                 grid->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, binder_result, "ygrid: binder create");
    grid->binder = binder_result.value;

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Byte buffer growth
 *=========================================================================*/

static struct yetty_ycore_void_result grow_bytes(struct yetty_ygrid_grid *g, size_t need)
{
    if (g->bytes_len + need <= g->bytes_cap) {
        return YETTY_OK_VOID();
    }
    size_t cap = g->bytes_cap ? g->bytes_cap * 2u : 256u;
    while (g->bytes_len + need > cap) {
        cap *= 2u;
    }
    uint8_t *grown = (uint8_t *)realloc(g->bytes, cap);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "ygrid: byte buffer oom");
    }
    g->bytes = grown;
    g->bytes_cap = cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_ygrid_grid_ptr_result yetty_ygrid_create(struct yetty_ycore_rectangle rect,
                                                      uint32_t grid_cols, uint32_t grid_rows,
                                                      const struct yetty_context *context)
{
    if (grid_cols == 0 || grid_rows == 0) {
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: grid dims must be non-zero");
    }
    /* Test/tooling mode: `context == NULL` (or `context->runtime == NULL`)
     * skips every GPU-touching init step — no shader load, no binder.
     * The entity tree, flyweight registry, cell bucketing, and the
     * process_bytes path all still work. Render is a no-op-on-NULL-binder
     * later; tests never call it. */
    int headless = (context == NULL) || (context->runtime == NULL);

    /* Allocate as a yclass object so the figure carries a class header
     * (enables yclass dispatch). The typed body lives at obj + 1; the
     * embedded `base` is its first member. */
    struct yetty_yclass_ptr_result grid_class_r = yetty_ygrid_grid_class_get();
    YETTY_RETURN_IF_ERR(yetty_ygrid_grid_ptr, grid_class_r, "ygrid_create: grid class");
    struct yetty_yclass_object_ptr_result grid_obj_r =
        yetty_yclass_object_alloc(grid_class_r.value);
    YETTY_RETURN_IF_ERR(yetty_ygrid_grid_ptr, grid_obj_r, "ygrid_create: object_alloc");
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)(grid_obj_r.value + 1);

    g->base.ops = ygrid_ops();
    g->base.self_obj = grid_obj_r.value;
    g->base.rect = rect;
    g->base.dirty = 1;
    g->grid_cols = grid_cols;
    g->grid_rows = grid_rows;
    g->content_scale = 1.0f;
    if (!headless) {
        g->device = context->runtime->gpu.device;
        g->queue = context->runtime->gpu.queue;
        g->target_format = context->runtime->gpu.surface_format;
        g->allocator = context->runtime->gpu.allocator;
        /* Mirror text-layer's scale-at-construction: read the host's
         * HiDPI factor once here so the receiver-side coordinate scaling
         * needs no per-host plumbing. Guard against an unset / zero value
         * leaking a degenerate scale into the wire path. */
        float scale = context->runtime->gpu.app_gpu_context.content_scale;
        if (scale > 0.0f) {
            g->content_scale = scale;
        }
    }
    g->staging_dirty = 1;
    g->free_slot_head = YGRID_INVALID_SLOT;

    /* Allocate the implicit root entity at slot 0. Plain ADD records
     * arriving outside any CMD_GROUP scope attach to it. external_id=0
     * is the root sentinel — not stored in the id_index hash. */
    {
        uint32_t root_slot;
        struct yetty_ycore_void_result ar = entity_alloc_slot(g, &root_slot);
        if (YETTY_IS_ERR(ar)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: alloc root slot", ar);
        }
        g->entities[root_slot].parent_slot = YGRID_INVALID_SLOT;
        g->entities[root_slot].external_id = 0;
    }

    /* Flyweight registry — used by process_input to walk the routed-
     * record payload as a stream of SDF / glyph / TEXT_SPAN / FONT
     * records. Same handler set the legacy compositor used for its
     * outer iterator, lifted here so each ygrid is self-sufficient. */
    {
        struct yetty_ydraw_flyweight_registry_ptr_result rr =
            yetty_ydraw_flyweight_registry_create();
        if (YETTY_IS_ERR(rr)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: registry", rr);
        }
        g->registry = rr.value;
        yetty_ydraw_flyweight_registry_set_default(g->registry, yetty_ysdf_handler);
        struct yetty_ycore_void_result hr;
        hr = yetty_ydraw_flyweight_registry_add(g->registry, YETTY_YDRAW_CMD_BASE,
                                                YETTY_YDRAW_CMD_END, yetty_ydraw_cmd_handler);
        if (YETTY_IS_ERR(hr)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: registry cmd", hr);
        }
        hr = yetty_ydraw_flyweight_registry_add(g->registry, YETTY_YDRAW_TYPE_FONT,
                                                YETTY_YDRAW_TYPE_FONT,
                                                yetty_ydraw_font_drawable_handler);
        if (YETTY_IS_ERR(hr)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: registry font", hr);
        }
        hr = yetty_ydraw_flyweight_registry_add(g->registry, YETTY_YDRAW_TYPE_TEXT_SPAN,
                                                YETTY_YDRAW_TYPE_TEXT_SPAN,
                                                yetty_ydraw_text_span_drawable_handler);
        if (YETTY_IS_ERR(hr)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: registry text", hr);
        }
        hr = yetty_ydraw_flyweight_registry_add(g->registry, YETTY_YDRAW_COMPLEX_TYPE_BASE,
                                                0xFFFFFFFFu, yetty_ydraw_raw_figure_handler);
        if (YETTY_IS_ERR(hr)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: registry complex", hr);
        }
    }

    struct yetty_ycore_void_result cr = cells_alloc(g);
    if (YETTY_IS_ERR(cr)) {
        ygrid_destroy(&g->base);
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: cells_alloc", cr);
    }
    if (!headless) {
        struct yetty_ycore_void_result lr = load_sdf_lib(g, context);
        if (YETTY_IS_ERR(lr)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: load_sdf_lib", lr);
        }
        struct yetty_ycore_void_result ls = load_layer_shader(g, context);
        if (YETTY_IS_ERR(ls)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: load_layer_shader", ls);
        }
        struct yetty_ycore_void_result br = build_binder(g);
        if (YETTY_IS_ERR(br)) {
            ygrid_destroy(&g->base);
            return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: build_binder", br);
        }
    }

    return YETTY_OK(yetty_ygrid_grid_ptr, g);
}

/* Target pixel size for one spatial-bucketing cell. The pre-Ycompositor
 * scene-canvas used the terminal text-cell grid (~12×24 px) so prims
 * bucketed across thousands of cells and the shader's per-pixel loop
 * only iterated the prims actually overlapping that pixel's cell. The
 * 1×1 grid that replaced it piled every prim into one cell, blowing
 * past the shader's 64-prim per-cell loop cap. ~32 px keeps cells
 * coarse enough to amortise the bucketing CPU work while still
 * leaving each cell with only a handful of overlapping prims. */
#define YGRID_TARGET_CELL_PX 32u

static void ygrid_dims_from_rect(struct yetty_ycore_rectangle rect, uint32_t *out_cols,
                                 uint32_t *out_rows)
{
    float w = rect.max.x - rect.min.x;
    float h = rect.max.y - rect.min.y;
    uint32_t cols =
        (uint32_t)((w + (float)YGRID_TARGET_CELL_PX - 1.0f) / (float)YGRID_TARGET_CELL_PX);
    uint32_t rows =
        (uint32_t)((h + (float)YGRID_TARGET_CELL_PX - 1.0f) / (float)YGRID_TARGET_CELL_PX);
    if (cols == 0u) {
        cols = 1u;
    }
    if (rows == 0u) {
        rows = 1u;
    }
    *out_cols = cols;
    *out_rows = rows;
}

/* Factory used by yetty_yfigure_registry_mint. `user` is a
 * (borrowed) `yetty_ygrid_factory_args *` carrying the default font and
 * (optional) complex-prim factory. NULL `user` is allowed — produces
 * an ygrid with no font and no complex-prim support, useful for tests
 * and tooling. */
static struct yetty_yfigure_figure_ptr_result ygrid_factory_impl(struct yetty_ycore_rectangle rect,
                                                                 const struct yetty_context *context,
                                                                 void *user, int absolute_coords)
{
    uint32_t grid_cols, grid_rows;
    ygrid_dims_from_rect(rect, &grid_cols, &grid_rows);
    struct yetty_ygrid_grid_ptr_result gr = yetty_ygrid_create(rect, grid_cols, grid_rows, context);
    if (YETTY_IS_ERR(gr)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "ygrid_factory: create", gr);
    }
    gr.value->absolute_coords = absolute_coords;
    if (user) {
        const struct yetty_ygrid_factory_args *args = user;
        if (args->default_font) {
            struct yetty_ycore_void_result fr =
                yetty_ygrid_set_font(gr.value, 0u, args->default_font);
            if (YETTY_IS_ERR(fr)) {
                ydebug("ygrid_factory: set_font(slot 0) failed: %s", fr.error.msg);
                yetty_ycore_error_destroy(fr.error);
            }
        }
        if (args->figure_factory) {
            yetty_ygrid_set_figure_factory(gr.value, args->figure_factory);
        }
    }
    return YETTY_OK(yetty_yfigure_figure_ptr, yetty_ygrid_as_figure(gr.value));
}

/* KIND_YGRID figures (the chrome grid + ygui widgets promoted via
 * make_figure) carry absolute-coord content. */
static struct yetty_yfigure_figure_ptr_result ygrid_factory_absolute(
    struct yetty_ycore_rectangle rect, const struct yetty_context *context, void *user)
{
    return ygrid_factory_impl(rect, context, user, 1);
}

/* Producer-kind figures (yimage/yplot/…) draw their content in local
 * coords from (0,0). */
static struct yetty_yfigure_figure_ptr_result ygrid_factory_local(
    struct yetty_ycore_rectangle rect, const struct yetty_context *context, void *user)
{
    return ygrid_factory_impl(rect, context, user, 0);
}

struct yetty_ycore_void_result yetty_ygrid_register_factory(
    struct yetty_yfigure_registry *registry, const struct yetty_ygrid_factory_args *args)
{
    return yetty_yfigure_registry_register(registry, YETTY_YFIGURE_KIND_YGRID,
                                           ygrid_factory_absolute, (void *)args);
}

struct yetty_ycore_void_result yetty_ygrid_register_factory_for_kind(
    struct yetty_yfigure_registry *registry, uint32_t kind,
    const struct yetty_ygrid_factory_args *args)
{
    return yetty_yfigure_registry_register(registry, kind, ygrid_factory_local, (void *)args);
}

struct yetty_yfigure_figure *yetty_ygrid_as_figure(struct yetty_ygrid_grid *grid)
{
    if (!grid) {
        return NULL;
    }
    return &grid->base;
}

/* Multiply one little-endian f32 wire word in place by `scale`. The
 * wire words are u32-typed storage; round-trip through a float so the
 * scaling is well-defined regardless of alignment. */
static void scale_record_word(uint32_t *word, float scale)
{
    float v;
    memcpy(&v, word, sizeof(v));
    v *= scale;
    memcpy(word, &v, sizeof(v));
}

/* Apply the receiver's HiDPI scale to every LOGICAL-pixel coordinate in
 * a freshly-appended wire record, in place, before the record is parsed
 * and staged. Doing it here (once, at the add boundary) means both the
 * spatial index built by parse_and_index_record and the bytes copied
 * verbatim into the GPU storage buffer see framebuffer-pixel
 * coordinates — no second pass, no AABB/staging divergence.
 *
 * Only geometry/position fields are touched; color, z_order, and
 * packed-index words are left intact. Handled types are exactly the
 * ones a logical-pixel producer (ygui chrome) emits:
 *
 *   GLYPH      — x, y, font_size (words 2,3,4).
 *   TEXT_SPAN  — x, y, font_size (words 2,3,4). Its expanded glyphs
 *                inherit the scaled span and are NOT re-scaled: they are
 *                generated straight into parse_and_index_record, below
 *                this boundary.
 *   SDF prims  — stroke_w (word 4) plus the geometry words from word 5
 *                onward, minus the two trailing u32 color words carried
 *                by the gradient-box variants.
 *
 * Everything else (FONT, complex figures, admin commands) falls through
 * untouched — those carry no scalable chrome coordinates here. */
static void scale_record_coords(struct yetty_ygrid_grid *g, uint32_t record_offset,
                                size_t record_len)
{
    float scale = g->content_scale;
    if (scale == 1.0f) {
        return;
    }
    uint32_t word_count = (uint32_t)(record_len / 4u);
    if (word_count < 5u) {
        return;
    }
    uint32_t *words = (uint32_t *)(g->bytes + record_offset);
    uint32_t type = words[0];

    if (type == YGRID_GLYPH_TYPE || type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        /* Both layouts place x, y, font_size at words 2, 3, 4. */
        scale_record_word(&words[2], scale);
        scale_record_word(&words[3], scale);
        scale_record_word(&words[4], scale);
        return;
    }

    /* SDF prims share the header [type, z_order, fill, stroke, stroke_w]
     * with geometry from word 5. yetty_ysdf_handler gates the type so
     * non-SDF records are skipped. */
    struct yetty_ydraw_drawable_base_ops_ptr_result ops_r = yetty_ysdf_handler(type);
    if (YETTY_IS_ERR(ops_r)) {
        yetty_ycore_error_destroy(ops_r.error);
        return;
    }
    uint32_t geom_end = word_count;
    if (type == YETTY_YSDF_LINEAR_GRADIENT_BOX || type == YETTY_YSDF_RADIAL_GRADIENT_BOX) {
        geom_end -= 2u; /* trailing u32 color words stay intact */
    }
    scale_record_word(&words[4], scale); /* stroke_w */
    for (uint32_t i = 5u; i < geom_end; ++i) {
        scale_record_word(&words[i], scale);
    }
}

struct yetty_ycore_void_result yetty_ygrid_add_record_local(struct yetty_ygrid_grid *grid,
                                                            const uint8_t *record_bytes,
                                                            size_t record_len)
{
    if (!grid || !record_bytes) {
        return YETTY_ERR(yetty_ycore_void, "ygrid_add_record: NULL arg");
    }
    if (record_len < 4u || record_len % 4u != 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygrid_add_record: record_len must be a non-zero u32-multiple");
    }
    struct yetty_ycore_void_result gr = grow_bytes(grid, record_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "ygrid_add_record: grow_bytes");

    uint32_t record_offset = (uint32_t)grid->bytes_len;
    memcpy(grid->bytes + grid->bytes_len, record_bytes, record_len);
    grid->bytes_len += record_len;

    /* Receiver-side HiDPI scaling: rewrite logical-pixel coordinates to
     * framebuffer pixels in place before the record is indexed/staged. */
    scale_record_coords(grid, record_offset, record_len);

    struct yetty_ycore_void_result pr = parse_and_index_record(grid, record_offset, record_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid_add_record: parse_and_index");

    grid->staging_dirty = 1;
    grid->base.dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygrid_clear_local(struct yetty_ygrid_grid *grid)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "ygrid_clear: NULL arg");
    }
    grid->bytes_len = 0;
    grid->prim_count = 0;
    cells_clear(grid);
    grid->staging_dirty = 1;
    grid->base.dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygrid_set_font(struct yetty_ygrid_grid *grid, uint32_t slot,
                                                    struct yetty_ydraw_font *font)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "ygrid_set_font: NULL grid");
    }
    /* Cap at the rs.children[] capacity minus the SDF lib slot. */
    if (slot >= YETTY_YRENDER_RS_MAX_CHILDREN - 1u) {
        return YETTY_ERR(yetty_ycore_void, "ygrid_set_font: slot out of range");
    }

    grid->fonts[slot] = font;
    /* font_count is the high watermark used by the dispatcher loop —
     * keep it ≥ slot+1 when assigning; clearing a tail slot doesn't
     * shrink it (NULL slots fall through to the default case anyway). */
    if (font && (slot + 1u) > grid->font_count) {
        grid->font_count = slot + 1u;
    }
    grid->font_generation++;
    grid->base.dirty = 1;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * yclass slot overrides
 *
 * One wrapper impl per ygrid public/vtable method whose signature can
 * survive RPC marshalling. The wrapper conforms to the yclass slot
 * shape `(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object
 * *obj, …)`, then forwards to the existing C impl below. Keeping the
 * old impls intact preserves the legacy callsites that still pass a
 * raw `struct yetty_yfigure_figure *` while every figure-kind gets
 * ported one at a time.
 *
 * Skipped (signature incompatible with the wire, see codegen
 * validate_method):
 *   - render(self, target*)         — `target*` is a per-frame GPU
 *                                     pointer, not RPC-able.
 *   - process_input(self, sm*)      — `sm*` is a wire-statemachine
 *                                     coroutine pointer.
 *   - dump(self, indent) → char*    — pointer return.
 *   - set_font(grid, slot, font*)   — font isn't a yclass object yet.
 *   - set_figure_factory(grid, f*)  — same; the factory pointer has no
 *                                     yclass identity.
 *   - as_figure(grid) → figure*     — pure cast helper, no dispatch.
 *
 * Recovery cast: today every figure is still calloc'd by the legacy
 * factories so the user-data starts at offset 0 of the blob. Once the
 * yclass-allocated layout takes over (yclass_object header at offset
 * 0, user data at offset sizeof(yclass_object)), this cast becomes
 *   container_of(obj, struct yetty_ygrid_grid, base) + 1
 * — but no yclass-dispatched callsite exists yet, so the legacy cast
 * is correct for every current caller.
 *=========================================================================*/

[[clang::annotate("override@ygrid:grid:add_record")]]
static struct yetty_ycore_void_result yetty_ygrid_grid_add_record_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, struct yetty_ycore_buffer record)
{
    (void)ctx;
    struct yetty_ygrid_grid *grid = (struct yetty_ygrid_grid *)obj;
    return yetty_ygrid_add_record_local(grid, record.data, record.size);
}

[[clang::annotate("override@ygrid:grid:clear")]]
static struct yetty_ycore_void_result yetty_ygrid_grid_clear_impl(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ygrid_grid *grid = (struct yetty_ygrid_grid *)obj;
    return yetty_ygrid_clear_local(grid);
}

[[clang::annotate("override@ygrid:grid:destroy")]]
static struct yetty_ycore_void_result yetty_ygrid_grid_destroy_impl(struct yetty_yclass_ctx *ctx,
                                                                    struct yetty_yclass_object *obj)
{
    (void)ctx;
    return ygrid_destroy((struct yetty_yfigure_figure *)obj);
}

[[clang::annotate("override@ygrid:grid:process_bytes")]]
static struct yetty_ycore_void_result yetty_ygrid_grid_process_bytes_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ycore_buffer payload)
{
    (void)ctx;
    return ygrid_process_bytes((struct yetty_yfigure_figure *)obj, payload.data, payload.size);
}

[[clang::annotate("override@ygrid:grid:reset_content")]]
static struct yetty_ycore_void_result yetty_ygrid_grid_reset_content_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    return ygrid_reset_content((struct yetty_yfigure_figure *)obj);
}

#include "grid.gen.c"
