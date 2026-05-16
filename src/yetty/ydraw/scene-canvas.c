/* scene-canvas.c — non-scrolling canvas with named entities.
 *
 * See include/yetty/ydraw/scene-canvas.h for the public contract.
 *
 * Layout
 * ------
 *
 * Entities form a tree (parent / children). Each entity owns its own
 * drawable sequence (arena + prims[]) and a touched-cells list (which
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
#include <yetty/ydraw/flyweight.h>
#include <yetty/ydraw/scene-canvas.h>
#include <stdio.h>

#include <yetty/yconfig/config.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/compat.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ytrace/ytrace.h>
#if YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh-gen.h>
#endif
#if YETTY_HAS_YMSDF_GEN
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ymsdf/generator.h>
#endif

#define SCENE_ROOT_SLOT 0u
#define SCENE_INVALID_SLOT UINT32_MAX

/* Glyph layout — internal SDF type emitted by TEXT_SPAN expansion.
 * Same shape as the scrolling-canvas glyph drawable:
 *   words[0] = type (SCENE_YSDF_GLYPH = 200)
 *   words[1] = z_order
 *   words[2] = x (float)
 *   words[3] = y (float — absolute, no rolling-row math on scene-canvas)
 *   words[4] = font_size
 *   words[5] = packed (glyph_idx in low 16, (font_slot+1) in high 16)
 *   words[6] = color
 */
#define SCENE_YSDF_GLYPH 200u
#define SCENE_GLYPH_WORDS 7u

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
    bool in_use;
    uint32_t next_free;

    uint32_t *children;
    uint32_t children_count;
    uint32_t children_capacity;

    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;

    struct scene_prim *prims;
    uint32_t drawable_count;
    uint32_t drawable_capacity;

    struct scene_touched_cell *touched_cells;
    uint32_t touched_cell_count;
    uint32_t touched_cell_capacity;
};

struct scene_canvas {
    /* Polymorphic handle MUST be first — vtable methods downcast at offset 0. */
    struct yetty_ydraw_canvas base;

    /* Dimensions + dirty flag. */
    struct yetty_ycore_pixel_size cell_size;
    struct yetty_ycore_grid_size grid_size;
    bool dirty;

    /* GPU staging buffers (render path not yet wired). */
    uint32_t *grid_staging;
    uint32_t grid_staging_count;
    uint32_t grid_staging_capacity;
    uint32_t *drawable_staging;
    uint32_t drawable_staging_count;
    uint32_t drawable_staging_capacity;

    /* Opaque spatial index. */
    struct yetty_ydraw_scene_grid *grid;

    /* Entity table + free-list. Slot 0 = implicit root. */
    struct yetty_ydraw_scene_entity *entities;
    uint32_t entity_capacity;
    uint32_t free_slot_head;

    /* Wire dispatch resources. The registry is shared by the
     * drawable-iterator (handler lookup) and by the GROUP-body inner-loop
     * parser. The factory builds figure instances (yplot/yimage/ymesh)
     * for figure-typed drawables. */
    struct yetty_ydraw_flyweight_registry *flyweight_registry;
    struct yetty_ydraw_figure_factory *figure_factory;

    /* Font cache + default font (slot 0). Scene-canvas does not evict —
     * fonts loaded here live for the canvas's lifetime. */
    struct yetty_yfont_cache *font_cache;
    struct yetty_ydraw_font *default_font;
    yetty_yfont_cache_handle default_handle;
    int font_render_method; /* 0=MSDF, 1=raster */
    float raster_base_size;
    char shaders_dir[512];
    char fonts_dir[512];
    char font_family[128];
    struct yetty_ymsdf_generator *msdf_generator; /* borrowed from gpu_context */

    /* Wire font-id → cache handle. Populated by FONT prims, consumed by
     * TEXT_SPAN. Permanent for the canvas's lifetime — once a font_id
     * resolves, it stays. */
    struct {
        char hex[17];
        bool resolved;
        struct yetty_ydraw_font *font;
        yetty_yfont_cache_handle handle;
    } *font_map;
    uint32_t font_map_capacity;
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

static struct yetty_ycore_void_result grow_u32(uint32_t **arr, uint32_t *cap, uint32_t need)
{
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = *cap ? *cap * 2 : 8;
    while (new_cap < need) {
        new_cap *= 2;
    }
    uint32_t *grown = realloc(*arr, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: u32 grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grow_prims(struct scene_prim **arr, uint32_t *cap,
                                                 uint32_t need)
{
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = *cap ? *cap * 2 : 8;
    while (new_cap < need) {
        new_cap *= 2;
    }
    struct scene_prim *grown = realloc(*arr, new_cap * sizeof(struct scene_prim));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: prims grow failed");
    }
    *arr = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grow_touched(struct scene_touched_cell **arr, uint32_t *cap,
                                                   uint32_t need)
{
    if (need <= *cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = *cap ? *cap * 2 : 16;
    while (new_cap < need) {
        new_cap *= 2;
    }
    struct scene_touched_cell *grown = realloc(*arr, new_cap * sizeof(struct scene_touched_cell));
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
    e->slot = slot;
    e->parent_slot = SCENE_INVALID_SLOT;
    e->next_free = SCENE_INVALID_SLOT;
    e->in_use = false;
}

static struct yetty_ycore_void_result scene_grow_entities(struct scene_canvas *sc, uint32_t need)
{
    if (need <= sc->entity_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = sc->entity_capacity ? sc->entity_capacity * 2 : 16;
    while (new_cap < need) {
        new_cap *= 2;
    }
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

static struct yetty_ycore_void_result scene_alloc_slot(struct scene_canvas *sc, uint32_t *out)
{
    if (sc->free_slot_head != SCENE_INVALID_SLOT) {
        uint32_t slot = sc->free_slot_head;
        sc->free_slot_head = sc->entities[slot].next_free;
        sc->entities[slot].next_free = SCENE_INVALID_SLOT;
        sc->entities[slot].in_use = true;
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
    e->drawable_count = e->drawable_capacity = 0;
    e->touched_cells = NULL;
    e->touched_cell_count = e->touched_cell_capacity = 0;
    e->children = NULL;
    e->children_count = e->children_capacity = 0;
}

static void scene_release_slot(struct scene_canvas *sc, uint32_t slot)
{
    struct yetty_ydraw_scene_entity *e = &sc->entities[slot];
    entity_free_storage(e);
    e->in_use = false;
    e->external_id = 0;
    e->parent_slot = SCENE_INVALID_SLOT;
    e->next_free = sc->free_slot_head;
    sc->free_slot_head = slot;
}

/*===========================================================================
 * Entity clear — detach this entity's buckets from every touched cell.
 *===========================================================================*/

static void entity_clear_in_place(struct scene_canvas *sc, struct yetty_ydraw_scene_entity *e)
{
    for (uint32_t i = 0; i < e->touched_cell_count; i++) {
        yetty_ydraw_scene_grid_drop_at(sc->grid, e->touched_cells[i].row, e->touched_cells[i].col,
                                       e->slot);
    }
    e->touched_cell_count = 0;
    e->arena_count = 0;
    e->drawable_count = 0;
    sc->dirty = true;
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

/*===========================================================================
 * Font-blob utilities (mirror scrolling-canvas — scene-canvas owns its
 * own font cache for the "self-contained" rule).
 *===========================================================================*/

static int scene_blob_is_raster(const char *name, int canvas_method)
{
    if (name) {
        size_t n = strlen(name);
        if (n >= 4) {
            const char *ext = name + n - 4;
            if (strcasecmp(ext, ".ttf") == 0 || strcasecmp(ext, ".otf") == 0) {
                return 1;
            }
            if (strcasecmp(ext, ".cdb") == 0) {
                return 0;
            }
        }
    }
    return canvas_method;
}

static uint64_t scene_fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void scene_sanitize_identifier(char *dst, size_t dst_cap, const char *src)
{
    size_t ni = 0;
    for (const char *s = src; *s && ni + 1 < dst_cap; s++) {
        char c = *s;
        dst[ni++] = (c == '-' || c == ' ' || c == '.') ? '_' : c;
    }
    dst[ni] = '\0';
}

static struct yetty_yfont_cache_ref_result scene_get_default_font_ref(struct scene_canvas *sc)
{
    if (sc->font_render_method == 1) {
        return YETTY_ERR(yetty_yfont_cache_ref,
                         "raster default font not yet wired through font cache");
    }
    char cdb_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", sc->fonts_dir,
             sc->font_family);
    char ns[128];
    scene_sanitize_identifier(ns, sizeof(ns), sc->font_family);
    ydebug("scene-canvas: default msdf font cdb='%s' key='%s'", cdb_path, ns);
    return yetty_yfont_cache_get_font(sc->font_cache, ns, cdb_path);
}

static struct yetty_ycore_void_result scene_ensure_blob_font_cdb(struct scene_canvas *sc,
                                                                 const uint8_t *ttf,
                                                                 uint32_t ttf_len,
                                                                 const char *hint_name,
                                                                 char out_hex[17])
{
    if (!ttf || ttf_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "blob is empty");
    }
    if (scene_blob_is_raster(hint_name, sc->font_render_method)) {
        return YETTY_ERR(yetty_ycore_void, "raster blob fonts not yet wired through font cache");
    }
    uint64_t h = scene_fnv1a64(ttf, ttf_len);
    snprintf(out_hex, 17, "%016llx", (unsigned long long)h);

    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_ycore_void, "no cache dir");
    }
    char fonts_dir[768];
    snprintf(fonts_dir, sizeof(fonts_dir), "%s/ydraw-fonts", cache_dir);
    char ttf_path[1024], cdb_path[1024];
    snprintf(ttf_path, sizeof(ttf_path), "%s/pdf_%s.ttf", fonts_dir, out_hex);
    snprintf(cdb_path, sizeof(cdb_path), "%s/pdf_%s.cdb", fonts_dir, out_hex);

    if (yetty_yplatform_file_exists(cdb_path)) {
        return YETTY_OK_VOID();
    }

    yetty_yplatform_mkdir_p(fonts_dir);
    if (!yetty_yplatform_file_exists(ttf_path)) {
        FILE *f = fopen(ttf_path, "wb");
        if (!f) {
            return YETTY_ERR(yetty_ycore_void, "open ttf cache for write");
        }
        size_t written = fwrite(ttf, 1, ttf_len, f);
        if (fclose(f) != 0) {
            return YETTY_ERR(yetty_ycore_void, "fclose ttf cache failed");
        }
        if (written != ttf_len) {
            return YETTY_ERR(yetty_ycore_void, "short write ttf cache");
        }
    }

#if YETTY_HAS_YMSDF_GEN
    if (!sc->msdf_generator) {
        return YETTY_ERR(yetty_ycore_void, "no MSDF generator available");
    }
    struct yetty_ymsdf_generator_config gen = {0};
    gen.ttf_path = ttf_path;
    gen.cdb_path = cdb_path;
    gen.font_size = 32.0f;
    gen.pixel_range = 4.0f;
    struct yetty_ycore_void_result gr = sc->msdf_generator->ops->generate(sc->msdf_generator, &gen);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "msdf generator failed");
    return YETTY_OK_VOID();
#else
    return YETTY_ERR(yetty_ycore_void, "ymsdf-gen disabled in this build");
#endif
}

static struct yetty_yfont_cache_ref_result scene_resolve_blob_font_handle(struct scene_canvas *sc,
                                                                          const char *hex)
{
    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_yfont_cache_ref, "no cache dir");
    }
    char cdb_path[1024];
    snprintf(cdb_path, sizeof(cdb_path), "%s/ydraw-fonts/pdf_%s.cdb", cache_dir, hex);
    return yetty_yfont_cache_get_font(sc->font_cache, hex, cdb_path);
}

static struct yetty_ycore_void_result scene_font_map_grow(struct scene_canvas *sc, uint32_t want)
{
    if (want <= sc->font_map_capacity) return YETTY_OK_VOID();
    uint32_t new_cap = sc->font_map_capacity ? sc->font_map_capacity * 2 : 8;
    while (new_cap < want) new_cap *= 2;
    void *grown = realloc(sc->font_map, new_cap * sizeof(*sc->font_map));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: font_map grow");
    }
    sc->font_map = grown;
    for (uint32_t i = sc->font_map_capacity; i < new_cap; i++) {
        sc->font_map[i].hex[0] = '\0';
        sc->font_map[i].resolved = false;
        sc->font_map[i].font = NULL;
        sc->font_map[i].handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    }
    sc->font_map_capacity = new_cap;
    return YETTY_OK_VOID();
}

static void scene_canvas_destroy_internals(struct scene_canvas *sc)
{
    if (sc->grid) {
        yetty_ydraw_scene_grid_destroy(sc->grid);
        sc->grid = NULL;
    }
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            entity_free_storage(&sc->entities[i]);
        }
    }
    free(sc->entities);
    sc->entities = NULL;
    sc->entity_capacity = 0;
    free(sc->grid_staging);
    sc->grid_staging = NULL;
    free(sc->drawable_staging);
    sc->drawable_staging = NULL;
    if (sc->font_cache) {
        for (uint32_t i = 0; i < sc->font_map_capacity; i++) {
            if (sc->font_map[i].resolved) {
                yetty_yfont_cache_release_font(sc->font_cache, sc->font_map[i].handle);
            }
        }
        if (sc->default_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) {
            yetty_yfont_cache_release_font(sc->font_cache, sc->default_handle);
            sc->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
        }
        yetty_yfont_cache_destroy(sc->font_cache);
        sc->font_cache = NULL;
    }
    free(sc->font_map);
    sc->font_map = NULL;
    sc->font_map_capacity = 0;
    if (sc->figure_factory) {
        yetty_ydraw_figure_factory_destroy(sc->figure_factory);
        sc->figure_factory = NULL;
    }
    if (sc->flyweight_registry) {
        yetty_ydraw_flyweight_registry_destroy(sc->flyweight_registry);
        sc->flyweight_registry = NULL;
    }
}

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
    sc->base.ops = &scene_canvas_ops;
    sc->dirty = true;
    sc->free_slot_head = SCENE_INVALID_SLOT;
    sc->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    sc->raster_base_size = 32.0f;

    /* Reserve slot 0 for the root. */
    struct yetty_ycore_void_result gr = scene_grow_entities(sc, 1);
    if (YETTY_IS_ERR(gr)) {
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: root reserve failed", gr);
    }
    sc->entities[SCENE_ROOT_SLOT].in_use = true;
    sc->entities[SCENE_ROOT_SLOT].external_id = 0;
    sc->entities[SCENE_ROOT_SLOT].parent_slot = SCENE_INVALID_SLOT;

    /* Opaque grid (zero-size until set_grid_size). */
    struct yetty_ydraw_scene_grid_ptr_result grid_res = yetty_ydraw_scene_grid_create();
    if (YETTY_IS_ERR(grid_res)) {
        scene_canvas_destroy_internals(sc);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: grid create failed", grid_res);
    }
    sc->grid = grid_res.value;

    /* Flyweight registry — handler lookup used by both the wire iter
     * (drawable-iterator) and the inner-loop GROUP-body parser. */
    struct yetty_ydraw_flyweight_registry_ptr_result fw_res = yetty_ydraw_flyweight_create();
    if (YETTY_IS_ERR(fw_res)) {
        scene_canvas_destroy_internals(sc);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "scene-canvas: flyweight create failed", fw_res);
    }
    sc->flyweight_registry = fw_res.value;

    /* Figure factory + built-in concrete factories (yplot, yimage, ymesh).
     * Figures arriving on the wire route through this factory at
     * dispatch time. */
    struct yetty_ydraw_figure_factory_ptr_result factory_res = yetty_ydraw_figure_factory_create(
        context->gpu_context.device, context->gpu_context.queue,
        context->gpu_context.surface_format, context->gpu_context.allocator);
    if (YETTY_IS_ERR(factory_res)) {
        scene_canvas_destroy_internals(sc);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "scene-canvas: figure factory create failed", factory_res);
    }
    sc->figure_factory = factory_res.value;

    {
        struct yetty_ydraw_concrete_factory *f = yetty_yplot_factory_create();
        if (!f) {
            scene_canvas_destroy_internals(sc);
            free(sc);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: yplot factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_figure_factory_register(sc->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_yplot_factory_destroy(f);
            scene_canvas_destroy_internals(sc);
            free(sc);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: yplot register", rr);
        }
    }
    {
        struct yetty_ydraw_concrete_factory *f = yetty_yimage_factory_create();
        if (!f) {
            scene_canvas_destroy_internals(sc);
            free(sc);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: yimage factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_figure_factory_register(sc->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_yimage_factory_destroy(f);
            scene_canvas_destroy_internals(sc);
            free(sc);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: yimage register", rr);
        }
    }
#if YETTY_HAS_YMESH
    {
        struct yetty_ydraw_concrete_factory *f = yetty_ymesh_factory_create();
        if (!f) {
            scene_canvas_destroy_internals(sc);
            free(sc);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: ymesh factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_figure_factory_register(sc->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_ymesh_factory_destroy(f);
            scene_canvas_destroy_internals(sc);
            free(sc);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: ymesh register", rr);
        }
    }
#endif

    /* Resource dirs + font config from yconfig. */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = config->ops->font_family(config);
    if (!font_family || strcmp(font_family, "default") == 0) {
        font_family = "DejaVuSansMNerdFontMono";
    }
    const char *render_method = config->ops->get_string(config, "ydraw/font/render-method", "msdf");
    strncpy(sc->shaders_dir, shaders_dir, sizeof(sc->shaders_dir) - 1);
    strncpy(sc->fonts_dir, fonts_dir, sizeof(sc->fonts_dir) - 1);
    strncpy(sc->font_family, font_family, sizeof(sc->font_family) - 1);
    sc->font_render_method = (strcmp(render_method, "raster") == 0) ? 1 : 0;
    sc->msdf_generator = context->gpu_context.msdf_generator;

    /* Per-canvas font cache. */
    struct yetty_yfont_cache_ptr_result cache_res = yetty_yfont_cache_create(shaders_dir);
    if (YETTY_IS_ERR(cache_res)) {
        scene_canvas_destroy_internals(sc);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: font cache create", cache_res);
    }
    sc->font_cache = cache_res.value;

    /* Default font (slot 0). */
    struct yetty_yfont_cache_ref_result def_res = scene_get_default_font_ref(sc);
    if (YETTY_IS_ERR(def_res)) {
        scene_canvas_destroy_internals(sc);
        free(sc);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scene-canvas: default font create", def_res);
    }
    sc->default_font = def_res.value.font;
    sc->default_handle = def_res.value.handle;

    return YETTY_OK(yetty_ydraw_canvas_ptr, &sc->base);
}

static struct yetty_ycore_void_result scene_destroy(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_OK_VOID();
    }
    struct scene_canvas *sc = as_scene(base);
    scene_canvas_destroy_internals(sc);
    free(sc);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Config
 *===========================================================================*/

static struct yetty_ycore_void_result scene_set_cell_size(struct yetty_ydraw_canvas *base,
                                                          struct yetty_ycore_pixel_size pixel_size)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    }
    if (pixel_size.width <= 0.0f || pixel_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cell size must be > 0");
    }
    struct scene_canvas *sc = as_scene(base);
    sc->cell_size = pixel_size;
    sc->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_grid_size(struct yetty_ydraw_canvas *base,
                                                          struct yetty_ycore_grid_size size)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    }
    struct scene_canvas *sc = as_scene(base);

    /* Resizing wipes content: every cell's bucket layout depends on grid
     * dims. Easier than migrating. */
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            sc->entities[i].arena_count = 0;
            sc->entities[i].drawable_count = 0;
            sc->entities[i].touched_cell_count = 0;
        }
    }

    sc->grid_size = size;
    struct yetty_ycore_void_result gr =
        yetty_ydraw_scene_grid_set_size(sc->grid, (uint32_t)size.rows, (uint32_t)size.cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: grid set_size");
    sc->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_pixel_size scene_get_cell_size(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->cell_size : (struct yetty_ycore_pixel_size){0, 0};
}

static struct yetty_ycore_grid_size scene_get_grid_size(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->grid_size : (struct yetty_ycore_grid_size){0, 0};
}

/*===========================================================================
 * Entity API
 *===========================================================================*/

struct yetty_ydraw_scene_entity *yetty_ydraw_scene_canvas_root(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return NULL;
    }
    return &as_scene(canvas)->entities[SCENE_ROOT_SLOT];
}

struct yetty_ydraw_scene_entity *yetty_ydraw_scene_entity_lookup(struct yetty_ydraw_canvas *canvas,
                                                                 uint64_t external_id)
{
    if (!canvas) {
        return NULL;
    }
    struct scene_canvas *sc = as_scene(canvas);
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use && sc->entities[i].external_id == external_id) {
            return &sc->entities[i];
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result entity_push_child(struct yetty_ydraw_scene_entity *parent,
                                                        uint32_t child_slot)
{
    struct yetty_ycore_void_result gr =
        grow_u32(&parent->children, &parent->children_capacity, parent->children_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: push_child");
    parent->children[parent->children_count++] = child_slot;
    return YETTY_OK_VOID();
}

static void entity_remove_child(struct yetty_ydraw_scene_entity *parent, uint32_t child_slot)
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
    struct yetty_ydraw_canvas *canvas, struct yetty_ydraw_scene_entity *parent,
    uint64_t external_id)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr, "scene-canvas: NULL");
    }
    struct scene_canvas *sc = as_scene(canvas);
    if (!parent) {
        parent = &sc->entities[SCENE_ROOT_SLOT];
    }

    if (external_id != 0 && yetty_ydraw_scene_entity_lookup(canvas, external_id) != NULL) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr, "scene-canvas: external_id already in use");
    }

    uint32_t parent_slot = parent->slot;
    uint32_t slot = SCENE_INVALID_SLOT;
    struct yetty_ycore_void_result ar = scene_alloc_slot(sc, &slot);
    if (YETTY_IS_ERR(ar)) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr, "scene-canvas: alloc slot failed", ar);
    }
    /* Re-fetch parent — scene_alloc_slot may have reallocated entities[]. */
    parent = &sc->entities[parent_slot];

    struct yetty_ydraw_scene_entity *e = &sc->entities[slot];
    e->external_id = external_id;
    e->parent_slot = parent_slot;

    struct yetty_ycore_void_result cr = entity_push_child(parent, slot);
    if (YETTY_IS_ERR(cr)) {
        scene_release_slot(sc, slot);
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr, "scene-canvas: link to parent failed", cr);
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
    struct yetty_ycore_void_result tr =
        grow_touched(&ctx->entity->touched_cells, &ctx->entity->touched_cell_capacity,
                     ctx->entity->touched_cell_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "scene-canvas: touched grow");
    ctx->entity->touched_cells[ctx->entity->touched_cell_count++] = (struct scene_touched_cell){
        .row = (uint16_t)row,
        .col = (uint16_t)col,
    };
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Add drawable
 *===========================================================================*/

/* Lower-level: append raw drawable bytes + a pre-computed AABB to the
 * entity. Used by add_prim (which derives both from a flyweight's ops)
 * and by the TEXT_SPAN glyph expansion (which derives them directly
 * from the glyph payload + font metadata, without going through a
 * registered flyweight). */
static struct yetty_ycore_void_result scene_entity_add_bytes(
    struct scene_canvas *sc, struct yetty_ydraw_scene_entity *entity, const uint32_t *data,
    uint32_t word_count, struct yetty_ycore_rectangle aabb)
{
    if (!entity || !data) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to add_bytes");
    }
    if (word_count == 0) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: word_count is 0");
    }
    if (sc->cell_size.width <= 0.0f || sc->cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cell size is 0");
    }
    if (sc->grid_size.cols == 0 || sc->grid_size.rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: grid size is 0");
    }

    if (aabb.min.y > aabb.max.y) {
        float tmp = aabb.min.y;
        aabb.min.y = aabb.max.y;
        aabb.max.y = tmp;
    }

    float cell_w = sc->cell_size.width;
    float cell_h = sc->cell_size.height;
    uint32_t cols = sc->grid_size.cols;
    uint32_t rows = sc->grid_size.rows;

    if (aabb.max.y < 0.0f || aabb.max.x < 0.0f) {
        return YETTY_OK_VOID();
    }
    if (aabb.min.y >= (float)rows * cell_h || aabb.min.x >= (float)cols * cell_w) {
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
        grow_u32(&entity->arena, &entity->arena_capacity, entity->arena_count + word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gar, "scene-canvas: arena grow");
    uint32_t arena_offset = entity->arena_count;
    memcpy(&entity->arena[arena_offset], data, word_count * sizeof(uint32_t));
    entity->arena_count += word_count;

    /* Append placement record. */
    struct yetty_ycore_void_result gpr =
        grow_prims(&entity->prims, &entity->drawable_capacity, entity->drawable_count + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpr, "scene-canvas: prims grow");
    uint32_t local_idx = entity->drawable_count;
    entity->prims[local_idx] = (struct scene_prim){
        .arena_offset = arena_offset,
        .word_count = word_count,
        .type = data[0],
    };
    entity->drawable_count++;

    /* Insert into the grid via opaque API. */
    struct fresh_ctx ctx = {.entity = entity};
    struct yetty_ycore_void_result ir = yetty_ydraw_scene_grid_insert(
        sc->grid, entity->slot, local_idx, row_min, row_max, col_min, col_max, on_fresh_cell, &ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "scene-canvas: grid_insert");

    sc->dirty = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scene_entity_add_prim(
    struct yetty_ydraw_canvas *canvas, struct yetty_ydraw_scene_entity *entity,
    const struct yetty_ydraw_drawable_flyweight *fw)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to add_prim");
    }
    if (!fw || !fw->data || !fw->ops || !fw->ops->size || !fw->ops->aabb) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: invalid flyweight");
    }
    struct scene_canvas *sc = as_scene(canvas);

    struct yetty_ycore_size_result sz_res = fw->ops->size(fw->data);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sz_res, "scene-canvas: prim size");
    uint32_t word_count = (uint32_t)(sz_res.value / sizeof(uint32_t));

    struct rectangle_result aabb_res = fw->ops->aabb(fw->data);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, aabb_res, "scene-canvas: prim aabb");
    return scene_entity_add_bytes(sc, entity, fw->data, word_count, aabb_res.value);
}

/*===========================================================================
 * Clear / delete
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_scene_entity_clear(
    struct yetty_ydraw_canvas *canvas, struct yetty_ydraw_scene_entity *entity)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to clear");
    }
    entity_clear_in_place(as_scene(canvas), entity);
    return YETTY_OK_VOID();
}

static void entity_delete_recursive(struct scene_canvas *sc, struct yetty_ydraw_scene_entity *e)
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
    struct yetty_ydraw_canvas *canvas, struct yetty_ydraw_scene_entity *entity)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to delete");
    }
    if (entity->slot == SCENE_ROOT_SLOT) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: cannot delete root (use clear)");
    }
    struct scene_canvas *sc = as_scene(canvas);
    uint32_t parent_slot = entity->parent_slot;
    uint32_t self_slot = entity->slot;
    entity_delete_recursive(sc, entity);
    if (parent_slot != SCENE_INVALID_SLOT && parent_slot < sc->entity_capacity &&
        sc->entities[parent_slot].in_use) {
        entity_remove_child(&sc->entities[parent_slot], self_slot);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scene_entity_delete_children(
    struct yetty_ydraw_canvas *canvas, struct yetty_ydraw_scene_entity *entity)
{
    if (!canvas || !entity) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: null arg to delete_children");
    }
    struct scene_canvas *sc = as_scene(canvas);
    /* Each iteration deletes one child subtree. The recursive delete
     * does not touch the parent's children list, so we manage popping
     * from the back to keep slot validity invariant. */
    while (entity->children_count > 0) {
        uint32_t child_slot = entity->children[entity->children_count - 1];
        entity->children_count--;
        if (child_slot < sc->entity_capacity && sc->entities[child_slot].in_use) {
            entity_delete_recursive(sc, &sc->entities[child_slot]);
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Wire dispatch — outer iter loop + recursive GROUP-body inner loop.
 *
 * Outer loop reads commands from the wire-statemachine via the
 * drawable-iterator and dispatches them with the implicit-root entity
 * as the current parent. When dispatch encounters an ADD whose
 * flyweight type is CMD_GROUP, it looks up / creates the entity, then
 * walks the GROUP's payload bytes as nested commands via an inner
 * (in-memory) parsing loop. Nested GROUPs recurse. DELETE removes the
 * named entity. Other ADDs append the drawable to the current entity.
 *===========================================================================*/

/* Forward declare — dispatch_command and process_group_body call each
 * other (recursion through GROUP). */
static struct yetty_ycore_void_result process_group_body(
    struct scene_canvas *sc, struct yetty_ydraw_scene_entity *parent, const uint8_t *body_bytes,
    uint32_t body_len);

/* Lookup an in-use entity by external_id; returns root for id==0;
 * NULL if not found. */
static struct yetty_ydraw_scene_entity *scene_lookup_entity(struct scene_canvas *sc,
                                                            uint64_t external_id)
{
    if (external_id == 0) {
        return &sc->entities[SCENE_ROOT_SLOT];
    }
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use && sc->entities[i].external_id == external_id) {
            return &sc->entities[i];
        }
    }
    return NULL;
}

/* Return the entity for `external_id`, creating it as a child of
 * `parent` if it doesn't exist yet. Re-OPEN: an existing entity is
 * returned as-is so contents are appended (ydraw.md §7). The root id
 * (0) is reserved and rejected here. */
static struct yetty_ydraw_scene_entity_ptr_result scene_lookup_or_create_entity(
    struct scene_canvas *sc, struct yetty_ydraw_scene_entity *parent, uint64_t external_id)
{
    if (external_id == 0) {
        return YETTY_ERR(yetty_ydraw_scene_entity_ptr,
                         "scene-canvas: GROUP id=0 is reserved for root");
    }
    struct yetty_ydraw_scene_entity *existing = scene_lookup_entity(sc, external_id);
    if (existing) {
        return YETTY_OK(yetty_ydraw_scene_entity_ptr, existing);
    }
    return yetty_ydraw_scene_entity_create(&sc->base, parent, external_id);
}

/* Forward decl — used by dispatch_command. */
static struct yetty_ycore_void_result scene_clear(struct yetty_ydraw_canvas *base);

/* Expand a TEXT_SPAN view into one glyph drawable per codepoint, each
 * added to `entity` via the standard add-prim path. Scene-canvas has no
 * cursor and no rolling row — coordinates from the TEXT_SPAN payload
 * are taken as absolute. */
static struct yetty_ycore_void_result scene_expand_text_span_to_glyphs(
    struct scene_canvas *sc, struct yetty_ydraw_scene_entity *entity,
    const struct yetty_ydraw_text_span_drawable_view *ts, struct yetty_ydraw_font *font,
    yetty_yfont_cache_handle font_handle)
{
    static uint32_t glyph_z_order = 0;
    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0) ? ts->font_size / base_size : 1.0f;
    float cursor_x = ts->x;

    const uint8_t *ptr = (const uint8_t *)ts->text;
    const uint8_t *end = ptr + ts->text_len;

    while (ptr < end) {
        uint32_t cp = 0;
        if ((*ptr & 0x80) == 0) {
            cp = *ptr++;
        } else if ((*ptr & 0xE0) == 0xC0) {
            cp = (*ptr++ & 0x1F) << 6;
            if (ptr < end) cp |= (*ptr++ & 0x3F);
        } else if ((*ptr & 0xF0) == 0xE0) {
            cp = (*ptr++ & 0x0F) << 12;
            if (ptr < end) cp |= (*ptr++ & 0x3F) << 6;
            if (ptr < end) cp |= (*ptr++ & 0x3F);
        } else if ((*ptr & 0xF8) == 0xF0) {
            cp = (*ptr++ & 0x07) << 18;
            if (ptr < end) cp |= (*ptr++ & 0x3F) << 12;
            if (ptr < end) cp |= (*ptr++ & 0x3F) << 6;
            if (ptr < end) cp |= (*ptr++ & 0x3F);
        } else {
            ptr++;
            continue;
        }

        struct uint32_result gi_res = font->ops->get_glyph_index(font, cp);
        if (YETTY_IS_ERR(gi_res)) {
            yetty_ycore_error_destroy(gi_res.error);
            cursor_x += (ts->font_size * 0.25f) + ts->char_spacing;
            if (cp == 0x20) cursor_x += ts->word_spacing;
            continue;
        }
        uint32_t glyph_index = gi_res.value;

        struct yetty_yrender_gpu_resource_set_result rs_res = font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "scene-canvas: text-span gpu_resource_set");
        const struct yetty_ydraw_gpu_resource_set *rs = rs_res.value;
        if (rs->buffer_count == 0 || !rs->buffers[0].data) {
            return YETTY_ERR(yetty_ycore_void,
                             "scene-canvas: font resource set has no glyph metadata buffer");
        }
        const float *meta = (const float *)rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(rs->buffers[0].size / (6 * sizeof(float)));
        if (glyph_index >= meta_count) {
            return YETTY_ERR(yetty_ycore_void, "scene-canvas: glyph_index out of metadata range");
        }
        const float *gm = meta + glyph_index * 6;
        float size_x = gm[0], size_y = gm[1];
        float bearing_x = gm[2], bearing_y = gm[3];
        float advance = gm[4];

        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * scale;
            continue;
        }

        float gx = cursor_x + bearing_x * scale;
        float gy = ts->y - bearing_y * scale;
        float gw = size_x * scale;
        float gh = size_y * scale;

        uint32_t slot = (font_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) ? font_handle : 0u;
        uint32_t glyph_data[SCENE_GLYPH_WORDS];
        glyph_data[0] = SCENE_YSDF_GLYPH;
        glyph_data[1] = glyph_z_order++;
        memcpy(&glyph_data[2], &gx, sizeof(gx));
        memcpy(&glyph_data[3], &gy, sizeof(gy));
        memcpy(&glyph_data[4], &ts->font_size, sizeof(ts->font_size));
        uint32_t packed_gf = (glyph_index & 0xFFFF) | (((uint32_t)(slot + 1) & 0xFFFF) << 16);
        glyph_data[5] = packed_gf;
        memcpy(&glyph_data[6], &ts->color, sizeof(ts->color));

        /* AABB from the glyph's pixel rect — direct, no registry lookup. */
        struct yetty_ycore_rectangle gabb = {
            .min = {.x = gx, .y = gy},
            .max = {.x = gx + gw, .y = gy + gh},
        };
        struct yetty_ycore_void_result ar =
            scene_entity_add_bytes(sc, entity, glyph_data, SCENE_GLYPH_WORDS, gabb);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "scene-canvas: add glyph");

        cursor_x += advance * scale + ts->char_spacing;
        if (cp == 0x20) cursor_x += ts->word_spacing;
    }
    return YETTY_OK_VOID();
}

/* FONT prim: declare a font for use by subsequent TEXT_SPANs in any
 * envelope. The font ref is held by the canvas's font_map for its
 * lifetime (scene-canvas does not evict). */
static struct yetty_ycore_void_result scene_handle_font(
    struct scene_canvas *sc, const struct yetty_ydraw_drawable_flyweight *fw)
{
    struct yetty_ydraw_font_drawable_view fv;
    if (yetty_ydraw_font_drawable_parse(fw->data, &fv) != 0 || fv.font_id < 0) {
        return YETTY_OK_VOID();
    }
    char hex[17];
    if (fv.ttf_len == 0) {
        /* Hash-ref form: 16-char hex hash directly. */
        if (fv.name_len != 16) {
            return YETTY_ERR(yetty_ycore_void,
                             "scene-canvas: FONT ttf_len=0 but name is not a 16-char hex hash");
        }
        memcpy(hex, fv.name, 16);
        hex[16] = '\0';
    } else {
        char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
        size_t hl = fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
        memcpy(hint, fv.name, hl);
        hint[hl] = '\0';
        struct yetty_ycore_void_result er =
            scene_ensure_blob_font_cdb(sc, fv.ttf, fv.ttf_len, hint, hex);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "scene-canvas: ensure_blob_font_cdb");
    }
    struct yetty_ycore_void_result gr = scene_font_map_grow(sc, (uint32_t)fv.font_id + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: font_map grow");
    /* If a prior FONT prim with this same font_id already resolved a
     * different hash, the original ref stays — the new prim becomes a
     * no-op. This matches ygui's expectation that font_ids are stable. */
    if (!sc->font_map[fv.font_id].resolved) {
        memcpy(sc->font_map[fv.font_id].hex, hex, 17);
    }
    return YETTY_OK_VOID();
}

/* Resolve a TEXT_SPAN's font_id to a (font, handle). Loads on demand,
 * caches in font_map. Returns default font when id is missing or
 * unresolved. */
static struct yetty_ycore_void_result scene_resolve_text_span_font(
    struct scene_canvas *sc, int32_t font_id, struct yetty_ydraw_font **out_font,
    yetty_yfont_cache_handle *out_handle)
{
    if (font_id < 0 || (uint32_t)font_id >= sc->font_map_capacity ||
        sc->font_map[font_id].hex[0] == '\0') {
        *out_font = sc->default_font;
        *out_handle = sc->default_handle;
        return YETTY_OK_VOID();
    }
    if (!sc->font_map[font_id].resolved) {
        struct yetty_yfont_cache_ref_result rr =
            scene_resolve_blob_font_handle(sc, sc->font_map[font_id].hex);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "scene-canvas: resolve_blob_font_handle");
        sc->font_map[font_id].font = rr.value.font;
        sc->font_map[font_id].handle = rr.value.handle;
        sc->font_map[font_id].resolved = true;
    }
    *out_font = sc->font_map[font_id].font;
    *out_handle = sc->font_map[font_id].handle;
    return YETTY_OK_VOID();
}

/* Apply one parsed command. `current_entity` is the top of the parser
 * stack (root for the outer loop, the GROUP's entity inside a body). */
static struct yetty_ycore_void_result dispatch_command(
    struct scene_canvas *sc, struct yetty_ydraw_scene_entity *current_entity,
    const struct yetty_ydraw_command *cmd)
{
    if (cmd->kind == YETTY_YDRAW_COMMAND_DELETE) {
        struct yetty_ydraw_scene_entity *target = scene_lookup_entity(sc, cmd->id);
        if (!target) {
            /* §7: unknown id is a warn-and-continue, not fatal. */
            ydebug("scene-canvas: DELETE id=%u: not found, ignoring", cmd->id);
            return YETTY_OK_VOID();
        }
        if (target->slot == SCENE_ROOT_SLOT) {
            ydebug("scene-canvas: DELETE id=0 (root): ignoring (use CMD_ZERO to clear)");
            return YETTY_OK_VOID();
        }
        return yetty_ydraw_scene_entity_delete(&sc->base, target);
    }

    /* ADD — switch on the flyweight type to find any special verbs. */
    if (!cmd->flyweight.data) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: ADD with NULL flyweight data");
    }
    uint32_t drawable_type = cmd->flyweight.data[0];

    if (drawable_type == YETTY_YDRAW_CMD_ZERO) {
        return scene_clear(&sc->base);
    }
    if (drawable_type == YETTY_YDRAW_CMD_GROUP) {
        uint32_t id;
        uint32_t payload_size;
        memcpy(&id, &cmd->flyweight.data[1], sizeof(id));
        memcpy(&payload_size, &cmd->flyweight.data[2], sizeof(payload_size));
        struct yetty_ydraw_scene_entity_ptr_result ent_res =
            scene_lookup_or_create_entity(sc, current_entity, (uint64_t)id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ent_res, "scene-canvas: GROUP lookup/create");
        const uint8_t *body = (const uint8_t *)cmd->flyweight.data + 12u;
        return process_group_body(sc, ent_res.value, body, payload_size);
    }
    if (drawable_type == YETTY_YDRAW_TYPE_FONT) {
        return scene_handle_font(sc, &cmd->flyweight);
    }
    if (drawable_type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        struct yetty_ydraw_text_span_drawable_view tv;
        if (yetty_ydraw_text_span_drawable_parse(cmd->flyweight.data, &tv) != 0) {
            return YETTY_OK_VOID();
        }
        struct yetty_ydraw_font *font = NULL;
        yetty_yfont_cache_handle handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
        struct yetty_ycore_void_result fr =
            scene_resolve_text_span_font(sc, tv.font_id, &font, &handle);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "scene-canvas: resolve text-span font");
        if (!font) {
            /* No font available — silently drop the span. */
            return YETTY_OK_VOID();
        }
        return scene_expand_text_span_to_glyphs(sc, current_entity, &tv, font, handle);
    }
    /* Implicit Add — append drawable to the current parent. */
    return yetty_ydraw_scene_entity_add_prim(&sc->base, current_entity, &cmd->flyweight);
}

/* Walk a GROUP's payload as a stream of nested commands. Recurses on
 * sub-GROUPs via dispatch_command. */
static struct yetty_ycore_void_result process_group_body(
    struct scene_canvas *sc, struct yetty_ydraw_scene_entity *parent, const uint8_t *body_bytes,
    uint32_t body_len)
{
    uint32_t offset = 0;
    while (offset < body_len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result pr = yetty_ydraw_drawable_command_parse(
            sc->flyweight_registry, body_bytes + offset, body_len - offset, &cmd);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "scene-canvas: group body parse");
        if (pr.value == 0) {
            return YETTY_ERR(yetty_ycore_void, "scene-canvas: group body parser returned 0");
        }
        struct yetty_ycore_void_result dr = dispatch_command(sc, parent, &cmd);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "scene-canvas: group body dispatch");
        offset += (uint32_t)pr.value;
    }
    return YETTY_OK_VOID();
}

/* Persistent per-layer coroutine entry point. One envelope per outer
 * iteration: iter_init, drain commands, iter_destroy, yield, repeat.
 * Yielding after each envelope lets the wire-statemachine clear its
 * terminator-seen flag and queue the next envelope's body. */
static struct yetty_ycore_void_result scene_process_input(
    struct yetty_ydraw_canvas *base, struct yetty_ywire_wire_statemachine *sm)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scene_process_input: NULL canvas");
    }
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "scene_process_input: NULL wire_statemachine");
    }
    struct scene_canvas *sc = as_scene(base);

    for (;;) {
        struct yetty_ydraw_drawable_iterator iter = {0};
        struct yetty_ycore_void_result ret = YETTY_OK_VOID();

        struct yetty_ycore_void_result ir =
            yetty_ydraw_drawable_iterator_init(&iter, sm, sc->flyweight_registry);
        if (YETTY_IS_ERR(ir)) {
            ret = YETTY_ERR(yetty_ycore_void, "scene_process_input: iter init", ir);
            goto cleanup;
        }

        struct yetty_ydraw_scene_entity *root = &sc->entities[SCENE_ROOT_SLOT];

        for (;;) {
            struct yetty_ydraw_drawable_iterator_status_result sr =
                yetty_ydraw_drawable_iterator_next(&iter);
            if (YETTY_IS_ERR(sr)) {
                ret = YETTY_ERR(yetty_ycore_void, "scene_process_input: iter_next", sr);
                goto cleanup;
            }
            if (sr.value == YETTY_YDRAW_ITERATOR_EOE) {
                break;
            }
            struct yetty_ycore_void_result dr = dispatch_command(sc, root, &iter.command);
            if (YETTY_IS_ERR(dr)) {
                ret = YETTY_ERR(yetty_ycore_void, "scene_process_input: dispatch", dr);
                goto cleanup;
            }
        }

        sc->dirty = true;

cleanup:
        yetty_ydraw_drawable_iterator_destroy(&iter);
        if (YETTY_IS_ERR(ret)) {
            return ret;
        }
        /* Envelope handled cleanly — yield so sm_coro can clear its
         * terminator-seen state before resuming us on the next body. */
        yetty_yplatform_coro_yield();
    }
}

static struct yetty_ycore_void_result scene_clear(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_OK_VOID();
    }
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

static uint32_t scene_drawable_count(const struct yetty_ydraw_canvas *base)
{
    const struct scene_canvas *sc = as_scene_const(base);
    if (!sc) {
        return 0;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < sc->entity_capacity; i++) {
        if (sc->entities[i].in_use) {
            total += sc->entities[i].drawable_count;
        }
    }
    return total;
}

/* Staging build — produce the same flat (drawable_staging, grid_staging)
 * shape that scrolling-canvas produces, so the shader is shared.
 *
 * drawable_staging layout (matches scrolling-grid's build_drawable_staging):
 *   words[0..drawable_count) — per-drawable offset into the data section
 *   data section (starts at drawable_count words in):
 *     [rolling_row | payload words…] for each drawable, packed.
 *
 * Scene-canvas has no scrollback, so rolling_row is always 0. The
 * concat order is: walk entities by slot ascending; within each entity,
 * walk drawables 0..drawable_count-1.
 *
 * grid_staging is built by scene-grid via entity_base[slot] = the
 * first global drawable index belonging to that entity's slot.
 */
static struct yetty_ycore_void_result scene_build_staging_pass(struct scene_canvas *sc)
{
    /* Pass 1: count drawables and accumulate total payload words per
     * entity, while filling in entity_base[]. */
    uint32_t entity_cap = sc->entity_capacity;
    uint32_t *entity_base = NULL;
    if (entity_cap > 0) {
        entity_base = calloc(entity_cap, sizeof(uint32_t));
        if (!entity_base) {
            return YETTY_ERR(yetty_ycore_void, "scene-canvas: entity_base alloc");
        }
    }
    uint32_t total_drawables = 0;
    uint32_t total_payload_words = 0;
    for (uint32_t s = 0; s < entity_cap; s++) {
        if (!sc->entities[s].in_use) {
            entity_base[s] = total_drawables; /* harmless — unused slot */
            continue;
        }
        entity_base[s] = total_drawables;
        const struct yetty_ydraw_scene_entity *e = &sc->entities[s];
        total_drawables += e->drawable_count;
        for (uint32_t p = 0; p < e->drawable_count; p++) {
            total_payload_words += e->prims[p].word_count + 1u; /* +1 for rolling_row slot */
        }
    }

    /* drawable_staging = drawable_count words of offsets + payload data. */
    uint32_t drawable_staging_words = total_drawables + total_payload_words;
    if (drawable_staging_words == 0) {
        sc->drawable_staging_count = 0;
    } else {
        if (drawable_staging_words > sc->drawable_staging_capacity) {
            uint32_t new_cap =
                sc->drawable_staging_capacity ? sc->drawable_staging_capacity * 2u : 256u;
            while (new_cap < drawable_staging_words) new_cap *= 2u;
            uint32_t *grown = realloc(sc->drawable_staging, new_cap * sizeof(uint32_t));
            if (!grown) {
                free(entity_base);
                return YETTY_ERR(yetty_ycore_void, "scene-canvas: drawable_staging realloc");
            }
            sc->drawable_staging = grown;
            sc->drawable_staging_capacity = new_cap;
        }

        uint32_t data_offset = 0;
        uint32_t drawable_idx = 0;
        for (uint32_t s = 0; s < entity_cap; s++) {
            if (!sc->entities[s].in_use) continue;
            const struct yetty_ydraw_scene_entity *e = &sc->entities[s];
            for (uint32_t p = 0; p < e->drawable_count; p++) {
                const struct scene_prim *pr = &e->prims[p];
                /* Per-drawable offset into the data section. */
                sc->drawable_staging[drawable_idx] = data_offset;
                /* rolling_row slot — scene-canvas has no scroll → 0. */
                sc->drawable_staging[total_drawables + data_offset] = 0u;
                /* Payload words follow. */
                memcpy(&sc->drawable_staging[total_drawables + data_offset + 1u],
                       &e->arena[pr->arena_offset], pr->word_count * sizeof(uint32_t));
                data_offset += pr->word_count + 1u;
                drawable_idx++;
            }
        }
        sc->drawable_staging_count = drawable_staging_words;
    }

    /* grid_staging — delegate to the opaque scene-grid, passing entity_base. */
    uint32_t grid_count = 0;
    struct yetty_ycore_void_result gr =
        yetty_ydraw_scene_grid_rebuild_staging(sc->grid, entity_base, entity_cap,
                                               &sc->grid_staging, &sc->grid_staging_capacity,
                                               &grid_count);
    free(entity_base);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "scene-canvas: grid rebuild_staging");
    sc->grid_staging_count = grid_count;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_rebuild_grid(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    }
    struct scene_canvas *sc = as_scene(base);
    if (!sc->dirty && sc->grid_staging_count > 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = scene_build_staging_pass(sc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "scene-canvas: rebuild_grid");
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

static struct yetty_ydraw_drawable_staging_result scene_build_drawable_staging(
    struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ydraw_drawable_staging, "scene-canvas: NULL");
    }
    struct scene_canvas *sc = as_scene(base);
    /* drawable_staging is built jointly with grid_staging by rebuild_grid.
     * Re-run if either staging is missing or canvas is dirty. */
    if (sc->dirty || sc->drawable_staging_count == 0) {
        struct yetty_ycore_void_result r = scene_build_staging_pass(sc);
        YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_staging, r,
                            "scene-canvas: build_drawable_staging");
        sc->dirty = false;
    }
    struct yetty_ydraw_drawable_staging view = {
        .data = sc->drawable_staging,
        .word_count = sc->drawable_staging_count,
    };
    return YETTY_OK(yetty_ydraw_drawable_staging, view);
}

static uint32_t scene_drawable_gpu_size(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->drawable_staging_count * (uint32_t)sizeof(uint32_t) : 0;
}

static struct yetty_ycore_void_result scene_mark_dirty(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL");
    }
    as_scene(base)->dirty = true;
    return YETTY_OK_VOID();
}

static bool scene_is_dirty(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->dirty : false;
}

static uint32_t scene_font_count(const struct yetty_ydraw_canvas *base)
{
    return base ? yetty_yfont_cache_count(as_scene_const(base)->font_cache) : 0;
}

static uint32_t scene_font_generation(const struct yetty_ydraw_canvas *base)
{
    return base ? yetty_yfont_cache_generation(as_scene_const(base)->font_cache) : 0;
}

static struct yetty_ydraw_font *scene_get_font_at(const struct yetty_ydraw_canvas *base,
                                                  uint32_t slot)
{
    if (!base) return NULL;
    return yetty_yfont_cache_font_at(as_scene_const(base)->font_cache,
                                     (yetty_yfont_cache_handle)slot);
}

static struct yetty_ydraw_font *scene_get_default_font(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->default_font : NULL;
}

static const struct yetty_ydraw_flyweight_registry *scene_get_flyweight_registry(
    const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->flyweight_registry : NULL;
}

static struct yetty_ydraw_figure_factory *scene_get_figure_factory(
    const struct yetty_ydraw_canvas *base)
{
    return base ? as_scene_const(base)->figure_factory : NULL;
}

static uint32_t scene_figure_count(const struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ydraw_figure_instance *scene_get_figure(const struct yetty_ydraw_canvas *base,
                                                            uint32_t index)
{
    (void)base;
    (void)index;
    return NULL;
}

static struct yetty_ycore_void_result scene_for_each_glyph(struct yetty_ydraw_canvas *base,
                                                           yetty_ydraw_canvas_glyph_visitor visitor,
                                                           void *user)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL base");
    }
    if (!visitor) {
        return YETTY_ERR(yetty_ycore_void, "scene-canvas: NULL visitor");
    }
    struct scene_canvas *sc = as_scene(base);
    for (uint32_t s = 0; s < sc->entity_capacity; s++) {
        if (!sc->entities[s].in_use) continue;
        const struct yetty_ydraw_scene_entity *e = &sc->entities[s];
        for (uint32_t p = 0; p < e->drawable_count; p++) {
            const struct scene_prim *pr = &e->prims[p];
            if (pr->word_count < SCENE_GLYPH_WORDS) continue;
            if (pr->type != SCENE_YSDF_GLYPH) continue;
            const uint32_t *w = &e->arena[pr->arena_offset];
            float gx;
            float gy;
            uint32_t packed;
            memcpy(&gx, &w[2], sizeof(gx));
            memcpy(&gy, &w[3], sizeof(gy));
            memcpy(&packed, &w[5], sizeof(packed));
            uint32_t glyph_idx = packed & 0xFFFFu;
            uint32_t slot_plus_one = (packed >> 16) & 0xFFFFu;
            int32_t font_slot = slot_plus_one ? (int32_t)(slot_plus_one - 1u) : -1;
            struct yetty_ydraw_glyph_view view = {
                .x = gx,
                .y = gy,
                .glyph_idx = glyph_idx,
                .font_slot = font_slot,
            };
            visitor(&view, user);
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Cursor / scroll no-ops
 *===========================================================================*/

static struct yetty_ycore_void_result scene_set_cursor_pos(struct yetty_ydraw_canvas *base,
                                                           struct yetty_ycore_grid_cursor_pos pos)
{
    (void)base;
    (void)pos;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_scroll_lines(struct yetty_ydraw_canvas *base,
                                                         uint16_t num_lines)
{
    (void)base;
    (void)num_lines;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_view_top(struct yetty_ydraw_canvas *base,
                                                         bool active, uint32_t view_top)
{
    (void)base;
    (void)active;
    (void)view_top;
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
    struct yetty_ydraw_canvas *base, yetty_ydraw_canvas_scroll_callback callback, void *userdata)
{
    (void)base;
    (void)callback;
    (void)userdata;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_set_cursor_callback(
    struct yetty_ydraw_canvas *base, yetty_ydraw_canvas_cursor_callback callback, void *userdata)
{
    (void)base;
    (void)callback;
    (void)userdata;
    return YETTY_OK_VOID();
}

static const struct yetty_ydraw_canvas_ops scene_canvas_ops = {
    .name = "scene",
    .destroy = scene_destroy,
    .set_cell_size = scene_set_cell_size,
    .set_grid_size = scene_set_grid_size,
    .get_cell_size = scene_get_cell_size,
    .get_grid_size = scene_get_grid_size,
    .process_input = scene_process_input,
    .set_cursor_pos = scene_set_cursor_pos,
    .scroll_lines = scene_scroll_lines,
    .set_view_top = scene_set_view_top,
    .rolling_row_0 = scene_rolling_row_0,
    .live_rolling_row_0 = scene_live_rolling_row_0,
    .set_scroll_callback = scene_set_scroll_callback,
    .set_cursor_callback = scene_set_cursor_callback,
    .mark_dirty = scene_mark_dirty,
    .is_dirty = scene_is_dirty,
    .rebuild_grid = scene_rebuild_grid,
    .grid_data = scene_grid_data,
    .grid_word_count = scene_grid_word_count,
    .build_drawable_staging = scene_build_drawable_staging,
    .drawable_gpu_size = scene_drawable_gpu_size,
    .clear = scene_clear,
    .drawable_count = scene_drawable_count,
    .font_count = scene_font_count,
    .font_generation = scene_font_generation,
    .get_font_at = scene_get_font_at,
    .get_default_font = scene_get_default_font,
    .get_flyweight_registry = scene_get_flyweight_registry,
    .get_figure_factory = scene_get_figure_factory,
    .figure_count = scene_figure_count,
    .get_figure = scene_get_figure,
    .for_each_glyph = scene_for_each_glyph,
};
