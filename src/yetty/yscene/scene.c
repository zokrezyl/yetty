/*
 * scene.c — yclass class `yscene:scene`: the retained-scene renderer
 * figure kind and the module's single public object (#691).
 *
 * One scene owns exactly one dom (internal.h) — the GPU-free retained
 * content tree. Every client-facing content operation is a method on
 * the scene, applied to that dom in-process; no dom handle ever crosses
 * RPC/FFI. The host frame loop (the figure container's per-frame walk)
 * calls render; producers only mutate + commit.
 *
 * Responsibilities in this TU:
 *   - the public mutation surface (typed methods → dom functions);
 *   - the legacy wire-envelope adapter (process_bytes: embedded
 *     CMD_GROUP / CMD_DELETE / CMD_ZERO bodies → typed mutations);
 *   - DERIVE: walk the committed tree, accumulate world transform /
 *     clip / opacity top-down, emit one flat paint-ordered leaf array
 *     (sorted by the converged key (paint_z, paint_seq, record_index));
 *   - hit-testing over the derived world state (reverse paint order);
 *   - dump_state for tests.
 *
 * View transform: scroll and view scale live on the scene as VIEW
 * state (screen = (document - scroll) * view_scale). They never touch
 * the dom and never trigger re-derivation — scrolling re-emits nothing,
 * which is the point of the whole design.
 *
 * GPU path (this increment): staging is rebuilt from the SORTED leaf
 * array — buffer order IS paint order, so the shader composites
 * correctly with no per-cell sort and no creation-order band-aid. The
 * staging layout, uniforms, and shader contract are ydraw-layer's
 * (shared machinery: binder + ygrid.wgsl + ysdf lib), which keeps the
 * scene renderable by the existing pipeline while the yscene-specific
 * shader evolution (per-prim effective clip, z-run interleave for
 * complex leaves) lands in the next increments.
 *
 * NOT here yet: complex-leaf instances and CMD_UPDATE routing,
 * font-resource installation + TEXT→glyph expansion, rotation/shear in
 * the staging transform (derive/hit-test already handle them; the
 * staging path bakes translate+scale only), incremental (dirty-subtree)
 * derive — the dom already tracks the dirt for it.
 */
#include <yetty/yclass/class.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ydraw-core/font-resource.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ymsdf/generator.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yrender/font-dispatcher.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ysdf/default-registry.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/registry.h>
#include "yetty/gen/impl/yfigure/figure.h"

#include "internal.h"

/* GLYPH staging-record type — matches ydraw-layer.wgsl's YDRAW_SDF_GLYPH
 * (7 words: type, layer, x, y, font_size, packed glyph|slot, color).
 * Generated at staging from TEXT_DRAWABLE_LIST leaves; never stored in
 * the dom's master spans. */
#define YSCENE_GLYPH_TYPE 200u
#define YSCENE_GLYPH_WORDS 7u

/* Spatial cull grid dimensions (narrow-phase buckets over the content
 * extent). Fixed for now; sized adaptively in a later increment. */
#define YSCENE_GRID_COLS 16u
#define YSCENE_GRID_ROWS 16u

/* Uniform block — mirrors ydraw-layer.c / ygrid: same names, same
 * order, same shader (ygrid.wgsl). */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_ROLLING_ROW_0 2
#define U_PRIM_COUNT 3
#define U_VZ_SCALE 4
#define U_VZ_OFF 5
#define U_CZ_SCALE 6
#define U_CZ_OFF 7
#define U_VIEW_SIZE 8
#define U_TIME 9
#define U_POST_FX_INDEX 10
#define U_POST_FX_P0 11
#define U_POST_FX_P1 12
#define U_POST_FX_P2 13
#define U_POST_FX_P3 14
#define U_POST_FX_P4 15
#define U_POST_FX_P5 16
#define U_COUNT 17

/*===========================================================================
 * Derived leaf — one drawable record in world form. Rebuilt by derive;
 * disposable (never the source of truth).
 *=========================================================================*/

enum yscene_leaf_kind {
    YSCENE_LEAF_PRIMITIVE = 0, /* SDF — unified-shader batchable */
    YSCENE_LEAF_TEXT,          /* TEXT_DRAWABLE_LIST — expands to glyphs at staging */
    YSCENE_LEAF_COMPLEX,       /* composite — own pipeline, z-run cut point */
};

struct yscene_leaf {
    /* Paint key, lexicographic. */
    int32_t paint_z;
    uint32_t paint_seq;
    uint32_t record_index;

    /* Source reference. */
    uint64_t node_external_id;
    uint32_t node_slot;
    uint32_t batch_slot;
    uint32_t record_offset;
    uint32_t record_size;
    uint32_t record_type;
    enum yscene_leaf_kind kind;

    /* World form. */
    float m00, m01, m10, m11, translate_x, translate_y;
    struct yetty_ycore_rectangle world_aabb;
    bool has_world_clip;
    struct yetty_ycore_rectangle world_clip;
    float world_opacity;
};

/*===========================================================================
 * The class
 *=========================================================================*/

struct YETTY_ANNOTATE("class@yscene:scene") YETTY_ANNOTATE("parent@yfigure:figure")
    yetty_yscene_scene {
    /* The owned content tree. Created lazily on first use so a shared
     * registry bound after create still reaches it. */
    struct yetty_yscene_dom *dom;

    /* Borrowed shared wire-record registry (framework's instance); the
     * derive pass and the dom fall back to an owned default when unset
     * (headless tests). */
    struct yetty_ydraw_drawable_list_registry *registry;
    bool owns_registry;

    /* View state (document → screen): never re-derives, never re-emits. */
    float scroll_x;
    float scroll_y;
    float view_scale;
    float content_w;
    float content_h;

    /* Derived flat paint-ordered leaves (valid for derived_generation).
     * `leaves` is the PUBLISHED snapshot; derive builds into the
     * scratch pair and swaps only on full success, so a failed publish
     * leaves the previous snapshot intact (review M1). */
    struct yscene_leaf *leaves;
    uint32_t leaf_count;
    uint32_t leaf_capacity;
    struct yscene_leaf *leaves_scratch;
    uint32_t leaf_scratch_count;
    uint32_t leaf_scratch_capacity;
    uint64_t derived_generation;
    bool derived_valid;

    /* Wire-envelope transaction guard (review H2): a partial apply
     * (semantic rejection or allocation failure mid-body) poisons the
     * pending state — commits refuse until a full reset (CMD_ZERO /
     * reset_content) rebuilds from scratch. */
    bool pending_poisoned;

    /* Complex (composite) leaves — own-pipeline draws at their paint
     * position. Instances are minted when their record enters the dom
     * (so same-envelope CMD_UPDATE can target them, ygrid semantics),
     * keyed by their immutable span location, swept when staging no
     * longer references that span. */
    struct yetty_ydraw_composite_factory *composite_factory; /* borrowed */
    struct yscene_complex_instance {
        struct yetty_ydraw_composite *instance;
        /* Identity = (batch_stamp, record_offset). The stamp is the
         * batch's immutable ALLOCATION id — slots recycle after zero/
         * navigation, and a slot-keyed instance would collide with the
         * next page's span at the same coordinates (the leaked-images
         * nav bug: old image marked live, new image never minted). */
        uint64_t batch_stamp;
        uint32_t batch_slot;
        uint32_t record_offset;
        uint32_t stream_ordinal; /* CMD_UPDATE address, per-body 1-based */
        /* Paint key + world anchor, refreshed at staging. */
        int32_t paint_z;
        uint32_t paint_seq;
        uint32_t record_index;
        float translate_x, translate_y;
        bool seen;
    } *complexes;
    uint32_t complex_count;
    uint32_t complex_capacity;
    uint32_t stream_next_ordinal;

    /*--- GPU state (absent in headless mode: binder == NULL) ---------*/
    struct yetty_yframework *runtime; /* borrowed — frame clock */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Resource set + shader libs — ydraw-layer's shape, so ygrid.wgsl
     * renders the scene's staging without modification. */
    struct yetty_yrender_gpu_resource_set rs;
    struct yetty_yrender_gpu_resource_set sdf_lib_rs;
    struct yetty_ycore_buffer sdf_lib_code;
    struct yetty_yrender_gpu_resource_set effects_lib_rs;
    struct yetty_ycore_buffer effects_lib_code;
    struct yetty_ycore_buffer layer_shader_code;
    char *combined_shader;
    size_t combined_shader_size;
    struct yetty_yrender_gpu_resource_binder *binder;
    bool binder_finalized;

    /* Wire-shipped fonts (FONT_RESOURCE records → MSDF slots), mirror
     * of ygrid's model: slot 0 is the host default font; producer
     * font_ids map through wire_font_slot to installed slots. The
     * dispatcher/children rebuild when font_generation moves. */
    struct yetty_yfont_font *fonts[YETTY_YRENDER_RS_MAX_CHILDREN - 1];
    /* Cache handle per wire-installed slot (0 for slot 0 / uninstalled):
     * released on reset so navigation cannot leak or resurrect fonts. */
    yetty_yfont_cache_handle font_handles[YETTY_YRENDER_RS_MAX_CHILDREN - 1];
    uint32_t font_count;
    uint32_t font_generation;
    uint32_t last_emitted_font_generation;
    struct yetty_yfont_cache *font_cache;
    struct yetty_ymsdf_generator *msdf_generator; /* borrowed */
    char shaders_dir[512];
    char cache_dir[512];
    char data_dir[512];
    int32_t wire_font_slot[YETTY_YRENDER_RS_MAX_CHILDREN];
    uint32_t next_font_slot;

    /* Staging scratch: records (verbatim + generated glyphs) accumulate
     * here in paint order during the rebuild — the offset table can
     * only be sized once TEXT expansion has run. */
    uint32_t *staging_scratch;
    size_t staging_scratch_len;
    size_t staging_scratch_cap;
    struct yscene_staged_record {
        uint32_t word_offset; /* into staging_scratch */
        struct yetty_ycore_rectangle world_aabb;
    } *staged_records;
    uint32_t staged_record_count;
    uint32_t staged_record_cap;

    /* Staging — rebuilt from the sorted leaf array whenever the derived
     * generation moves past staging_generation. Buffer order IS paint
     * order. */
    uint32_t *grid_staging;
    size_t grid_staging_words;
    size_t grid_staging_cap;
    uint32_t *prim_staging;
    size_t prim_staging_words;
    size_t prim_staging_cap;
    uint32_t staged_prim_count;
    uint64_t staging_generation;
    /* The content extent staging bucketed against — part of the staging
     * key (extent changes shift cell geometry). */
    float staged_content_w;
    float staged_content_h;
    bool staging_valid;

    /* Narrow-phase cull buckets over the content extent. */
    struct yscene_cell {
        uint32_t *indices;
        uint32_t count;
        uint32_t capacity;
    } cells[YSCENE_GRID_COLS * YSCENE_GRID_ROWS];
};

YETTY_YRESULT_DECLARE(yetty_yscene_scene_ptr, struct yetty_yscene_scene *);

/* Defined in the appended generated impl glue (foot of this TU). */
struct yetty_yclass_ptr_result yetty_yscene_scene_class_get(void);
struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yscene_scene_to(struct yetty_yscene_scene *data);

static struct yetty_yscene_scene_ptr_result scene_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_yscene_scene_class_get();
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, class_res, "yscene scene_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, slice_res, "yscene scene_from_obj: object_data");
    return YETTY_OK(yetty_yscene_scene_ptr, (struct yetty_yscene_scene *)slice_res.value);
}

/* The owned dom, created on first use (with whatever registry is bound
 * by then). */
static struct yetty_yscene_dom_ptr_result scene_dom(struct yetty_yscene_scene *scene)
{
    if (scene->dom) {
        return YETTY_OK(yetty_yscene_dom_ptr, scene->dom);
    }
    struct yetty_yscene_dom_ptr_result dom_res = yetty_yscene_dom_create(scene->registry);
    YETTY_RETURN_IF_ERR(yetty_yscene_dom_ptr, dom_res, "yscene: dom create");
    scene->dom = dom_res.value;
    return YETTY_OK(yetty_yscene_dom_ptr, scene->dom);
}

/* THE one registry-binding path (review H1): binds scene AND dom
 * together so validation, derive, and staging always step records with
 * the same strides. `owns` transfers ownership to the scene. */
static struct yetty_ycore_void_result scene_bind_registry(
    struct yetty_yscene_scene *scene, struct yetty_ydraw_drawable_list_registry *registry,
    bool owns)
{
    struct yetty_yscene_dom_ptr_result dom_res = scene_dom(scene);
    if (YETTY_IS_ERR(dom_res)) {
        if (owns) {
            yetty_ydraw_drawable_list_registry_destroy(registry);
        }
        return YETTY_ERR(yetty_ycore_void, "yscene bind_registry: dom", dom_res);
    }
    struct yetty_ycore_void_result bind_res =
        yetty_yscene_dom_set_registry(dom_res.value, registry);
    if (YETTY_IS_ERR(bind_res)) {
        if (owns) {
            yetty_ydraw_drawable_list_registry_destroy(registry);
        }
        return YETTY_ERR(yetty_ycore_void, "yscene bind_registry: dom bind", bind_res);
    }
    if (scene->owns_registry && scene->registry && scene->registry != registry) {
        yetty_ydraw_drawable_list_registry_destroy(scene->registry);
    }
    scene->registry = registry;
    scene->owns_registry = owns;
    return YETTY_OK_VOID();
}

static struct yetty_ydraw_drawable_list_registry_ptr_result scene_registry(
    struct yetty_yscene_scene *scene)
{
    if (scene->registry) {
        return YETTY_OK(yetty_ydraw_drawable_list_registry_ptr, scene->registry);
    }
    /* Headless fallback: ONE owned default, bound to scene + dom alike. */
    struct yetty_ydraw_drawable_list_registry_ptr_result created_res =
        yetty_ydraw_drawable_list_registry_create_default();
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list_registry_ptr, created_res,
                        "yscene: default registry create");
    struct yetty_ycore_void_result bind_res =
        scene_bind_registry(scene, created_res.value, /*owns=*/true);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list_registry_ptr, bind_res,
                        "yscene: default registry bind");
    return YETTY_OK(yetty_ydraw_drawable_list_registry_ptr, scene->registry);
}

/*===========================================================================
 * 2D affine helpers (world = M * local + translate)
 *=========================================================================*/

struct yscene_transform {
    float m00, m01, m10, m11, translate_x, translate_y;
};

static struct yscene_transform transform_identity(void)
{
    return (struct yscene_transform){1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
}

/* world = parent ∘ local (apply local first, then parent). */
static struct yscene_transform transform_compose(const struct yscene_transform *parent,
                                                 const struct yscene_transform *local)
{
    struct yscene_transform out;
    out.m00 = parent->m00 * local->m00 + parent->m01 * local->m10;
    out.m01 = parent->m00 * local->m01 + parent->m01 * local->m11;
    out.m10 = parent->m10 * local->m00 + parent->m11 * local->m10;
    out.m11 = parent->m10 * local->m01 + parent->m11 * local->m11;
    out.translate_x =
        parent->m00 * local->translate_x + parent->m01 * local->translate_y + parent->translate_x;
    out.translate_y =
        parent->m10 * local->translate_x + parent->m11 * local->translate_y + parent->translate_y;
    return out;
}

/* Map a local-space rect through `transform`; the world AABB of its four
 * corners (exact for translate/scale, conservative under rotation). */
static struct yetty_ycore_rectangle transform_map_rect(const struct yscene_transform *transform,
                                                       struct yetty_ycore_rectangle rect)
{
    float corner_x[4] = {rect.min.x, rect.max.x, rect.min.x, rect.max.x};
    float corner_y[4] = {rect.min.y, rect.min.y, rect.max.y, rect.max.y};
    struct yetty_ycore_rectangle out;
    for (int i = 0; i < 4; i++) {
        float world_x =
            transform->m00 * corner_x[i] + transform->m01 * corner_y[i] + transform->translate_x;
        float world_y =
            transform->m10 * corner_x[i] + transform->m11 * corner_y[i] + transform->translate_y;
        if (i == 0) {
            out.min.x = out.max.x = world_x;
            out.min.y = out.max.y = world_y;
        } else {
            out.min.x = world_x < out.min.x ? world_x : out.min.x;
            out.max.x = world_x > out.max.x ? world_x : out.max.x;
            out.min.y = world_y < out.min.y ? world_y : out.min.y;
            out.max.y = world_y > out.max.y ? world_y : out.max.y;
        }
    }
    return out;
}

static struct yetty_ycore_rectangle rect_intersect(struct yetty_ycore_rectangle first,
                                                   struct yetty_ycore_rectangle second)
{
    struct yetty_ycore_rectangle out;
    out.min.x = first.min.x > second.min.x ? first.min.x : second.min.x;
    out.min.y = first.min.y > second.min.y ? first.min.y : second.min.y;
    out.max.x = first.max.x < second.max.x ? first.max.x : second.max.x;
    out.max.y = first.max.y < second.max.y ? first.max.y : second.max.y;
    return out;
}

static bool rect_contains(struct yetty_ycore_rectangle rect, float x, float y)
{
    return x >= rect.min.x && x < rect.max.x && y >= rect.min.y && y < rect.max.y;
}

/* Defined with the wire-font machinery below; derive installs FONT
 * resources as it encounters them, resets drop them. */
static struct yetty_ycore_void_result scene_install_wire_font(
    struct yetty_yscene_scene *scene, const struct yetty_ydraw_font_resource_view *view);
static void scene_reset_wire_fonts(struct yetty_yscene_scene *scene);
static void scene_reset_complexes(struct yetty_yscene_scene *scene);

/* A full reset replaces the DOCUMENT — the view offset into the old
 * document is meaningless for the new one. Leaving it in place strands
 * a shorter page's content above the viewport ("clicked link shows a
 * blank page" from the ychromium bridge). view_scale is NOT reset: it
 * is a host/display property, not document state. */
static void scene_reset_view(struct yetty_yscene_scene *scene)
{
    scene->scroll_x = 0.0f;
    scene->scroll_y = 0.0f;
    scene->content_w = 0.0f;
    scene->content_h = 0.0f;
}

/*===========================================================================
 * Derive — tree in, flat paint-ordered world leaves out
 *=========================================================================*/

static struct yetty_ycore_void_result scene_leaf_append(struct yetty_yscene_scene *scene,
                                                        const struct yscene_leaf *leaf)
{
    if (scene->leaf_scratch_count == scene->leaf_scratch_capacity) {
        uint32_t new_capacity = scene->leaf_scratch_capacity ? scene->leaf_scratch_capacity * 2
                                                             : 64;
        struct yscene_leaf *grown =
            realloc(scene->leaves_scratch, (size_t)new_capacity * sizeof(struct yscene_leaf));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yscene derive: leaf array alloc failed");
        }
        scene->leaves_scratch = grown;
        scene->leaf_scratch_capacity = new_capacity;
    }
    scene->leaves_scratch[scene->leaf_scratch_count++] = *leaf;
    return YETTY_OK_VOID();
}

static int scene_leaf_compare(const void *first_ptr, const void *second_ptr)
{
    const struct yscene_leaf *first = first_ptr;
    const struct yscene_leaf *second = second_ptr;
    if (first->paint_z != second->paint_z) {
        return first->paint_z < second->paint_z ? -1 : 1;
    }
    if (first->paint_seq != second->paint_seq) {
        return first->paint_seq < second->paint_seq ? -1 : 1;
    }
    if (first->record_index != second->record_index) {
        return first->record_index < second->record_index ? -1 : 1;
    }
    return 0;
}

/* One frame of the iterative DFS: a node slot plus its parent's
 * accumulated world state. */
struct yscene_derive_frame {
    uint32_t slot;
    struct yscene_transform parent_world;
    bool parent_has_clip;
    struct yetty_ycore_rectangle parent_clip;
    float parent_opacity;
};

/* Emit every record of `node` (already in world form) into the leaf
 * array. Resources (FONT) produce no leaf. */
static struct yetty_ycore_void_result scene_derive_node_content(
    struct yetty_yscene_scene *scene, const struct yetty_yscene_dom *dom,
    const struct yetty_ydraw_drawable_list_registry *registry, uint32_t slot,
    const struct yscene_transform *world, bool has_clip, struct yetty_ycore_rectangle clip,
    float opacity)
{
    const struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    uint32_t record_index = 0;
    for (uint32_t batch_index = 0; batch_index < node->batch_count; batch_index++) {
        uint32_t batch_slot = node->batch_slots[batch_index];
        const struct yetty_yscene_dom_batch *batch = &dom->batches[batch_slot];
        if (batch->live_generation > dom->committed_generation) {
            /* Pending (uncommitted) content is invisible — the scene
             * renders committed generations only. (A batch REPLACED
             * mid-stream is equally invisible until commit; the fully
             * version-aware batch list arrives with incremental
             * derive.) */
            continue;
        }
        for (uint32_t i = 0; i < batch->record_count; i++) {
            uint32_t record_offset = batch->record_offsets[i];
            uint32_t record_type;
            memcpy(&record_type, batch->bytes + record_offset, sizeof(record_type));

            if (record_type == YETTY_YDRAW_RESOURCE_FONT) {
                /* Resource, not a paint leaf: install into an MSDF slot
                 * (idempotent per font_id; no-op headless) so TEXT
                 * expansion at staging finds it. */
                struct yetty_ydraw_font_resource_view font_view;
                if (yetty_ydraw_font_resource_parse(
                        (const uint32_t *)(batch->bytes + record_offset), &font_view) == 0) {
                    struct yetty_ycore_void_result install_res =
                        scene_install_wire_font(scene, &font_view);
                    if (YETTY_IS_ERR(install_res)) {
                        ydebug("yscene: wire font install failed: %s", install_res.error.msg);
                        yetty_ycore_error_destroy(install_res.error);
                    }
                }
                record_index++;
                continue;
            }

            struct yscene_leaf leaf = {
                .paint_z = node->paint_z,
                .paint_seq = batch->paint_seq,
                .record_index = record_index,
                .node_external_id = node->external_id,
                .node_slot = slot,
                .batch_slot = batch_slot,
                .record_offset = record_offset,
                .record_type = record_type,
                .kind = YSCENE_LEAF_PRIMITIVE,
                .m00 = world->m00,
                .m01 = world->m01,
                .m10 = world->m10,
                .m11 = world->m11,
                .translate_x = world->translate_x,
                .translate_y = world->translate_y,
                .has_world_clip = has_clip,
                .world_clip = clip,
                .world_opacity = opacity,
            };
            if (yetty_ydraw_is_composite(record_type)) {
                leaf.kind = YSCENE_LEAF_COMPLEX;
            } else if (record_type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
                leaf.kind = YSCENE_LEAF_TEXT;
            }

            /* Local AABB via the registry ops, mapped to world. A
             * record whose handler cannot bound it (yet) gets an empty
             * AABB — it still occupies its paint position. */
            struct yetty_ydraw_command command;
            struct yetty_ycore_size_result parse_res = yetty_ydraw_drawable_command_parse(
                (const struct yetty_ydraw_drawable_list_registry *)registry,
                batch->bytes + record_offset, (uint32_t)(batch->bytes_len - record_offset),
                &command);
            if (YETTY_IS_OK(parse_res)) {
                leaf.record_size = (uint32_t)parse_res.value;
            }
            if (YETTY_IS_OK(parse_res) && command.kind == YETTY_YDRAW_COMMAND_ADD &&
                command.entry.ops && command.entry.ops->aabb) {
                struct rectangle_result aabb_res = command.entry.ops->aabb(command.entry.data);
                if (YETTY_IS_OK(aabb_res)) {
                    leaf.world_aabb = transform_map_rect(world, aabb_res.value);
                } else {
                    yetty_ycore_error_destroy(aabb_res.error);
                }
            } else if (YETTY_IS_ERR(parse_res)) {
                yetty_ycore_error_destroy(parse_res.error);
            }

            struct yetty_ycore_void_result append_res = scene_leaf_append(scene, &leaf);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "yscene derive: leaf append");
            record_index++;
        }
    }
    return YETTY_OK_VOID();
}

/* Rebuild the flat leaf array from the dom's committed state. v1 walks
 * the full tree on any generation change; the dom's per-subtree dirty
 * flags already carry what an incremental version needs. */
static struct yetty_ycore_void_result scene_derive(struct yetty_yscene_scene *scene)
{
    if (!scene->dom) {
        scene->leaf_count = 0;
        scene->derived_valid = true;
        scene->derived_generation = 0;
        return YETTY_OK_VOID();
    }
    struct yetty_yscene_dom *dom = scene->dom;
    if (scene->derived_valid && scene->derived_generation == dom->committed_generation) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_drawable_list_registry_ptr_result registry_res = scene_registry(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, registry_res, "yscene derive: registry");

    /* Build the NEW snapshot in the scratch pair; the published one
     * stays intact until the swap below (failure-atomic publish). */
    scene->leaf_scratch_count = 0;

    /* Iterative DFS with an explicit stack (producer trees can be
     * arbitrarily deep). */
    struct yscene_derive_frame *stack = NULL;
    uint32_t stack_count = 0;
    uint32_t stack_capacity = 0;
    struct yscene_derive_frame root_frame = {
        .slot = YETTY_YSCENE_DOM_ROOT_SLOT,
        .parent_world = transform_identity(),
        .parent_has_clip = false,
        .parent_clip = {0},
        .parent_opacity = 1.0f,
    };
    {
        stack = malloc(16 * sizeof(struct yscene_derive_frame));
        if (!stack) {
            return YETTY_ERR(yetty_ycore_void, "yscene derive: stack alloc");
        }
        stack_capacity = 16;
    }
    stack[stack_count++] = root_frame;

    struct yetty_ycore_void_result walk_result = YETTY_OK_VOID();
    while (stack_count > 0) {
        struct yscene_derive_frame frame = stack[--stack_count];
        const struct yetty_yscene_dom_node *node = &dom->nodes[frame.slot];

        struct yscene_transform local = {node->m00, node->m01, node->m10,
                                         node->m11, node->translate_x, node->translate_y};
        struct yscene_transform world = transform_compose(&frame.parent_world, &local);

        bool has_clip = frame.parent_has_clip;
        struct yetty_ycore_rectangle clip = frame.parent_clip;
        if (node->has_clip) {
            /* The node's clip rect is in its own local space. */
            struct yetty_ycore_rectangle world_clip = transform_map_rect(&world, node->clip);
            clip = has_clip ? rect_intersect(clip, world_clip) : world_clip;
            has_clip = true;
        }
        float opacity = frame.parent_opacity * node->opacity;

        walk_result = scene_derive_node_content(scene, dom, registry_res.value, frame.slot,
                                                &world, has_clip, clip, opacity);
        if (YETTY_IS_ERR(walk_result)) {
            break;
        }

        for (uint32_t i = 0; i < node->child_count; i++) {
            if (stack_count == stack_capacity) {
                uint32_t new_capacity = stack_capacity * 2;
                struct yscene_derive_frame *grown =
                    realloc(stack, (size_t)new_capacity * sizeof(struct yscene_derive_frame));
                if (!grown) {
                    walk_result = YETTY_ERR(yetty_ycore_void, "yscene derive: stack alloc");
                    break;
                }
                stack = grown;
                stack_capacity = new_capacity;
            }
            stack[stack_count++] = (struct yscene_derive_frame){
                .slot = node->children[i],
                .parent_world = world,
                .parent_has_clip = has_clip,
                .parent_clip = clip,
                .parent_opacity = opacity,
            };
        }
        if (YETTY_IS_ERR(walk_result)) {
            break;
        }
    }
    free(stack);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_result, "yscene derive: walk");

    qsort(scene->leaves_scratch, scene->leaf_scratch_count, sizeof(struct yscene_leaf),
          scene_leaf_compare);

    /* Full success — publish by swapping the pairs. */
    struct yscene_leaf *previous_leaves = scene->leaves;
    uint32_t previous_capacity = scene->leaf_capacity;
    scene->leaves = scene->leaves_scratch;
    scene->leaf_count = scene->leaf_scratch_count;
    scene->leaf_capacity = scene->leaf_scratch_capacity;
    scene->leaves_scratch = previous_leaves;
    scene->leaf_scratch_count = 0;
    scene->leaf_scratch_capacity = previous_capacity;

    scene->derived_generation = dom->committed_generation;
    scene->derived_valid = true;
    /* Dirt clearing and span reclaim happen ONLY on the commit path
     * (scene_commit_and_publish) — a derive that runs outside a commit
     * (tests, hit-test warm-up) must not consume pending dirtiness or
     * free spans a published snapshot may still reference. */
    return YETTY_OK_VOID();
}

/* The ONE publication path (P0 contract): promote pending mutations,
 * then synchronously derive the freshly committed generation so the
 * published leaf snapshot is fixed BEFORE any later pending mutation
 * can touch the tree. Every committing entry point (typed commit,
 * process_bytes, reset_content) goes through here. Retired spans stay
 * alive past this point only until the reclaim below — which is safe,
 * because this snapshot was derived from the post-retirement lists. */
static struct yetty_ycore_uint64_result scene_commit_and_publish(struct yetty_yscene_scene *scene)
{
    if (scene->pending_poisoned) {
        return YETTY_ERR(yetty_ycore_uint64,
                         "yscene commit: pending state poisoned by a failed envelope — "
                         "a full reset (CMD_ZERO / reset_content) is required");
    }
    struct yetty_yscene_dom_ptr_result dom_res = scene_dom(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, dom_res, "yscene commit: dom");
    struct yetty_yscene_dom *dom = dom_res.value;
    struct yetty_ycore_uint64_result commit_res = yetty_yscene_dom_commit(dom);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, commit_res, "yscene commit: promote");
    struct yetty_ycore_void_result derive_res = scene_derive(scene);
    if (YETTY_IS_ERR(derive_res)) {
        return YETTY_ERR(yetty_ycore_uint64, "yscene commit: publish derive", derive_res);
    }
    struct yetty_ycore_void_result clear_res = yetty_yscene_dom_clear_dirty(dom);
    if (YETTY_IS_ERR(clear_res)) {
        return YETTY_ERR(yetty_ycore_uint64, "yscene commit: clear dirty", clear_res);
    }
    struct yetty_ycore_void_result reclaim_res =
        yetty_yscene_dom_reclaim(dom, scene->derived_generation);
    if (YETTY_IS_ERR(reclaim_res)) {
        return YETTY_ERR(yetty_ycore_uint64, "yscene commit: reclaim", reclaim_res);
    }
    return commit_res;
}

/*===========================================================================
 * Legacy wire-envelope adapter — embedded CMD_GROUP / CMD_DELETE /
 * CMD_ZERO bodies → typed dom mutations.
 *
 * Leaf-run mapping: consecutive plain records form a RUN; the k-th run
 * of a REOPENED node replaces the node's k-th batch in place (the batch
 * keeps its paint_seq, so re-emitted content repaints at its original
 * depth); extra runs append, surplus batches are trimmed. A fresh node
 * (and the root scope) simply appends. Nested CMD_GROUP declares the
 * child (minting its seq between the surrounding runs' seqs on first
 * declaration — which is exactly the source interleaving order); child
 * nodes persist across re-emission and are removed only by CMD_DELETE.
 *
 * Known approximation, documented: structurally INSERTING a new child
 * between two pre-existing runs of a reopened node places the child
 * after both runs (their seqs predate the child's). The typed API
 * expresses exact placement; legacy producers (ygui) re-emit with a
 * stable run structure, where this cannot occur.
 *=========================================================================*/

struct yscene_body_walk {
    struct yetty_yscene_scene *scene;
    struct yetty_yscene_dom *dom;
    const struct yetty_ydraw_drawable_list_registry *registry;
};

static struct yetty_ycore_void_result scene_apply_body(struct yscene_body_walk *walk,
                                                       uint64_t target_id, bool reopen,
                                                       const uint8_t *bytes, size_t bytes_len);

/* Untrusted producer bodies nest CMD_GROUP via C recursion in both the
 * validator and the applier — bound the depth. */
#define YSCENE_ADAPTER_MAX_DEPTH 64

/* Pass 1 — structural validation, NO mutation. Rejects: malformed or
 * truncated records, CMD_UPDATE (unrouted — silent drop would be data
 * loss), CMD_GROUP_REF (unimplemented forward references), CMD_ZERO
 * below the top level (the applier restarts at the root; outer frames
 * would resume against deleted targets), and over-deep nesting. After
 * this pass the applier cannot fail on input shape — only on
 * allocation. */
static struct yetty_ycore_void_result scene_validate_body_chain(
    struct yscene_body_walk *walk, const uint8_t *bytes, size_t bytes_len, uint32_t depth,
    uint32_t *group_chain);

static struct yetty_ycore_void_result scene_validate_body(struct yscene_body_walk *walk,
                                                          const uint8_t *bytes, size_t bytes_len,
                                                          uint32_t depth)
{
    uint32_t group_chain[YSCENE_ADAPTER_MAX_DEPTH + 1];
    (void)depth;
    return scene_validate_body_chain(walk, bytes, bytes_len, 0, group_chain);
}

static struct yetty_ycore_void_result scene_validate_body_chain(
    struct yscene_body_walk *walk, const uint8_t *bytes, size_t bytes_len, uint32_t depth,
    uint32_t *group_chain)
{
    if (depth > YSCENE_ADAPTER_MAX_DEPTH) {
        return YETTY_ERR(yetty_ycore_void, "yscene adapter: CMD_GROUP nesting too deep");
    }
    size_t cursor = 0;
    while (cursor < bytes_len) {
        struct yetty_ydraw_command command;
        struct yetty_ycore_size_result parse_res = yetty_ydraw_drawable_command_parse(
            walk->registry, bytes + cursor, (uint32_t)(bytes_len - cursor), &command);
        if (YETTY_IS_ERR(parse_res)) {
            /* Locate the offender for the producer's trace. */
            yerror("yscene adapter: validate parse failed at depth=%u offset=%zu of %zu",
                   depth, cursor, bytes_len);
            return YETTY_ERR(yetty_ycore_void, "yscene adapter: validate parse", parse_res);
        }
        if (parse_res.value == 0) {
            return YETTY_ERR(yetty_ycore_void, "yscene adapter: validate no progress");
        }
        uint32_t record_type = 0;
        memcpy(&record_type, bytes + cursor, sizeof(record_type));
        if (command.kind == YETTY_YDRAW_COMMAND_UPDATE) {
            /* Routed at apply (complex-instance streaming); shape was
             * already checked by command_parse above. */
            cursor += parse_res.value;
            continue;
        }
        if (record_type == YETTY_YDRAW_CMD_GROUP_REF) {
            return YETTY_ERR(yetty_ycore_void, "yscene adapter: CMD_GROUP_REF unsupported");
        }
        if (record_type == YETTY_YDRAW_CMD_ZERO && depth > 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yscene adapter: CMD_ZERO inside a group body is not allowed");
        }
        if (record_type == YETTY_YDRAW_CMD_GROUP) {
            uint32_t group_id;
            uint32_t payload_size;
            memcpy(&group_id, bytes + cursor + 4, sizeof(group_id));
            memcpy(&payload_size, bytes + cursor + 8, sizeof(payload_size));
            /* Semantic guard (review H2): a group nested anywhere inside
             * its own scope would fail declare() mid-apply — reject it
             * BEFORE any mutation. */
            for (uint32_t i = 0; i < depth; i++) {
                if (group_chain[i] == group_id) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "yscene adapter: group nested inside itself");
                }
            }
            group_chain[depth] = group_id;
            struct yetty_ycore_void_result nested_res = scene_validate_body_chain(
                walk, bytes + cursor + 12, payload_size, depth + 1, group_chain);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, nested_res, "yscene adapter: validate group");
        }
        cursor += parse_res.value;
    }
    if (cursor != bytes_len) {
        return YETTY_ERR(yetty_ycore_void, "yscene adapter: body ends mid-record");
    }
    return YETTY_OK_VOID();
}

/* Find (or mint) the complex instance for the composite record at
 * (batch_slot, record_offset). Minting happens at INGEST so a
 * CMD_UPDATE later in the same body can already target it — ygrid's
 * semantics. Returns NULL quietly when no factory is wired (headless). */
static struct yetty_ycore_void_result scene_complex_mint(struct yetty_yscene_scene *scene,
                                                         uint32_t batch_slot,
                                                         uint32_t record_offset,
                                                         const uint32_t *record,
                                                         size_t record_len)
{
    if (!scene->composite_factory) {
        return YETTY_OK_VOID();
    }
    uint64_t batch_stamp = scene->dom->batches[batch_slot].batch_stamp;
    for (uint32_t i = 0; i < scene->complex_count; i++) {
        if (scene->complexes[i].batch_stamp == batch_stamp &&
            scene->complexes[i].record_offset == record_offset) {
            return YETTY_OK_VOID(); /* already minted for this span */
        }
    }
    if (scene->complex_count == scene->complex_capacity) {
        uint32_t new_capacity = scene->complex_capacity ? scene->complex_capacity * 2 : 8;
        struct yscene_complex_instance *grown = realloc(
            scene->complexes, (size_t)new_capacity * sizeof(struct yscene_complex_instance));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yscene complex: instance array alloc failed");
        }
        scene->complexes = grown;
        scene->complex_capacity = new_capacity;
    }
    struct yetty_ydraw_composite_ptr_result instance_res =
        yetty_ydraw_composite_factory_create_instance(scene->composite_factory, record,
                                                      record_len, /*rolling_row=*/0);
    if (YETTY_IS_ERR(instance_res)) {
        /* Per-record best-effort, like ygrid: an unknown/failed complex
         * drops that record, not the frame. */
        ydebug("yscene complex: create_instance failed for type=0x%08x: %s", record[0],
               instance_res.error.msg);
        yetty_ycore_error_destroy(instance_res.error);
        return YETTY_OK_VOID();
    }
    struct yscene_complex_instance *slot = &scene->complexes[scene->complex_count++];
    memset(slot, 0, sizeof(*slot));
    slot->instance = instance_res.value;
    slot->batch_stamp = batch_stamp;
    slot->batch_slot = batch_slot;
    slot->record_offset = record_offset;
    slot->stream_ordinal = ++scene->stream_next_ordinal;
    return YETTY_OK_VOID();
}

/* Mint complex instances for every composite record in `batch_slot`.
 * Called right after a span enters the dom (append/replace/set). */
static struct yetty_ycore_void_result scene_complex_scan_batch(struct yetty_yscene_scene *scene,
                                                               uint32_t batch_slot)
{
    const struct yetty_yscene_dom_batch *batch = &scene->dom->batches[batch_slot];
    for (uint32_t i = 0; i < batch->record_count; i++) {
        uint32_t record_offset = batch->record_offsets[i];
        uint32_t record_type;
        memcpy(&record_type, batch->bytes + record_offset, sizeof(record_type));
        if (!yetty_ydraw_is_composite(record_type)) {
            continue;
        }
        size_t record_len = (i + 1 < batch->record_count ? batch->record_offsets[i + 1]
                                                         : (uint32_t)batch->bytes_len) -
                            record_offset;
        struct yetty_ycore_void_result mint_res = scene_complex_mint(
            scene, batch_slot, record_offset,
            (const uint32_t *)(batch->bytes + record_offset), record_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mint_res, "yscene complex: mint");
    }
    return YETTY_OK_VOID();
}

/* Route one CMD_UPDATE to the live instance registered under its
 * per-body ordinal (most recent wins). Unroutable updates drop with
 * trace — stale ids are normal in streaming producers. Payload shape:
 * first u32 = target_field, rest = body (same contract as ygrid). */
static void scene_complex_route_update(struct yetty_yscene_scene *scene,
                                       const struct yetty_ydraw_command_update *update)
{
    if (update->size < sizeof(uint32_t)) {
        return;
    }
    struct yscene_complex_instance *target = NULL;
    for (uint32_t i = 0; i < scene->complex_count; i++) {
        if (scene->complexes[i].stream_ordinal == update->id) {
            target = &scene->complexes[i]; /* keep scanning: latest wins */
        }
    }
    if (!target || !target->instance || !target->instance->ops ||
        !target->instance->ops->update) {
        ydebug("yscene: CMD_UPDATE id=%u has no live updatable target", update->id);
        return;
    }
    uint32_t target_field;
    memcpy(&target_field, update->data, sizeof(target_field));
    struct yetty_ycore_void_result update_res =
        target->instance->ops->update(target->instance, target_field,
                                      update->data + sizeof(uint32_t),
                                      update->size - sizeof(uint32_t));
    if (YETTY_IS_ERR(update_res)) {
        ydebug("yscene: CMD_UPDATE id=%u failed: %s", update->id, update_res.error.msg);
        yetty_ycore_error_destroy(update_res.error);
        return;
    }
    target->instance->dirty = 1;
}

/* Destroy EVERY complex instance immediately — the reset/navigation
 * path (reset_content, typed zero, wire CMD_ZERO). Complexes live
 * outside the dom, so a dom zero alone leaves the previous page's
 * images alive and painting over the new page, accumulating per
 * navigation (the ygrid #685 class, reported live from the ychromium
 * bridge). */
static void scene_reset_complexes(struct yetty_yscene_scene *scene)
{
    if (scene->complex_count > 0) {
        ydebug("yscene: reset destroying %u complex instance(s)", scene->complex_count);
    }
    for (uint32_t i = 0; i < scene->complex_count; i++) {
        if (scene->complexes[i].instance) {
            yetty_ydraw_composite_destroy(scene->complexes[i].instance);
        }
    }
    scene->complex_count = 0;
}

/* Destroy instances whose source span is gone (sweep after staging
 * marked the live ones). */
static void scene_complex_sweep(struct yetty_yscene_scene *scene)
{
    uint32_t write = 0;
    for (uint32_t i = 0; i < scene->complex_count; i++) {
        if (!scene->complexes[i].seen) {
            if (scene->complexes[i].instance) {
                yetty_ydraw_composite_destroy(scene->complexes[i].instance);
            }
            continue;
        }
        scene->complexes[write++] = scene->complexes[i];
    }
    scene->complex_count = write;
}

/* Flush [run_start, run_end) as the next content run of `target_id`. */
static struct yetty_ycore_void_result scene_flush_run(struct yscene_body_walk *walk,
                                                      uint64_t target_id, bool reopen,
                                                      uint32_t *run_index, uint32_t reopen_batches,
                                                      const uint8_t *run_start, size_t run_len)
{
    if (run_len == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result apply_res;
    bool replaced = reopen && *run_index < reopen_batches;
    if (replaced) {
        apply_res = yetty_yscene_dom_node_replace_batch(walk->dom, target_id, *run_index,
                                                        run_start, run_len);
    } else {
        apply_res = yetty_yscene_dom_node_append_batch(walk->dom, target_id, run_start, run_len);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene adapter: content run");
    /* Mint complex instances for any composite record in the span that
     * just landed, so same-body CMD_UPDATEs can target them. */
    {
        struct yetty_ycore_uint32_result slot_res =
            yetty_yscene_dom_lookup(walk->dom, target_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene adapter: run target");
        const struct yetty_yscene_dom_node *node = &walk->dom->nodes[slot_res.value];
        uint32_t batch_slot =
            replaced ? node->batch_slots[*run_index] : node->batch_slots[node->batch_count - 1];
        struct yetty_ycore_void_result mint_res =
            scene_complex_scan_batch(walk->scene, batch_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mint_res, "yscene adapter: complex mint");
    }
    (*run_index)++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_apply_body(struct yscene_body_walk *walk,
                                                       uint64_t target_id, bool reopen,
                                                       const uint8_t *bytes, size_t bytes_len)
{
    /* Snapshot of how many batches a reopened node starts with — runs
     * replace these in place, index by index. */
    uint32_t reopen_batches = 0;
    if (reopen) {
        struct yetty_ycore_uint32_result slot_res =
            yetty_yscene_dom_lookup(walk->dom, target_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene adapter: reopen target");
        reopen_batches = walk->dom->nodes[slot_res.value].batch_count;
    }

    uint32_t run_index = 0;
    size_t run_start = 0;
    bool run_open = false;
    size_t cursor = 0;

    while (cursor < bytes_len) {
        struct yetty_ydraw_command command;
        struct yetty_ycore_size_result parse_res = yetty_ydraw_drawable_command_parse(
            walk->registry, bytes + cursor, (uint32_t)(bytes_len - cursor), &command);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, parse_res, "yscene adapter: parse");
        if (parse_res.value == 0) {
            return YETTY_ERR(yetty_ycore_void, "yscene adapter: parser made no progress");
        }
        uint32_t record_type = 0;
        memcpy(&record_type, bytes + cursor, sizeof(record_type));

        bool is_structural =
            command.kind == YETTY_YDRAW_COMMAND_DELETE ||
            command.kind == YETTY_YDRAW_COMMAND_UPDATE ||
            record_type == YETTY_YDRAW_CMD_GROUP || record_type == YETTY_YDRAW_CMD_ZERO;

        if (!is_structural) {
            if (!run_open) {
                run_start = cursor;
                run_open = true;
            }
            cursor += parse_res.value;
            continue;
        }

        /* Structural record — close the open run first. */
        if (run_open) {
            struct yetty_ycore_void_result flush_res =
                scene_flush_run(walk, target_id, reopen, &run_index, reopen_batches,
                                bytes + run_start, cursor - run_start);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yscene adapter: flush");
            run_open = false;
        }

        if (command.kind == YETTY_YDRAW_COMMAND_DELETE) {
            struct yetty_ycore_uint32_result known_res =
                yetty_yscene_dom_lookup(walk->dom, (uint64_t)command.id);
            if (YETTY_IS_OK(known_res)) {
                struct yetty_ycore_void_result delete_res =
                    yetty_yscene_dom_node_delete(walk->dom, (uint64_t)command.id);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, delete_res, "yscene adapter: delete");
            } else {
                yetty_ycore_error_destroy(known_res.error);
            }
        } else if (command.kind == YETTY_YDRAW_COMMAND_UPDATE) {
            scene_complex_route_update(walk->scene, &command.update);
        } else if (record_type == YETTY_YDRAW_CMD_ZERO) {
            struct yetty_ycore_void_result zero_res = yetty_yscene_dom_zero(walk->dom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, zero_res, "yscene adapter: zero");
            /* A full reset rebuilds from scratch: poison, wire fonts,
             * and complex instances from the previous document are
             * gone. */
            walk->scene->pending_poisoned = false;
            scene_reset_wire_fonts(walk->scene);
            scene_reset_complexes(walk->scene);
            scene_reset_view(walk->scene);
            /* Everything after CMD_ZERO lands in the fresh root scope —
             * mirror the ygrid contract. */
            cursor += parse_res.value;
            return scene_apply_body(walk, 0, false, bytes + cursor, bytes_len - cursor);
        } else {
            /* CMD_GROUP: [type][id][payload_size][payload]. */
            uint32_t group_id;
            uint32_t payload_size;
            memcpy(&group_id, bytes + cursor + 4, sizeof(group_id));
            memcpy(&payload_size, bytes + cursor + 8, sizeof(payload_size));
            uint64_t child_id = (uint64_t)group_id;
            struct yetty_ycore_uint32_result known_res =
                yetty_yscene_dom_lookup(walk->dom, child_id);
            bool child_reopen = YETTY_IS_OK(known_res);
            if (!child_reopen) {
                yetty_ycore_error_destroy(known_res.error);
            }
            struct yetty_ycore_void_result declare_res =
                yetty_yscene_dom_node_declare(walk->dom, child_id, target_id);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, declare_res, "yscene adapter: declare");
            struct yetty_ycore_void_result body_res = scene_apply_body(
                walk, child_id, child_reopen, bytes + cursor + 12, payload_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "yscene adapter: group body");
        }
        cursor += parse_res.value;
    }

    if (run_open) {
        struct yetty_ycore_void_result flush_res =
            scene_flush_run(walk, target_id, reopen, &run_index, reopen_batches,
                            bytes + run_start, cursor - run_start);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yscene adapter: flush");
    }

    /* A reopened node that emitted fewer runs than it had batches drops
     * the surplus (the re-emission IS the node's new content). */
    if (reopen) {
        struct yetty_ycore_uint32_result slot_res =
            yetty_yscene_dom_lookup(walk->dom, target_id);
        if (YETTY_IS_OK(slot_res)) {
            struct yetty_yscene_dom_node *node = &walk->dom->nodes[slot_res.value];
            while (node->batch_count > run_index) {
                struct yetty_ycore_void_result remove_res = yetty_yscene_dom_node_remove_batch(
                    walk->dom, target_id, node->batch_count - 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, remove_res, "yscene adapter: trim");
            }
        } else {
            yetty_ycore_error_destroy(slot_res.error);
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public mutation methods — the module's client-facing API. Each
 * forwards to the owned dom and marks the figure dirty so the host
 * repaints.
 *=========================================================================*/

/* Resolve scene + dom for a method body; shared preamble. Ensures the
 * ONE registry binding exists before any content can land — otherwise
 * the dom would lazily mint its own default and the scene another
 * (review H1), and a later bind would be rejected. */
static struct yetty_yscene_scene_ptr_result scene_for_method(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, scene_res, "yscene method: object");
    struct yetty_yscene_dom_ptr_result dom_res = scene_dom(scene_res.value);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, dom_res, "yscene method: dom");
    struct yetty_ydraw_drawable_list_registry_ptr_result registry_res =
        scene_registry(scene_res.value);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, registry_res, "yscene method: registry");
    return scene_res;
}

YETTY_ANNOTATE("virtual@yscene:scene:constructor")
static struct yetty_ycore_void_result scene_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene constructor: object");
    struct yetty_yscene_scene *scene = scene_res.value;
    scene->view_scale = 1.0f;
    for (size_t i = 0; i < sizeof(scene->wire_font_slot) / sizeof(scene->wire_font_slot[0]);
         i++) {
        scene->wire_font_slot[i] = -1;
    }
    /* Slot 0 is reserved for the host default font. */
    scene->next_font_slot = 1;
    /* The 1:1 dom exists from construction (no lazy window in which the
     * scene and dom could bind different registries). Registry binding
     * happens through set_registry BEFORE any content lands; the
     * headless fallback is the dom's own lazy default. */
    struct yetty_yscene_dom_ptr_result dom_res = yetty_yscene_dom_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dom_res, "yscene constructor: dom");
    scene->dom = dom_res.value;
    return YETTY_OK_VOID();
}

/* Bind the shared wire-record registry (borrowed — the framework's
 * instance) to the scene AND its dom, as one binding. Rejected once the
 * dom holds content — record strides must not change under existing
 * spans. */
YETTY_ANNOTATE("virtual@yscene:scene:set_registry")
YETTY_ANNOTATE("local@yscene:set_registry")
static struct yetty_ycore_void_result scene_set_registry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list_registry *registry)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene set_registry: object");
    struct yetty_yscene_scene *scene = scene_res.value;
    return scene_bind_registry(scene, registry, /*owns=*/false);
}

YETTY_ANNOTATE("virtual@yscene:scene:node_declare")
static struct yetty_ycore_void_result scene_node_declare(struct yetty_yclass_object *obj,
                                                         uint64_t external_id,
                                                         uint64_t parent_external_id)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene node_declare");
    struct yetty_ycore_void_result apply_res =
        yetty_yscene_dom_node_declare(scene_res.value->dom, external_id, parent_external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene node_declare");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

/* Granular placement setters: read-modify-write through the atomic
 * placement function, so each is one consistent update. */

static struct yetty_ycore_void_result scene_patch_placement(
    struct yetty_yclass_object *obj, uint64_t external_id,
    void (*patch)(struct yetty_yscene_dom_placement *placement, const void *argument),
    const void *argument)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene placement");
    struct yetty_yscene_dom *dom = scene_res.value->dom;
    struct yetty_ycore_uint32_result slot_res = yetty_yscene_dom_lookup(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene placement: node");
    const struct yetty_yscene_dom_node *node = &dom->nodes[slot_res.value];
    struct yetty_yscene_dom_placement placement = {
        .paint_z = node->paint_z,
        .m00 = node->m00,
        .m01 = node->m01,
        .m10 = node->m10,
        .m11 = node->m11,
        .translate_x = node->translate_x,
        .translate_y = node->translate_y,
        .has_clip = node->has_clip,
        .clip = node->clip,
        .opacity = node->opacity,
    };
    patch(&placement, argument);
    struct yetty_ycore_void_result apply_res =
        yetty_yscene_dom_node_set_placement(dom, external_id, &placement);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene placement: apply");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

struct scene_transform_argument {
    float m00, m01, m10, m11, translate_x, translate_y;
};

static void scene_patch_transform(struct yetty_yscene_dom_placement *placement,
                                  const void *argument)
{
    const struct scene_transform_argument *transform = argument;
    placement->m00 = transform->m00;
    placement->m01 = transform->m01;
    placement->m10 = transform->m10;
    placement->m11 = transform->m11;
    placement->translate_x = transform->translate_x;
    placement->translate_y = transform->translate_y;
}

YETTY_ANNOTATE("virtual@yscene:scene:node_set_transform")
static struct yetty_ycore_void_result scene_node_set_transform(struct yetty_yclass_object *obj,
                                                               uint64_t external_id, float m00,
                                                               float m01, float m10, float m11,
                                                               float translate_x,
                                                               float translate_y)
{
    struct scene_transform_argument transform = {m00, m01, m10, m11, translate_x, translate_y};
    return scene_patch_placement(obj, external_id, scene_patch_transform, &transform);
}

static void scene_patch_clip(struct yetty_yscene_dom_placement *placement, const void *argument)
{
    const struct yetty_ycore_rectangle *clip = argument;
    placement->has_clip = true;
    placement->clip = *clip;
}

YETTY_ANNOTATE("virtual@yscene:scene:node_set_clip")
static struct yetty_ycore_void_result scene_node_set_clip(struct yetty_yclass_object *obj,
                                                          uint64_t external_id, float min_x,
                                                          float min_y, float max_x, float max_y)
{
    struct yetty_ycore_rectangle clip = {.min = {.x = min_x, .y = min_y},
                                         .max = {.x = max_x, .y = max_y}};
    return scene_patch_placement(obj, external_id, scene_patch_clip, &clip);
}

static void scene_patch_clear_clip(struct yetty_yscene_dom_placement *placement,
                                   const void *argument)
{
    (void)argument;
    placement->has_clip = false;
}

YETTY_ANNOTATE("virtual@yscene:scene:node_clear_clip")
static struct yetty_ycore_void_result scene_node_clear_clip(struct yetty_yclass_object *obj,
                                                            uint64_t external_id)
{
    return scene_patch_placement(obj, external_id, scene_patch_clear_clip, NULL);
}

static void scene_patch_opacity(struct yetty_yscene_dom_placement *placement,
                                const void *argument)
{
    placement->opacity = *(const float *)argument;
}

YETTY_ANNOTATE("virtual@yscene:scene:node_set_opacity")
static struct yetty_ycore_void_result scene_node_set_opacity(struct yetty_yclass_object *obj,
                                                             uint64_t external_id, float opacity)
{
    return scene_patch_placement(obj, external_id, scene_patch_opacity, &opacity);
}

static void scene_patch_z(struct yetty_yscene_dom_placement *placement, const void *argument)
{
    placement->paint_z = *(const int32_t *)argument;
}

YETTY_ANNOTATE("virtual@yscene:scene:node_set_z")
static struct yetty_ycore_void_result scene_node_set_z(struct yetty_yclass_object *obj,
                                                       uint64_t external_id, int32_t paint_z)
{
    return scene_patch_placement(obj, external_id, scene_patch_z, &paint_z);
}

YETTY_ANNOTATE("virtual@yscene:scene:node_set_content")
static struct yetty_ycore_void_result scene_node_set_content(struct yetty_yclass_object *obj,
                                                             uint64_t external_id,
                                                             struct yetty_ycore_buffer content)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene node_set_content");
    struct yetty_ycore_void_result apply_res = yetty_yscene_dom_node_set_content(
        scene_res.value->dom, external_id, content.data, content.size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene node_set_content");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("virtual@yscene:scene:node_append_batch")
static struct yetty_ycore_void_result scene_node_append_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              struct yetty_ycore_buffer content)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene node_append_batch");
    struct yetty_ycore_void_result apply_res = yetty_yscene_dom_node_append_batch(
        scene_res.value->dom, external_id, content.data, content.size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene node_append_batch");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("virtual@yscene:scene:node_replace_batch")
static struct yetty_ycore_void_result scene_node_replace_batch(struct yetty_yclass_object *obj,
                                                               uint64_t external_id,
                                                               uint32_t batch_index,
                                                               struct yetty_ycore_buffer content)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene node_replace_batch");
    struct yetty_ycore_void_result apply_res = yetty_yscene_dom_node_replace_batch(
        scene_res.value->dom, external_id, batch_index, content.data, content.size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene node_replace_batch");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("virtual@yscene:scene:node_remove_batch")
static struct yetty_ycore_void_result scene_node_remove_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              uint32_t batch_index)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene node_remove_batch");
    struct yetty_ycore_void_result apply_res =
        yetty_yscene_dom_node_remove_batch(scene_res.value->dom, external_id, batch_index);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene node_remove_batch");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("virtual@yscene:scene:node_delete")
static struct yetty_ycore_void_result scene_node_delete(struct yetty_yclass_object *obj,
                                                        uint64_t external_id)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene node_delete");
    struct yetty_ycore_void_result apply_res =
        yetty_yscene_dom_node_delete(scene_res.value->dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene node_delete");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("virtual@yscene:scene:zero")
static struct yetty_ycore_void_result scene_zero(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene zero");
    struct yetty_ycore_void_result apply_res = yetty_yscene_dom_zero(scene_res.value->dom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "yscene zero");
    scene_res.value->pending_poisoned = false;
    scene_reset_wire_fonts(scene_res.value);
    scene_reset_complexes(scene_res.value);
    scene_reset_view(scene_res.value);
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("virtual@yscene:scene:commit")
static struct yetty_ycore_uint64_result scene_commit(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, scene_res, "yscene commit");
    struct yetty_ycore_uint64_result commit_res = scene_commit_and_publish(scene_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, commit_res, "yscene commit");
    struct yetty_ycore_void_result dirty_res = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dirty_res)) {
        return YETTY_ERR(yetty_ycore_uint64, "yscene commit: dirty", dirty_res);
    }
    return commit_res;
}

/*===========================================================================
 * GPU — shader/lib loading, binder, staging, draw. The staging layout
 * and uniform block are ydraw-layer's contract (see ygrid.wgsl):
 *
 *   grid buffer : [per-cell offset table][sentinel count=0]
 *                 [per-cell blocks: count, prim_index...]
 *   prim buffer : [offset table: word offset from table end]
 *                 [per prim: rolling_row=0, record words (type first)]
 *
 * Cell blocks list prims in ascending staged index — and staged order
 * is the sorted leaf order, so in-bucket order is paint order by
 * construction.
 *=========================================================================*/

/*===========================================================================
 * Wire fonts — FONT_RESOURCE install (MSDF cache) + the shader-side
 * dispatcher rebuild. Ports ygrid's proven pipeline; the yscene twist is
 * that expansion output (glyphs) is DERIVED state, never master bytes.
 *=========================================================================*/

static uint64_t scene_fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static uint32_t scene_decode_utf8(const uint8_t **ptr, const uint8_t *end)
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

/* Install `font` (borrowed) into `slot`; bumps the generation so render
 * regenerates the dispatcher + children before the next draw. */
static struct yetty_ycore_void_result scene_set_font(struct yetty_yscene_scene *scene,
                                                     uint32_t slot,
                                                     struct yetty_yfont_font *font)
{
    if (slot >= sizeof(scene->fonts) / sizeof(scene->fonts[0])) {
        return YETTY_ERR(yetty_ycore_void, "yscene set_font: slot out of range");
    }
    scene->fonts[slot] = font;
    if (slot >= scene->font_count) {
        scene->font_count = slot + 1;
    }
    scene->font_generation++;
    return YETTY_OK_VOID();
}

/* Drop every wire-installed font: release cache references, clear the
 * producer-id → slot map, keep only the host default in slot 0. Called
 * on reset/navigation so page B's font_id 3 cannot silently reuse page
 * A's font (review H3). */
static void scene_reset_wire_fonts(struct yetty_yscene_scene *scene)
{
    bool changed = false;
    for (uint32_t slot = 1; slot < scene->font_count; slot++) {
        if (scene->fonts[slot]) {
            if (scene->font_cache && scene->font_handles[slot]) {
                yetty_yfont_cache_release_font(scene->font_cache, scene->font_handles[slot]);
            }
            scene->fonts[slot] = NULL;
            scene->font_handles[slot] = 0;
            changed = true;
        }
    }
    scene->font_count = scene->fonts[0] ? 1 : 0;
    scene->next_font_slot = 1;
    for (size_t i = 0; i < sizeof(scene->wire_font_slot) / sizeof(scene->wire_font_slot[0]);
         i++) {
        scene->wire_font_slot[i] = -1;
    }
    if (changed) {
        scene->font_generation++;
    }
}

/* FONT_RESOURCE record → MSDF slot. Reference shapes (same contract as
 * the ygrid/terminal receivers): a non-hash name = pre-installed font
 * under <data>/msdf-fonts/; a 16-hex name = hash-ref to a previously
 * cached TTF; embedded TTF bytes = generate (and cache) the MSDF CDB
 * keyed by content hash. Best-effort: a missing asset drops the font,
 * never the figure. Idempotent per producer font_id. */
static struct yetty_ycore_void_result scene_install_wire_font(
    struct yetty_yscene_scene *scene, const struct yetty_ydraw_font_resource_view *view)
{
    size_t slot_map_cap = sizeof(scene->wire_font_slot) / sizeof(scene->wire_font_slot[0]);
    if (view->font_id < 0 || (size_t)view->font_id >= slot_map_cap) {
        return YETTY_OK_VOID();
    }
    if (!scene->font_cache) {
        return YETTY_OK_VOID(); /* headless — nothing to render into */
    }
    if (scene->wire_font_slot[view->font_id] >= 0) {
        return YETTY_OK_VOID(); /* already installed */
    }
    if (!scene->cache_dir[0]) {
        return YETTY_ERR(yetty_ycore_void, "yscene wire font: no cache dir");
    }

    char cache_key[128];
    char cdb_path[1024];
    if (view->ttf_len == 0 && view->name_len != 16) {
        if (view->name_len == 0 || view->name_len >= sizeof(cache_key)) {
            return YETTY_OK_VOID();
        }
        memcpy(cache_key, view->name, view->name_len);
        cache_key[view->name_len] = '\0';
        if (!scene->data_dir[0]) {
            return YETTY_OK_VOID();
        }
        snprintf(cdb_path, sizeof(cdb_path), "%s/msdf-fonts/%s.cdb", scene->data_dir, cache_key);
        if (!yetty_yplatform_file_exists(cdb_path)) {
            ydebug("yscene wire font: named font '%s' not installed — dropped", cache_key);
            return YETTY_OK_VOID();
        }
    } else if (view->ttf_len == 0) {
        memcpy(cache_key, view->name, 16);
        cache_key[16] = '\0';
        snprintf(cdb_path, sizeof(cdb_path), "%s/ydraw-fonts/pdf_%s.cdb", scene->cache_dir,
                 cache_key);
    } else {
        snprintf(cache_key, sizeof(cache_key), "%016llx",
                 (unsigned long long)scene_fnv1a64(view->ttf, view->ttf_len));
        char fonts_dir[768];
        char ttf_path[1024];
        snprintf(fonts_dir, sizeof(fonts_dir), "%s/ydraw-fonts", scene->cache_dir);
        snprintf(ttf_path, sizeof(ttf_path), "%s/pdf_%s.ttf", fonts_dir, cache_key);
        snprintf(cdb_path, sizeof(cdb_path), "%s/pdf_%s.cdb", fonts_dir, cache_key);
        if (!yetty_yplatform_file_exists(cdb_path)) {
            if (!scene->msdf_generator) {
                return YETTY_ERR(yetty_ycore_void, "yscene wire font: no MSDF generator");
            }
            yetty_yplatform_mkdir_p(fonts_dir);
            if (!yetty_yplatform_file_exists(ttf_path)) {
                FILE *out = fopen(ttf_path, "wb");
                if (!out) {
                    return YETTY_ERR(yetty_ycore_void, "yscene wire font: open ttf cache");
                }
                size_t written = fwrite(view->ttf, 1, view->ttf_len, out);
                if (fclose(out) != 0 || written != view->ttf_len) {
                    return YETTY_ERR(yetty_ycore_void, "yscene wire font: write ttf cache");
                }
            }
            struct yetty_ymsdf_generator_config generator_config = {
                .ttf_path = ttf_path,
                .cdb_path = cdb_path,
                .font_size = 32.0f,
                .pixel_range = 4.0f,
            };
            struct yetty_ycore_void_result generate_res =
                scene->msdf_generator->ops->generate(scene->msdf_generator, &generator_config);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, generate_res, "yscene wire font: msdf");
        }
    }

    struct yetty_yfont_cache_ref_result ref_res =
        yetty_yfont_cache_get_font(scene->font_cache, cache_key, cdb_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ref_res, "yscene wire font: cache get_font");
    uint32_t slot = scene->next_font_slot;
    if (slot >= sizeof(scene->fonts) / sizeof(scene->fonts[0])) {
        yetty_yfont_cache_release_font(scene->font_cache, ref_res.value.handle);
        return YETTY_ERR(yetty_ycore_void, "yscene wire font: out of font slots");
    }
    struct yetty_ycore_void_result set_res = scene_set_font(scene, slot, ref_res.value.font);
    if (YETTY_IS_ERR(set_res)) {
        yetty_yfont_cache_release_font(scene->font_cache, ref_res.value.handle);
        return YETTY_ERR(yetty_ycore_void, "yscene wire font: set_font", set_res);
    }
    scene->font_handles[slot] = ref_res.value.handle;
    scene->wire_font_slot[view->font_id] = (int32_t)slot;
    scene->next_font_slot = slot + 1;
    ydebug("yscene: installed wire font_id=%d -> slot=%u key=%s", view->font_id, slot, cache_key);
    return YETTY_OK_VOID();
}

/* Regenerate the combined shader (font dispatcher + layer code) and
 * rs.children for the current font set. Shader-hash change makes the
 * binder refinalize on the next update. */
static struct yetty_ycore_void_result scene_rebuild_font_dispatcher(
    struct yetty_yscene_scene *scene)
{
    const struct yetty_yrender_gpu_resource_set *font_rs[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    const char *slot_namespaces[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t slot = 0; slot < scene->font_count; slot++) {
        if (!scene->fonts[slot]) {
            continue;
        }
        struct yetty_yrender_gpu_resource_set_result rs_res =
            scene->fonts[slot]->ops->get_gpu_resource_set(scene->fonts[slot]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "yscene dispatcher: font rs");
        font_rs[slot] = rs_res.value;
        slot_namespaces[slot] = rs_res.value->namespace;
    }

    char *dispatcher_wgsl = NULL;
    size_t dispatcher_size = 0;
    struct yetty_ycore_void_result dispatcher_res = yetty_yrender_build_font_dispatcher_wgsl(
        slot_namespaces, scene->font_count, &dispatcher_wgsl, &dispatcher_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_res, "yscene: font dispatcher");

    size_t combined_size = dispatcher_size + scene->layer_shader_code.size;
    char *combined_buffer = malloc(combined_size + 1);
    if (!combined_buffer) {
        free(dispatcher_wgsl);
        return YETTY_ERR(yetty_ycore_void, "yscene: combined shader alloc failed");
    }
    memcpy(combined_buffer, dispatcher_wgsl, dispatcher_size);
    memcpy(combined_buffer + dispatcher_size, scene->layer_shader_code.data,
           scene->layer_shader_code.size);
    combined_buffer[combined_size] = '\0';
    free(dispatcher_wgsl);
    free(scene->combined_shader);
    scene->combined_shader = combined_buffer;
    scene->combined_shader_size = combined_size;
    yetty_yrender_shader_code_set(&scene->rs.shader, scene->combined_shader,
                                  scene->combined_shader_size);

    size_t children_used = 0;
    scene->rs.children[children_used++] = &scene->sdf_lib_rs;
    scene->rs.children[children_used++] = &scene->effects_lib_rs;
    for (uint32_t slot = 0; slot < scene->font_count; slot++) {
        if (!font_rs[slot] || children_used >= YETTY_YRENDER_RS_MAX_CHILDREN) {
            continue;
        }
        scene->rs.children[children_used++] =
            (struct yetty_yrender_gpu_resource_set *)font_rs[slot];
    }
    scene->rs.children_count = children_used;
    scene->last_emitted_font_generation = scene->font_generation;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_ensure_words(uint32_t **words, size_t *capacity,
                                                         size_t needed)
{
    if (needed <= *capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_capacity = *capacity ? *capacity : 256;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    uint32_t *grown = realloc(*words, new_capacity * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yscene: staging alloc failed");
    }
    *words = grown;
    *capacity = new_capacity;
    return YETTY_OK_VOID();
}

static void scene_init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
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
    rs->uniforms[U_TIME] = (struct yetty_yrender_uniform){"ydraw_time", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_POST_FX_INDEX] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_index", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_POST_FX_P0] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_p0", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_POST_FX_P1] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_p1", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_POST_FX_P2] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_p2", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_POST_FX_P3] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_p3", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_POST_FX_P4] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_p4", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_POST_FX_P5] =
        (struct yetty_yrender_uniform){"ydraw_post_fx_p5", YETTY_YRENDER_UNIFORM_F32};

    rs->uniforms[U_ROLLING_ROW_0].u32 = 0;
    rs->uniforms[U_VZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_CZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_TIME].f32 = 0.0f;
    rs->uniforms[U_POST_FX_INDEX].u32 = 0;
}

static struct yetty_ycore_void_result scene_load_shader_libs(struct yetty_yscene_scene *scene,
                                                             const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];

    snprintf(path, sizeof(path), "%s/ysdf.gen.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result sdf_res = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sdf_res, "yscene: read ysdf.gen.wgsl");
    scene->sdf_lib_code = sdf_res.value;
    strncpy(scene->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&scene->sdf_lib_rs.shader,
                                  (const char *)scene->sdf_lib_code.data,
                                  scene->sdf_lib_code.size);

    /* Effects lib: attach a no-op fallback when the asset is missing so
     * the layer shader's fx_post_apply() always resolves. */
    snprintf(path, sizeof(path), "%s/effects-lib.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result effects_res = yetty_ycore_read_file(path);
    strncpy(scene->effects_lib_rs.namespace, "fx_lib", YETTY_YRENDER_NAME_MAX - 1);
    if (YETTY_IS_ERR(effects_res)) {
        static const char fx_stub_wgsl[] =
            "fn fx_post_apply(index: u32, color: vec3<f32>, pixel: vec2<f32>, "
            "screen: vec2<f32>, time: f32, mouse: vec2<f32>, cursor: vec2<f32>, "
            "cell: vec2<f32>, p0: f32, p1: f32, p2: f32, p3: f32, "
            "p4: f32, p5: f32) -> vec3<f32> { return color; }\n";
        ywarn("yscene: effects-lib.wgsl not loaded (%s) — post effects disabled",
              effects_res.error.msg ? effects_res.error.msg : "read failed");
        yetty_ycore_error_destroy(effects_res.error);
        yetty_yrender_shader_code_set(&scene->effects_lib_rs.shader, fx_stub_wgsl,
                                      sizeof(fx_stub_wgsl) - 1);
    } else {
        scene->effects_lib_code = effects_res.value;
        yetty_yrender_shader_code_set(&scene->effects_lib_rs.shader,
                                      (const char *)scene->effects_lib_code.data,
                                      scene->effects_lib_code.size);
    }

    snprintf(path, sizeof(path), "%s/ygrid.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result layer_res = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layer_res, "yscene: read ygrid.wgsl");
    scene->layer_shader_code = layer_res.value;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_build_binder(struct yetty_yscene_scene *scene)
{
    strncpy(scene->rs.namespace, "ydraw", YETTY_YRENDER_NAME_MAX - 1);
    scene->rs.buffer_count = 2;
    strncpy(scene->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(scene->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    scene->rs.buffers[0].readonly = 1;
    strncpy(scene->rs.buffers[1].name, "prims", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(scene->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    scene->rs.buffers[1].readonly = 1;
    scene_init_uniforms(&scene->rs);
    scene->rs.instance_count = 1;

    /* Initial (no-font) dispatcher; wire-font installs regenerate it. */
    struct yetty_ycore_void_result dispatcher_res = scene_rebuild_font_dispatcher(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_res, "yscene: initial dispatcher");

    struct yetty_yrender_gpu_resource_binder_result binder_res =
        yetty_yrender_gpu_resource_binder_create(scene->device, scene->queue,
                                                 scene->target_format, scene->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, binder_res, "yscene: binder create");
    scene->binder = binder_res.value;
    return YETTY_OK_VOID();
}

/* Content extent in px — what the cell grid spans and the shader
 * bounds-checks against. Defaults to the figure rect. */
static void scene_content_extent(const struct yetty_yscene_scene *scene,
                                 struct yetty_ycore_rectangle rect, float *out_w, float *out_h)
{
    float width = rect.max.x - rect.min.x;
    float height = rect.max.y - rect.min.y;
    if (scene->content_w > width) {
        width = scene->content_w;
    }
    if (scene->content_h > height) {
        height = scene->content_h;
    }
    *out_w = width > 1.0f ? width : 1.0f;
    *out_h = height > 1.0f ? height : 1.0f;
}

static void scene_cells_clear(struct yetty_yscene_scene *scene)
{
    for (uint32_t i = 0; i < YSCENE_GRID_COLS * YSCENE_GRID_ROWS; i++) {
        scene->cells[i].count = 0;
    }
}

static struct yetty_ycore_void_result scene_cell_push(struct yscene_cell *cell,
                                                      uint32_t staged_index)
{
    if (cell->count == cell->capacity) {
        uint32_t new_capacity = cell->capacity ? cell->capacity * 2 : 8;
        uint32_t *grown = realloc(cell->indices, (size_t)new_capacity * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yscene: cell bucket alloc failed");
        }
        cell->indices = grown;
        cell->capacity = new_capacity;
    }
    cell->indices[cell->count++] = staged_index;
    return YETTY_OK_VOID();
}

/* Append one staged record (header word + payload words) to the scratch
 * stream and register its meta (offset + world AABB for bucketing). */
static struct yetty_ycore_void_result scene_stage_record(struct yetty_yscene_scene *scene,
                                                         const uint32_t *record_words,
                                                         uint32_t word_count,
                                                         struct yetty_ycore_rectangle world_aabb)
{
    size_t needed = scene->staging_scratch_len + 1u + word_count;
    if (needed > scene->staging_scratch_cap) {
        size_t new_capacity = scene->staging_scratch_cap ? scene->staging_scratch_cap : 512;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }
        uint32_t *grown = realloc(scene->staging_scratch, new_capacity * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yscene staging: scratch alloc failed");
        }
        scene->staging_scratch = grown;
        scene->staging_scratch_cap = new_capacity;
    }
    if (scene->staged_record_count == scene->staged_record_cap) {
        uint32_t new_capacity = scene->staged_record_cap ? scene->staged_record_cap * 2 : 128;
        struct yscene_staged_record *grown = realloc(
            scene->staged_records, (size_t)new_capacity * sizeof(struct yscene_staged_record));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yscene staging: meta alloc failed");
        }
        scene->staged_records = grown;
        scene->staged_record_cap = new_capacity;
    }
    struct yscene_staged_record *meta = &scene->staged_records[scene->staged_record_count++];
    meta->word_offset = (uint32_t)scene->staging_scratch_len;
    meta->world_aabb = world_aabb;
    scene->staging_scratch[scene->staging_scratch_len++] = 0; /* rolling_row */
    memcpy(&scene->staging_scratch[scene->staging_scratch_len], record_words,
           (size_t)word_count * sizeof(uint32_t));
    scene->staging_scratch_len += word_count;
    return YETTY_OK_VOID();
}

/* Multiply a packed 0xAARRGGBB color's alpha by `opacity`. */
static uint32_t scene_fold_opacity(uint32_t packed, float opacity)
{
    if (opacity >= 1.0f) {
        return packed;
    }
    uint32_t alpha = packed >> 24;
    alpha = (uint32_t)((float)alpha * opacity + 0.5f);
    return (packed & 0x00FFFFFFu) | (alpha << 24);
}

/* Expand one TEXT_DRAWABLE_LIST leaf into world-space GLYPH staging
 * records (7 words each). The span's pen positions are node-local: each
 * glyph anchor maps through the leaf's world transform; the font size
 * scales by m00 (uniform-scale assumption — anisotropic text scale
 * waits for the shader fork). Missing fonts drop the span, never the
 * frame. */
static struct yetty_ycore_void_result scene_stage_text_leaf(struct yetty_yscene_scene *scene,
                                                            const struct yscene_leaf *leaf,
                                                            const uint8_t *record_bytes)
{
    struct yetty_ydraw_text_drawable_list_view span;
    if (yetty_ydraw_text_drawable_list_parse((const uint32_t *)record_bytes, &span) != 0) {
        return YETTY_ERR(yetty_ycore_void, "yscene staging: TEXT parse failed");
    }
    size_t slot_map_cap = sizeof(scene->wire_font_slot) / sizeof(scene->wire_font_slot[0]);
    uint32_t slot;
    if (span.font_id >= 0 && (size_t)span.font_id < slot_map_cap &&
        scene->wire_font_slot[span.font_id] >= 0) {
        slot = (uint32_t)scene->wire_font_slot[span.font_id];
    } else {
        slot = (span.font_id < 0) ? 0u : (uint32_t)span.font_id;
    }
    if (slot >= scene->font_count || !scene->fonts[slot]) {
        ydebug("yscene staging: TEXT font_id=%d -> slot %u has no font; span dropped",
               span.font_id, slot);
        return YETTY_OK_VOID();
    }
    struct yetty_yfont_font *font = scene->fonts[slot];
    float base_size = font->ops->get_base_size(font);
    /* World scale for the glyph size/advance (uniform-scale assumption). */
    float world_scale = leaf->m00 > 0.0f ? leaf->m00 : 1.0f;
    float world_font_size = span.font_size * world_scale;
    float glyph_scale = (base_size > 0.0f) ? span.font_size / base_size : 1.0f;
    uint32_t color = scene_fold_opacity(span.color, leaf->world_opacity);
    float cursor_x = span.x;

    const uint8_t *text_cursor = (const uint8_t *)span.text;
    const uint8_t *text_end = text_cursor + span.text_len;
    while (text_cursor < text_end) {
        uint32_t codepoint = scene_decode_utf8(&text_cursor, text_end);
        if (codepoint == 0) {
            break;
        }
        struct uint32_result glyph_idx_res = font->ops->get_glyph_index(font, codepoint);
        if (YETTY_IS_ERR(glyph_idx_res)) {
            yetty_ycore_error_destroy(glyph_idx_res.error);
            cursor_x += (span.font_size * 0.25f) + span.char_spacing;
            if (codepoint == 0x20) {
                cursor_x += span.word_spacing;
            }
            continue;
        }
        uint32_t glyph_index = glyph_idx_res.value;
        /* Re-fetch metadata AFTER get_glyph_index — lazy slot allocation
         * may have grown it. */
        struct yetty_yrender_gpu_resource_set_result rs_res =
            font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "yscene staging: font rs");
        const float *meta = (const float *)rs_res.value->buffers[0].data;
        uint32_t meta_count = (uint32_t)(rs_res.value->buffers[0].size / (6u * sizeof(float)));
        if (!meta || glyph_index >= meta_count) {
            return YETTY_ERR(yetty_ycore_void, "yscene staging: glyph metadata out of range");
        }
        const float *glyph_meta = meta + glyph_index * 6u;
        float size_x = glyph_meta[0];
        float size_y = glyph_meta[1];
        float bearing_x = glyph_meta[2];
        float bearing_y = glyph_meta[3];
        float advance = glyph_meta[4];
        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * glyph_scale + span.char_spacing;
            if (codepoint == 0x20) {
                cursor_x += span.word_spacing;
            }
            continue;
        }

        /* Node-local pen position → world. */
        float local_x = cursor_x + bearing_x * glyph_scale;
        float local_y = span.y - bearing_y * glyph_scale;
        float world_x = leaf->m00 * local_x + leaf->m01 * local_y + leaf->translate_x;
        float world_y = leaf->m10 * local_x + leaf->m11 * local_y + leaf->translate_y;

        uint32_t glyph_record[YSCENE_GLYPH_WORDS];
        glyph_record[0] = YSCENE_GLYPH_TYPE;
        glyph_record[1] = span.layer;
        memcpy(&glyph_record[2], &world_x, sizeof(float));
        memcpy(&glyph_record[3], &world_y, sizeof(float));
        memcpy(&glyph_record[4], &world_font_size, sizeof(float));
        glyph_record[5] = (glyph_index & 0xFFFFu) | ((slot + 1u) << 16);
        glyph_record[6] = color;

        float quad_scale = (base_size > 0.0f) ? world_font_size / base_size : 1.0f;
        struct yetty_ycore_rectangle glyph_aabb = {
            .min = {.x = world_x, .y = world_y},
            .max = {.x = world_x + size_x * quad_scale, .y = world_y + size_y * quad_scale},
        };
        struct yetty_ycore_void_result stage_res =
            scene_stage_record(scene, glyph_record, YSCENE_GLYPH_WORDS, glyph_aabb);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stage_res, "yscene staging: glyph");

        cursor_x += advance * glyph_scale + span.char_spacing;
        if (codepoint == 0x20) {
            cursor_x += span.word_spacing;
        }
    }
    return YETTY_OK_VOID();
}

/* Rebuild both staging buffers from the sorted leaf array. Staged
 * records carry WORLD-space geometry; scratch-then-assemble because
 * TEXT expansion makes the record count unknown up front. Buffer order
 * IS paint order. COMPLEX leaves are skipped until the complex
 * increment. */
static struct yetty_ycore_void_result scene_rebuild_staging(struct yetty_yscene_scene *scene,
                                                            float content_w, float content_h)
{
    const struct yetty_yscene_dom *dom = scene->dom;
    scene_cells_clear(scene);
    scene->staging_scratch_len = 0;
    scene->staged_record_count = 0;
    for (uint32_t i = 0; i < scene->complex_count; i++) {
        scene->complexes[i].seen = false;
    }

    /* Pass 1 — emit every stageable record into the scratch stream. */
    for (uint32_t i = 0; i < scene->leaf_count; i++) {
        const struct yscene_leaf *leaf = &scene->leaves[i];
        if (leaf->record_size == 0) {
            continue;
        }
        const struct yetty_yscene_dom_batch *batch = &dom->batches[leaf->batch_slot];
        if (leaf->kind == YSCENE_LEAF_TEXT) {
            struct yetty_ycore_void_result text_res =
                scene_stage_text_leaf(scene, leaf, batch->bytes + leaf->record_offset);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "yscene staging: text leaf");
            continue;
        }
        if (leaf->kind == YSCENE_LEAF_COMPLEX) {
            /* Own-pipeline draw: mark the instance live and refresh its
             * paint key + world anchor; drawn after the prim pass (the
             * z-run interleave is the follow-up). */
            uint64_t leaf_batch_stamp = dom->batches[leaf->batch_slot].batch_stamp;
            for (uint32_t complex_index = 0; complex_index < scene->complex_count;
                 complex_index++) {
                struct yscene_complex_instance *entry = &scene->complexes[complex_index];
                if (entry->batch_stamp == leaf_batch_stamp &&
                    entry->record_offset == leaf->record_offset) {
                    entry->seen = true;
                    entry->paint_z = leaf->paint_z;
                    entry->paint_seq = leaf->paint_seq;
                    entry->record_index = leaf->record_index;
                    entry->translate_x = leaf->translate_x;
                    entry->translate_y = leaf->translate_y;
                    break;
                }
            }
            continue;
        }
        if (leaf->kind != YSCENE_LEAF_PRIMITIVE) {
            continue;
        }
        uint32_t record_words = leaf->record_size / sizeof(uint32_t);
        struct yetty_ycore_void_result stage_res = scene_stage_record(
            scene, (const uint32_t *)(batch->bytes + leaf->record_offset), record_words,
            leaf->world_aabb);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stage_res, "yscene staging: prim");
        /* The scratch copy is mutable derived data: bake the world
         * transform into the geometry words (after the style header
         * [type, z_order, fill, stroke, stroke_w]) and fold inherited
         * opacity into the colors. */
        uint32_t *staged = &scene->staging_scratch[scene->staging_scratch_len - record_words];
        if (record_words > 5) {
            yetty_ysdf_geometry_transform(leaf->record_type, (float *)&staged[5],
                                          leaf->translate_x, leaf->translate_y, leaf->m00,
                                          leaf->m11);
        }
        if (leaf->world_opacity < 1.0f && record_words > 3) {
            staged[2] = scene_fold_opacity(staged[2], leaf->world_opacity);
            staged[3] = scene_fold_opacity(staged[3], leaf->world_opacity);
            /* Gradient prims carry their colors in type-specific payload
             * words (record layout: style header then geometry). Fold
             * those too or node opacity misses gradient fills. */
            if (leaf->record_type == YETTY_YSDF_LINEAR_GRADIENT_BOX && record_words > 15) {
                staged[14] = scene_fold_opacity(staged[14], leaf->world_opacity);
                staged[15] = scene_fold_opacity(staged[15], leaf->world_opacity);
            } else if (leaf->record_type == YETTY_YSDF_RADIAL_GRADIENT_BOX &&
                       record_words > 14) {
                staged[13] = scene_fold_opacity(staged[13], leaf->world_opacity);
                staged[14] = scene_fold_opacity(staged[14], leaf->world_opacity);
            }
        }
    }

    /* Pass 2 — assemble prim staging: [offset table][scratch records]. */
    uint32_t staged_count = scene->staged_record_count;
    size_t prim_words_needed = (size_t)staged_count + scene->staging_scratch_len;
    struct yetty_ycore_void_result ensure_res =
        scene_ensure_words(&scene->prim_staging, &scene->prim_staging_cap, prim_words_needed);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "yscene staging: prim ensure");
    for (uint32_t i = 0; i < staged_count; i++) {
        scene->prim_staging[i] = scene->staged_records[i].word_offset;
    }
    memcpy(&scene->prim_staging[staged_count], scene->staging_scratch,
           scene->staging_scratch_len * sizeof(uint32_t));
    scene->prim_staging_words = prim_words_needed;
    scene->staged_prim_count = staged_count;
    scene->staged_content_w = content_w;
    scene->staged_content_h = content_h;

    /* Pass 3 — cell buckets from the staged AABBs (ascending staged
     * index = paint order; no sort). */
    float cell_width = content_w / (float)YSCENE_GRID_COLS;
    float cell_height = content_h / (float)YSCENE_GRID_ROWS;
    for (uint32_t i = 0; i < staged_count; i++) {
        struct yetty_ycore_rectangle aabb = scene->staged_records[i].world_aabb;
        int32_t col_min = (int32_t)(aabb.min.x / cell_width);
        int32_t col_max = (int32_t)(aabb.max.x / cell_width);
        int32_t row_min = (int32_t)(aabb.min.y / cell_height);
        int32_t row_max = (int32_t)(aabb.max.y / cell_height);
        if (col_min < 0) {
            col_min = 0;
        }
        if (row_min < 0) {
            row_min = 0;
        }
        if (col_max >= (int32_t)YSCENE_GRID_COLS) {
            col_max = (int32_t)YSCENE_GRID_COLS - 1;
        }
        if (row_max >= (int32_t)YSCENE_GRID_ROWS) {
            row_max = (int32_t)YSCENE_GRID_ROWS - 1;
        }
        for (int32_t row = row_min; row <= row_max; row++) {
            for (int32_t col = col_min; col <= col_max; col++) {
                struct yetty_ycore_void_result push_res = scene_cell_push(
                    &scene->cells[(size_t)row * YSCENE_GRID_COLS + (size_t)col], i);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, push_res, "yscene staging: bucket");
            }
        }
    }

    /* Grid staging: offset table + sentinel + per-cell blocks. */
    size_t num_cells = (size_t)YSCENE_GRID_COLS * YSCENE_GRID_ROWS;
    size_t grid_words_needed = num_cells + 1;
    for (size_t i = 0; i < num_cells; i++) {
        if (scene->cells[i].count > 0) {
            grid_words_needed += 1u + scene->cells[i].count;
        }
    }
    ensure_res =
        scene_ensure_words(&scene->grid_staging, &scene->grid_staging_cap, grid_words_needed);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "yscene staging: grid ensure");
    uint32_t sentinel_offset = (uint32_t)num_cells;
    scene->grid_staging[sentinel_offset] = 0;
    uint32_t grid_cursor = sentinel_offset + 1;
    for (size_t i = 0; i < num_cells; i++) {
        const struct yscene_cell *cell = &scene->cells[i];
        if (cell->count == 0) {
            scene->grid_staging[i] = sentinel_offset;
            continue;
        }
        scene->grid_staging[i] = grid_cursor;
        scene->grid_staging[grid_cursor++] = cell->count;
        memcpy(&scene->grid_staging[grid_cursor], cell->indices,
               (size_t)cell->count * sizeof(uint32_t));
        grid_cursor += cell->count;
    }
    scene->grid_staging_words = grid_words_needed;
    scene->staging_generation = scene->derived_generation;
    scene->staging_valid = true;

    /* Instances whose source span left the committed scene die here. */
    scene_complex_sweep(scene);
    return YETTY_OK_VOID();
}

/* Paint-key order for the complex render pass. */
static int scene_complex_compare(const void *first_ptr, const void *second_ptr)
{
    const struct yscene_complex_instance *first = first_ptr;
    const struct yscene_complex_instance *second = second_ptr;
    if (first->paint_z != second->paint_z) {
        return first->paint_z < second->paint_z ? -1 : 1;
    }
    if (first->paint_seq != second->paint_seq) {
        return first->paint_seq < second->paint_seq ? -1 : 1;
    }
    if (first->record_index != second->record_index) {
        return first->record_index < second->record_index ? -1 : 1;
    }
    return 0;
}

/*===========================================================================
 * Figure overrides
 *=========================================================================*/

YETTY_ANNOTATE("override@yfigure:figure:render")
static struct yetty_ycore_void_result scene_render_slot(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_target *target)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene render: object");
    struct yetty_yscene_scene *scene = scene_res.value;
    struct yetty_ycore_void_result derive_res = scene_derive(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, derive_res, "yscene render: derive");
    if (!scene->binder || !target) {
        /* Headless (tests/tooling): the derive above keeps the host
         * contract exercised; there is nothing to draw into. */
        return yetty_yfigure_figure_dirty_set(obj, 0);
    }

    struct rectangle_result rect_res = yetty_yfigure_figure_rect_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yscene render: rect");
    struct yetty_ycore_rectangle rect = rect_res.value;
    float rect_w = rect.max.x - rect.min.x;
    float rect_h = rect.max.y - rect.min.y;
    if (rect_w <= 0.0f || rect_h <= 0.0f) {
        return yetty_yfigure_figure_dirty_set(obj, 0);
    }
    float content_w;
    float content_h;
    scene_content_extent(scene, rect, &content_w, &content_h);

    /* Font set changed (wire installs during derive) — regenerate the
     * dispatcher + children; the shader-hash change refinalizes the
     * binder on the update below. */
    if (scene->font_generation != scene->last_emitted_font_generation) {
        struct yetty_ycore_void_result dispatcher_res = scene_rebuild_font_dispatcher(scene);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_res, "yscene render: dispatcher");
    }

    /* Staging is keyed on BOTH the derived generation and the content
     * extent it bucketed against — an extent change (set_content_size,
     * rect resize) shifts cell geometry, so old membership would read
     * prims from the wrong cells. */
    bool staging_rebuilt = false;
    if (!scene->staging_valid || scene->staging_generation != scene->derived_generation ||
        scene->staged_content_w != content_w || scene->staged_content_h != content_h) {
        struct yetty_ycore_void_result staging_res =
            scene_rebuild_staging(scene, content_w, content_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, staging_res, "yscene render: staging");
        staging_rebuilt = true;
    }
    ydebug("yscene_render: rect=(%.1f,%.1f)-(%.1f,%.1f) content=%.0fx%.0f leaves=%u staged=%u "
           "complexes=%u gen=%llu rebuilt=%d scroll=(%.1f,%.1f)",
           rect.min.x, rect.min.y, rect.max.x, rect.max.y, content_w, content_h,
           scene->leaf_count, scene->staged_prim_count, scene->complex_count,
           (unsigned long long)scene->derived_generation, staging_rebuilt ? 1 : 0,
           scene->scroll_x, scene->scroll_y);

    /* Pointers refresh every frame (realloc moves them); UPLOAD dirt is
     * set only when staging actually changed — a scroll-only frame
     * re-uploads nothing. */
    scene->rs.buffers[0].data = (uint8_t *)scene->grid_staging;
    scene->rs.buffers[0].size = scene->grid_staging_words * sizeof(uint32_t);
    scene->rs.buffers[1].data = (uint8_t *)scene->prim_staging;
    scene->rs.buffers[1].size = scene->prim_staging_words * sizeof(uint32_t);
    if (staging_rebuilt) {
        scene->rs.buffers[0].dirty = 1;
        scene->rs.buffers[1].dirty = 1;
    }

    scene->rs.uniforms[U_GRID_SIZE].vec2[0] = (float)YSCENE_GRID_COLS;
    scene->rs.uniforms[U_GRID_SIZE].vec2[1] = (float)YSCENE_GRID_ROWS;
    scene->rs.uniforms[U_CELL_SIZE].vec2[0] = content_w / (float)YSCENE_GRID_COLS;
    scene->rs.uniforms[U_CELL_SIZE].vec2[1] = content_h / (float)YSCENE_GRID_ROWS;
    scene->rs.uniforms[U_VIEW_SIZE].vec2[0] = rect_w;
    scene->rs.uniforms[U_VIEW_SIZE].vec2[1] = rect_h;
    /* View transform on the GPU — no staging rebuild. Shader mapping is
     * document = screen / cz_scale + cz_off, the same formula hit-test
     * uses, so pixels and hits agree at every scale. */
    scene->rs.uniforms[U_CZ_SCALE].f32 = scene->view_scale > 0.0f ? scene->view_scale : 1.0f;
    scene->rs.uniforms[U_CZ_OFF].vec2[0] = scene->scroll_x;
    scene->rs.uniforms[U_CZ_OFF].vec2[1] = scene->scroll_y;
    scene->rs.uniforms[U_PRIM_COUNT].u32 = scene->staged_prim_count;
    if (scene->runtime) {
        scene->rs.uniforms[U_TIME].f32 = (float)scene->runtime->frame_time_sec;
    }
    scene->rs.pixel_size.width = rect_w;
    scene->rs.pixel_size.height = rect_h;

    struct yetty_ycore_void_result submit_res = scene->binder->ops->submit(scene->binder,
                                                                           &scene->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, submit_res, "yscene render: binder submit");
    if (!scene->binder_finalized) {
        struct yetty_ycore_void_result finalize_res = scene->binder->ops->finalize(scene->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, finalize_res, "yscene render: binder finalize");
        scene->binder_finalized = true;
    }
    struct yetty_ycore_void_result update_res = scene->binder->ops->update(scene->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "yscene render: binder update");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yscene render: target view NULL");
    }
    WGPUCommandEncoderDescriptor encoder_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(scene->device, &encoder_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "yscene render: encoder create");
    }
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = view;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);

    wgpuRenderPassEncoderSetViewport(pass, rect.min.x, rect.min.y, rect_w, rect_h, 0.0f, 1.0f);
    /* Scissor: the figure rect clamped to the target. */
    float target_min_x = target->viewport.x;
    float target_min_y = target->viewport.y;
    float target_max_x = target->viewport.x + target->viewport.w;
    float target_max_y = target->viewport.y + target->viewport.h;
    float scissor_min_x = rect.min.x > target_min_x ? rect.min.x : target_min_x;
    float scissor_min_y = rect.min.y > target_min_y ? rect.min.y : target_min_y;
    float scissor_max_x = rect.max.x < target_max_x ? rect.max.x : target_max_x;
    float scissor_max_y = rect.max.y < target_max_y ? rect.max.y : target_max_y;
    if (scissor_max_x <= scissor_min_x || scissor_max_y <= scissor_min_y) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBufferDescriptor skip_desc = {0};
        WGPUCommandBuffer skip_buffer = wgpuCommandEncoderFinish(encoder, &skip_desc);
        wgpuQueueSubmit(scene->queue, 1, &skip_buffer);
        wgpuCommandBufferRelease(skip_buffer);
        wgpuCommandEncoderRelease(encoder);
        return yetty_yfigure_figure_dirty_set(obj, 0);
    }
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)scissor_min_x, (uint32_t)scissor_min_y,
                                        (uint32_t)(scissor_max_x - scissor_min_x),
                                        (uint32_t)(scissor_max_y - scissor_min_y));

    WGPURenderPipeline pipeline = scene->binder->ops->get_pipeline(scene->binder);
    WGPUBuffer quad_vertex_buffer = scene->binder->ops->get_quad_vertex_buffer(scene->binder);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (quad_vertex_buffer) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vertex_buffer, 0, WGPU_WHOLE_SIZE);
    }
    struct yetty_ycore_void_result bind_res = scene->binder->ops->bind(scene->binder, pass, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bind_res, "yscene render: binder bind");
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor command_desc = {0};
    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(encoder, &command_desc);
    wgpuQueueSubmit(scene->queue, 1, &command_buffer);
    wgpuCommandBufferRelease(command_buffer);
    wgpuCommandEncoderRelease(encoder);

    /* Complex pass — own-pipeline draws in paint-key order, AFTER the
     * whole prim pass (ygrid-parity interim; the z-run interleave that
     * lets prims cover a complex is the follow-up). Anchor math: the
     * composite render op adds the record's node-LOCAL bounds scaled by
     * content_scale, so the anchor carries rect origin + (node world
     * translate − scroll) in screen units and content_scale carries the
     * view scale. Publish the figure scissor as target->clip so the
     * instance's own pass clips to this figure. */
    if (scene->complex_count > 0) {
        qsort(scene->complexes, scene->complex_count, sizeof(struct yscene_complex_instance),
              scene_complex_compare);
        float view_scale = scene->view_scale > 0.0f ? scene->view_scale : 1.0f;
        target->clip.x = scissor_min_x;
        target->clip.y = scissor_min_y;
        target->clip.w = scissor_max_x - scissor_min_x;
        target->clip.h = scissor_max_y - scissor_min_y;
        for (uint32_t i = 0; i < scene->complex_count; i++) {
            struct yscene_complex_instance *entry = &scene->complexes[i];
            if (!entry->instance || !entry->instance->render) {
                continue;
            }
            float anchor_x =
                rect.min.x + (entry->translate_x - scene->scroll_x) * view_scale;
            float anchor_y =
                rect.min.y + (entry->translate_y - scene->scroll_y) * view_scale;
            entry->instance->content_scale = view_scale;
            struct yetty_ycore_void_result render_res =
                yetty_ydraw_composite_render(entry->instance, target, anchor_x, anchor_y);
            if (YETTY_IS_ERR(render_res)) {
                ydebug("yscene render: complex instance failed: %s", render_res.error.msg);
                yetty_ycore_error_destroy(render_res.error);
            }
            entry->instance->dirty = 0;
        }
        target->clip.w = 0;
        target->clip.h = 0;
    }

    return yetty_yfigure_figure_dirty_set(obj, 0);
}

YETTY_ANNOTATE("override@yfigure:figure:destroy")
static struct yetty_ycore_void_result scene_destroy_slot(struct yetty_yclass_object *obj)
{
    /* Best-effort teardown: free everything reachable even if the slice
     * resolve fails, stashing the first error. */
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    struct yetty_yscene_scene *scene = YETTY_IS_OK(scene_res) ? scene_res.value : NULL;
    struct yetty_ycore_void_result result =
        YETTY_IS_ERR(scene_res)
            ? YETTY_ERR(yetty_ycore_void, "yscene destroy: object", scene_res)
            : YETTY_OK_VOID();
    if (scene) {
        if (scene->binder) {
            scene->binder->ops->destroy(scene->binder);
            scene->binder = NULL;
        }
        free(scene->sdf_lib_code.data);
        free(scene->effects_lib_code.data);
        free(scene->layer_shader_code.data);
        free(scene->combined_shader);
        for (uint32_t i = 0; i < scene->complex_count; i++) {
            if (scene->complexes[i].instance) {
                yetty_ydraw_composite_destroy(scene->complexes[i].instance);
            }
        }
        free(scene->complexes);
        scene->complexes = NULL;
        scene->complex_count = 0;
        if (scene->font_cache) {
            /* After the binder (which referenced the font resource
             * sets). */
            yetty_yfont_cache_destroy(scene->font_cache);
            scene->font_cache = NULL;
        }
        free(scene->grid_staging);
        free(scene->prim_staging);
        free(scene->staging_scratch);
        free(scene->staged_records);
        for (uint32_t i = 0; i < YSCENE_GRID_COLS * YSCENE_GRID_ROWS; i++) {
            free(scene->cells[i].indices);
        }
        struct yetty_ycore_void_result dom_res = yetty_yscene_dom_destroy(scene->dom);
        if (YETTY_IS_ERR(dom_res)) {
            if (YETTY_IS_OK(result)) {
                result = YETTY_ERR(yetty_ycore_void, "yscene destroy: dom", dom_res);
            } else {
                yetty_ycore_error_destroy(dom_res.error);
            }
        }
        scene->dom = NULL;
        free(scene->leaves);
        scene->leaves = NULL;
        free(scene->leaves_scratch);
        scene->leaves_scratch = NULL;
        if (scene->owns_registry && scene->registry) {
            yetty_ydraw_drawable_list_registry_destroy(scene->registry);
            scene->registry = NULL;
        }
    }
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_OK(result) && YETTY_IS_ERR(free_res)) {
        return YETTY_ERR(yetty_ycore_void, "yscene destroy: object_free", free_res);
    }
    if (YETTY_IS_ERR(free_res)) {
        yetty_ycore_error_destroy(free_res.error);
    }
    return result;
}

YETTY_ANNOTATE("override@yfigure:figure:process_bytes")
static struct yetty_ycore_void_result scene_process_bytes_slot(struct yetty_yclass_object *obj,
                                                               const uint8_t *bytes,
                                                               size_t bytes_len)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene process_bytes");
    struct yetty_yscene_scene *scene = scene_res.value;
    struct yetty_ydraw_drawable_list_registry_ptr_result registry_res = scene_registry(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, registry_res, "yscene process_bytes: registry");

    if (bytes_len > UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "yscene process_bytes: body exceeds 4 GiB");
    }
    struct yscene_body_walk walk = {
        .scene = scene,
        .dom = scene->dom,
        .registry = registry_res.value,
    };
    /* Validate the WHOLE body before mutating anything: a valid prefix
     * followed by a malformed record must leave no pending state a
     * later commit could publish. */
    struct yetty_ycore_void_result validate_res =
        scene_validate_body(&walk, bytes, bytes_len, /*depth=*/0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, validate_res, "yscene process_bytes: validate");
    /* Stream-update ordinals restart per body (ygrid's envelope analog:
     * a later body's mints overwrite the same ids, most-recent-wins). */
    scene->stream_next_ordinal = 0;
    struct yetty_ycore_void_result body_res = scene_apply_body(&walk, 0, false, bytes, bytes_len);
    if (YETTY_IS_ERR(body_res)) {
        /* Validation makes this allocation-only, but a partial apply is
         * a partial apply: poison the pending state so no later commit
         * can publish it. A full reset clears the poison. */
        scene->pending_poisoned = true;
        return YETTY_ERR(yetty_ycore_void, "yscene process_bytes: body (pending poisoned — "
                                           "reset required)", body_res);
    }
    /* One envelope = one produced frame — publish it. */
    struct yetty_ycore_uint64_result commit_res = scene_commit_and_publish(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, commit_res, "yscene process_bytes: commit");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("override@yfigure:figure:reset_content")
static struct yetty_ycore_void_result scene_reset_content_slot(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_for_method(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene reset_content");
    struct yetty_ycore_void_result zero_res = yetty_yscene_dom_zero(scene_res.value->dom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zero_res, "yscene reset_content: zero");
    scene_res.value->pending_poisoned = false;
    scene_reset_wire_fonts(scene_res.value);
    scene_reset_complexes(scene_res.value);
    scene_reset_view(scene_res.value);
    struct yetty_ycore_uint64_result commit_res = scene_commit_and_publish(scene_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, commit_res, "yscene reset_content: commit");
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("override@yfigure:figure:set_scroll")
static struct yetty_ycore_void_result scene_set_scroll_slot(struct yetty_yclass_object *obj,
                                                            float scroll_x, float scroll_y)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene set_scroll: object");
    /* View state only — nothing re-derives, nothing re-emits. */
    scene_res.value->scroll_x = scroll_x;
    scene_res.value->scroll_y = scroll_y;
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

YETTY_ANNOTATE("override@yfigure:figure:set_content_size")
static struct yetty_ycore_void_result scene_set_content_size_slot(struct yetty_yclass_object *obj,
                                                                  float content_w,
                                                                  float content_h)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene set_content_size: object");
    scene_res.value->content_w = content_w;
    scene_res.value->content_h = content_h;
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

/*===========================================================================
 * dump_state — deterministic text snapshot for tests: the tree, then
 * (when derived) the flat paint-ordered leaves.
 *=========================================================================*/

struct scene_dump_buffer {
    char *data;
    size_t length;
    size_t capacity;
};

static struct yetty_ycore_void_result scene_dump_appendf(struct scene_dump_buffer *buffer,
                                                         const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list measure_arguments;
    va_copy(measure_arguments, arguments);
    int needed = vsnprintf(NULL, 0, format, measure_arguments);
    va_end(measure_arguments);
    if (needed < 0) {
        va_end(arguments);
        return YETTY_ERR(yetty_ycore_void, "yscene dump: format failed");
    }
    if (buffer->length + (size_t)needed + 1 > buffer->capacity) {
        size_t new_capacity = buffer->capacity ? buffer->capacity : 1024;
        while (buffer->length + (size_t)needed + 1 > new_capacity) {
            new_capacity *= 2;
        }
        char *grown = realloc(buffer->data, new_capacity);
        if (!grown) {
            va_end(arguments);
            return YETTY_ERR(yetty_ycore_void, "yscene dump: alloc failed");
        }
        buffer->data = grown;
        buffer->capacity = new_capacity;
    }
    buffer->length += (size_t)vsnprintf(buffer->data + buffer->length,
                                        buffer->capacity - buffer->length, format, arguments);
    va_end(arguments);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scene_dump_node(const struct yetty_yscene_dom *dom,
                                                      uint32_t slot, int indent,
                                                      struct scene_dump_buffer *buffer)
{
    const struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    uint32_t record_total = 0;
    for (uint32_t i = 0; i < node->batch_count; i++) {
        record_total += dom->batches[node->batch_slots[i]].record_count;
    }
    struct yetty_ycore_void_result append_res = scene_dump_appendf(
        buffer, "%*snode id=%llu seq=%u z=%d batches=%u records=%u children=%u", indent, "",
        (unsigned long long)node->external_id, node->node_seq, node->paint_z, node->batch_count,
        record_total, node->child_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "yscene dump: node line");
    if (node->has_clip) {
        append_res = scene_dump_appendf(buffer, " clip=(%.1f,%.1f,%.1f,%.1f)", node->clip.min.x,
                                        node->clip.min.y, node->clip.max.x, node->clip.max.y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "yscene dump: clip");
    }
    if (node->opacity != 1.0f) {
        append_res = scene_dump_appendf(buffer, " opacity=%.2f", node->opacity);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "yscene dump: opacity");
    }
    append_res = scene_dump_appendf(buffer, "\n");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "yscene dump: newline");
    for (uint32_t i = 0; i < node->child_count; i++) {
        append_res = scene_dump_node(dom, node->children[i], indent + 2, buffer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "yscene dump: child");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yfigure:figure:dump_state")
static struct yetty_ycore_char_ptr_result scene_dump_state_slot(struct yetty_yclass_object *obj,
                                                                int indent)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, scene_res, "yscene dump: object");
    struct yetty_yscene_scene *scene = scene_res.value;

    struct scene_dump_buffer buffer = {0};
    struct yetty_ycore_void_result append_res;
    if (!scene->dom) {
        append_res = scene_dump_appendf(&buffer, "%*sscene empty\n", indent, "");
        if (YETTY_IS_ERR(append_res)) {
            free(buffer.data);
            return YETTY_ERR(yetty_ycore_char_ptr, "yscene dump", append_res);
        }
        return YETTY_OK(yetty_ycore_char_ptr, buffer.data);
    }
    append_res = scene_dump_appendf(&buffer, "%*sscene generation=%llu nodes=%u\n", indent, "",
                                    (unsigned long long)scene->dom->committed_generation,
                                    scene->dom->live_node_count);
    if (YETTY_IS_OK(append_res)) {
        append_res = scene_dump_node(scene->dom, YETTY_YSCENE_DOM_ROOT_SLOT, indent + 2, &buffer);
    }
    if (YETTY_IS_OK(append_res) && scene->derived_valid) {
        append_res = scene_dump_appendf(&buffer, "%*sleaves count=%u\n", indent + 2, "",
                                        scene->leaf_count);
        for (uint32_t i = 0; YETTY_IS_OK(append_res) && i < scene->leaf_count; i++) {
            const struct yscene_leaf *leaf = &scene->leaves[i];
            append_res = scene_dump_appendf(
                &buffer, "%*sleaf z=%d seq=%u idx=%u type=0x%08x node=%llu "
                         "aabb=(%.1f,%.1f,%.1f,%.1f)\n",
                indent + 4, "", leaf->paint_z, leaf->paint_seq, leaf->record_index,
                leaf->record_type, (unsigned long long)leaf->node_external_id,
                leaf->world_aabb.min.x, leaf->world_aabb.min.y, leaf->world_aabb.max.x,
                leaf->world_aabb.max.y);
        }
    }
    if (YETTY_IS_ERR(append_res)) {
        free(buffer.data);
        return YETTY_ERR(yetty_ycore_char_ptr, "yscene dump", append_res);
    }
    return YETTY_OK(yetty_ycore_char_ptr, buffer.data);
}

/*===========================================================================
 * Exposed helpers — creation, derive, hit-test, introspection
 *=========================================================================*/

/* Create a scene figure. `context == NULL` (or a context without a
 * runtime) is HEADLESS mode — tests/tooling: no shader load, no binder;
 * the tree, adapter, derive and hit-test all work, render draws
 * nothing. With a real context the scene binds the framework's shared
 * drawable registry and builds its GPU pipeline (shared ydraw-layer
 * machinery: binder + ygrid.wgsl + ysdf/effects libs). */
YETTY_ANNOTATE("expose")
struct yetty_yscene_scene_ptr_result yetty_yscene_create(struct yetty_ycore_rectangle rect,
                                                         const struct yetty_context *context)
{
    struct yetty_yclass_object_ptr_result object_res = yetty_yscene_scene_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yscene_scene_ptr, object_res, "yscene create: object");
    struct yetty_yclass_object *obj = object_res.value;
    struct yetty_ycore_void_result rect_res = yetty_yfigure_figure_rect_set(obj, rect);
    if (YETTY_IS_ERR(rect_res)) {
        struct yetty_ycore_void_result teardown_res = scene_destroy_slot(obj);
        if (YETTY_IS_ERR(teardown_res)) {
            yetty_ycore_error_destroy(teardown_res.error);
        }
        return YETTY_ERR(yetty_yscene_scene_ptr, "yscene create: rect", rect_res);
    }
    struct yetty_ycore_void_result dirty_res = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dirty_res)) {
        struct yetty_ycore_void_result teardown_res = scene_destroy_slot(obj);
        if (YETTY_IS_ERR(teardown_res)) {
            yetty_ycore_error_destroy(teardown_res.error);
        }
        return YETTY_ERR(yetty_yscene_scene_ptr, "yscene create: dirty", dirty_res);
    }
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    if (YETTY_IS_ERR(scene_res)) {
        struct yetty_ycore_void_result teardown_res = scene_destroy_slot(obj);
        if (YETTY_IS_ERR(teardown_res)) {
            yetty_ycore_error_destroy(teardown_res.error);
        }
        return YETTY_ERR(yetty_yscene_scene_ptr, "yscene create: from_obj", scene_res);
    }
    struct yetty_yscene_scene *scene = scene_res.value;

    bool headless = (context == NULL) || (context->runtime == NULL);
    if (!headless) {
        scene->runtime = context->runtime;
        scene->device = context->runtime->gpu.device;
        scene->queue = context->runtime->gpu.queue;
        scene->target_format = context->runtime->gpu.surface_format;
        scene->allocator = context->runtime->gpu.allocator;
        struct yetty_ycore_void_result registry_bind_res = scene_bind_registry(
            scene, context->runtime->drawable_registry, /*owns=*/false);
        if (YETTY_IS_ERR(registry_bind_res)) {
            struct yetty_ycore_void_result teardown_res = scene_destroy_slot(obj);
            if (YETTY_IS_ERR(teardown_res)) {
                yetty_ycore_error_destroy(teardown_res.error);
            }
            return YETTY_ERR(yetty_yscene_scene_ptr, "yscene create: registry bind",
                             registry_bind_res);
        }
        /* HiDPI: the view scale, never baked into wire coordinates. */
        float content_scale = context->runtime->gpu.app_gpu_context.content_scale;
        if (content_scale > 0.0f) {
            scene->view_scale = content_scale;
        }
        /* Wire-font plumbing (FONT_RESOURCE → MSDF slots). Best-effort:
         * without a cache, fonts drop but SDF content still renders. */
        struct yetty_yconfig_config *config = context->runtime->config;
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        snprintf(scene->shaders_dir, sizeof(scene->shaders_dir), "%s",
                 shaders_dir ? shaders_dir : "");
        const char *cache_dir = config->ops->get_string(config, "paths/cache", "");
        snprintf(scene->cache_dir, sizeof(scene->cache_dir), "%s", cache_dir ? cache_dir : "");
        const char *data_dir = config->ops->get_string(config, "paths/data", "");
        snprintf(scene->data_dir, sizeof(scene->data_dir), "%s", data_dir ? data_dir : "");
        scene->msdf_generator = context->runtime->gpu.msdf_generator;
        struct yetty_yfont_cache_ptr_result font_cache_res =
            yetty_yfont_cache_create(scene->shaders_dir);
        if (YETTY_IS_OK(font_cache_res)) {
            scene->font_cache = font_cache_res.value;
        } else {
            ydebug("yscene create: font cache unavailable: %s", font_cache_res.error.msg);
            yetty_ycore_error_destroy(font_cache_res.error);
        }
        struct yetty_ycore_void_result libs_res = scene_load_shader_libs(scene, context);
        if (YETTY_IS_ERR(libs_res)) {
            struct yetty_ycore_void_result teardown_res = scene_destroy_slot(obj);
            if (YETTY_IS_ERR(teardown_res)) {
                yetty_ycore_error_destroy(teardown_res.error);
            }
            return YETTY_ERR(yetty_yscene_scene_ptr, "yscene create: shader libs", libs_res);
        }
        struct yetty_ycore_void_result binder_res = scene_build_binder(scene);
        if (YETTY_IS_ERR(binder_res)) {
            struct yetty_ycore_void_result teardown_res = scene_destroy_slot(obj);
            if (YETTY_IS_ERR(teardown_res)) {
                yetty_ycore_error_destroy(teardown_res.error);
            }
            return YETTY_ERR(yetty_yscene_scene_ptr, "yscene create: binder", binder_res);
        }
    }
    return YETTY_OK(yetty_yscene_scene_ptr, scene);
}

/* Host inputs every wire-minted scene borrows. Lifetime: owned by the
 * host (terminal), outliving every scene minted through the registry —
 * the same contract as ygrid's factory args bundle. */
struct YETTY_ANNOTATE("expose") yetty_yscene_factory_args {
    /* Complex renderer (yplot / yimage / yvideo / …). NULL → composite
     * records drop with trace. */
    struct yetty_ydraw_composite_factory *composite_factory;
    /* Slot-0 font for TEXT spans with the default/negative font id.
     * NULL → such spans drop until a wire font arrives. */
    struct yetty_yfont_font *default_font;
};

/* Wire factory: mint a scene for a CREATE_CHILD of kind "yscene". The
 * envelope body (typed structure via the adapter) flows in afterwards
 * through process_bytes. */
static struct yetty_yfigure_figure_ptr_result scene_wire_factory(
    struct yetty_ycore_rectangle rect, const struct yetty_context *context, void *user)
{
    const struct yetty_yscene_factory_args *args = user;
    struct yetty_yscene_scene_ptr_result scene_res = yetty_yscene_create(rect, context);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, scene_res, "yscene factory: create");
    struct yetty_yscene_scene *scene = scene_res.value;
    if (args) {
        scene->composite_factory = args->composite_factory;
        if (args->default_font) {
            struct yetty_ycore_void_result font_res =
                scene_set_font(scene, 0, args->default_font);
            if (YETTY_IS_ERR(font_res)) {
                ydebug("yscene factory: default font install failed: %s", font_res.error.msg);
                yetty_ycore_error_destroy(font_res.error);
            }
        }
    }
    struct yetty_yclass_object_ptr_result object_res = yetty_yscene_scene_to(scene);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, object_res, "yscene factory: object");
    return YETTY_OK(yetty_yfigure_figure_ptr,
                    (struct yetty_yfigure_figure *)(object_res.value + 1));
}

/* Register the "yscene" figure kind so containers can mint scenes from
 * wire CREATE_CHILD records. `args` is BORROWED for the registry's
 * lifetime (host-owned bundle); NULL registers a bare scene (no
 * composites, no default font). Call at terminal/host create time. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yscene_register_factory(
    struct yetty_yfigure_registry *registry, const struct yetty_yscene_factory_args *args)
{
    return yetty_yfigure_registry_register(registry, yetty_yfigure_kind_token("yscene"),
                                           scene_wire_factory, (void *)args);
}

/* Host-side default font (slot 0) for a hand-created scene. Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yscene_set_default_font(struct yetty_yclass_object *obj,
                                                             struct yetty_yfont_font *font)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene set_default_font: object");
    return scene_set_font(scene_res.value, 0, font);
}

/* Host-side complex renderer for a hand-created scene. Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yscene_set_composite_factory(
    struct yetty_yclass_object *obj, struct yetty_ydraw_composite_factory *factory)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene set_composite_factory: object");
    scene_res.value->composite_factory = factory;
    return YETTY_OK_VOID();
}

/* The figure base of this scene (the container's handle on it). */
YETTY_ANNOTATE("expose")
struct yetty_yfigure_figure *yetty_yscene_as_figure(struct yetty_yscene_scene *scene)
{
    struct yetty_yclass_object_ptr_result object_res = yetty_yscene_scene_to(scene);
    if (YETTY_IS_ERR(object_res)) {
        yetty_ycore_error_destroy(object_res.error);
        return NULL;
    }
    return (struct yetty_yfigure_figure *)(object_res.value + 1);
}

/* Rebuild the derived world state from the latest committed generation
 * (no-op when already current). Render does this implicitly; tests and
 * hit-test callers may want it explicitly. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yscene_derive(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene derive: object");
    return scene_derive(scene_res.value);
}

/* Number of derived paint leaves (after derive). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yscene_leaf_count(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, scene_res, "yscene leaf_count: object");
    return YETTY_OK(yetty_ycore_uint32, scene_res.value->leaf_count);
}

/* Hit-test a SCREEN point against the derived scene: reverse paint
 * order over world AABBs + effective clips. Returns the owning node's
 * external id; 0 = no leaf hit (the document root / background). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_yscene_hit_test(struct yetty_yclass_object *obj,
                                                       float screen_x, float screen_y)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, scene_res, "yscene hit_test: object");
    struct yetty_yscene_scene *scene = scene_res.value;
    struct yetty_ycore_void_result derive_res = scene_derive(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, derive_res, "yscene hit_test: derive");

    float scale = scene->view_scale > 0.0f ? scene->view_scale : 1.0f;
    float document_x = screen_x / scale + scene->scroll_x;
    float document_y = screen_y / scale + scene->scroll_y;

    for (uint32_t i = scene->leaf_count; i-- > 0;) {
        const struct yscene_leaf *leaf = &scene->leaves[i];
        if (leaf->has_world_clip && !rect_contains(leaf->world_clip, document_x, document_y)) {
            continue;
        }
        if (rect_contains(leaf->world_aabb, document_x, document_y)) {
            return YETTY_OK(yetty_ycore_uint64, leaf->node_external_id);
        }
    }
    return YETTY_OK(yetty_ycore_uint64, 0);
}

/* Render-plan snapshot for headless tests: derive, build staging
 * against the CURRENT rect/content extent (staging is pure CPU — no
 * binder needed), and dump the exact data destined for GPU upload:
 * staged count, extent, per-prim word summaries, cell occupancy, and
 * the view mapping. Caller frees the string. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_char_ptr_result yetty_yscene_render_plan(struct yetty_yclass_object *obj)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, scene_res, "yscene render_plan: object");
    struct yetty_yscene_scene *scene = scene_res.value;
    struct yetty_ycore_void_result derive_res = scene_derive(scene);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, derive_res, "yscene render_plan: derive");

    struct rectangle_result rect_res = yetty_yfigure_figure_rect_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, rect_res, "yscene render_plan: rect");
    float content_w;
    float content_h;
    scene_content_extent(scene, rect_res.value, &content_w, &content_h);
    if (!scene->staging_valid || scene->staging_generation != scene->derived_generation ||
        scene->staged_content_w != content_w || scene->staged_content_h != content_h) {
        struct yetty_ycore_void_result staging_res =
            scene_rebuild_staging(scene, content_w, content_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, staging_res, "yscene render_plan: staging");
    }

    struct scene_dump_buffer buffer = {0};
    struct yetty_ycore_void_result append_res = scene_dump_appendf(
        &buffer,
        "plan generation=%llu staged=%u extent=%.1fx%.1f cell=%.2fx%.2f "
        "scroll=%.1f,%.1f scale=%.2f\n",
        (unsigned long long)scene->derived_generation, scene->staged_prim_count, content_w,
        content_h, content_w / (float)YSCENE_GRID_COLS, content_h / (float)YSCENE_GRID_ROWS,
        scene->scroll_x, scene->scroll_y, scene->view_scale);
    for (uint32_t i = 0; YETTY_IS_OK(append_res) && i < scene->staged_prim_count; i++) {
        uint32_t data_offset = scene->prim_staging[i];
        /* +1 skips the rolling_row prefix; word 0 there is the type. */
        const uint32_t *record = &scene->prim_staging[scene->staged_prim_count + data_offset];
        append_res = scene_dump_appendf(&buffer, "prim idx=%u type=0x%08x fill=0x%08x\n", i,
                                        record[1], record[3]);
    }
    uint32_t occupied_cells = 0;
    for (uint32_t i = 0; i < YSCENE_GRID_COLS * YSCENE_GRID_ROWS; i++) {
        if (scene->cells[i].count > 0) {
            occupied_cells++;
        }
    }
    if (YETTY_IS_OK(append_res)) {
        append_res = scene_dump_appendf(&buffer, "cells occupied=%u of %u\n", occupied_cells,
                                        YSCENE_GRID_COLS * YSCENE_GRID_ROWS);
    }
    if (YETTY_IS_ERR(append_res)) {
        free(buffer.data);
        return YETTY_ERR(yetty_ycore_char_ptr, "yscene render_plan", append_res);
    }
    return YETTY_OK(yetty_ycore_char_ptr, buffer.data);
}

/* View scale (HiDPI / zoom) — derive/view state, never baked into wire
 * coordinates. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yscene_set_view_scale(struct yetty_yclass_object *obj,
                                                           float view_scale)
{
    struct yetty_yscene_scene_ptr_result scene_res = scene_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_res, "yscene set_view_scale: object");
    scene_res.value->view_scale = view_scale > 0.0f ? view_scale : 1.0f;
    return yetty_yfigure_figure_dirty_set(obj, 1);
}

#include "yetty/gen/impl/yscene/scene.c"
