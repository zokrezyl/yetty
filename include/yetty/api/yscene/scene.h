/* GENERATED — do not edit. */
/* Object API for regular class(es) `scene` (implementation module: yscene).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YSCENE_SCENE_H
#define YETTY_YCLASSGEN_API_YSCENE_SCENE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_ydraw_composite_factory;
struct yetty_ydraw_drawable_list_registry;
struct yetty_yfigure_figure;
struct yetty_yfigure_registry;
struct yetty_yfont_font;
struct yetty_yscene_scene;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_FACTORY_ARGS
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_FACTORY_ARGS
/* Host inputs every wire-minted scene borrows. Lifetime: owned by the
 * host (terminal), outliving every scene minted through the registry —
 * the same contract as ygrid's factory args bundle. */
struct yetty_yscene_factory_args {
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_yfont_font *default_font;
};
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yscene_scene;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_SCENE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_SCENE_PTR_RESULT
struct yetty_yscene_scene_ptr_result {
    int ok;
    union {
        struct yetty_yscene_scene *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yscene_scene_ptr_result yetty_yscene_scene_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yscene_scene_to(struct yetty_yscene_scene *data);

/* Bind the shared wire-record registry (borrowed — the framework's
 * instance) to the scene AND its dom, as one binding. Rejected once the
 * dom holds content — record strides must not change under existing
 * spans. */
struct yetty_ycore_void_result yetty_yscene_set_registry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list_registry *registry);
struct yetty_ycore_void_result yetty_yscene_node_declare(struct yetty_yclass_object *obj,
                                                         uint64_t external_id,
                                                         uint64_t parent_external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_transform(struct yetty_yclass_object *obj,
                                                               uint64_t external_id, float m00,
                                                               float m01, float m10, float m11,
                                                               float translate_x,
                                                               float translate_y);
struct yetty_ycore_void_result yetty_yscene_node_set_clip(struct yetty_yclass_object *obj,
                                                          uint64_t external_id, float min_x,
                                                          float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yscene_node_clear_clip(struct yetty_yclass_object *obj,
                                                            uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_opacity(struct yetty_yclass_object *obj,
                                                             uint64_t external_id, float opacity);
struct yetty_ycore_void_result yetty_yscene_node_set_z(struct yetty_yclass_object *obj,
                                                       uint64_t external_id, int32_t paint_z);
struct yetty_ycore_void_result yetty_yscene_node_set_content(struct yetty_yclass_object *obj,
                                                             uint64_t external_id,
                                                             struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_append_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_replace_batch(struct yetty_yclass_object *obj,
                                                               uint64_t external_id,
                                                               uint32_t batch_index,
                                                               struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_remove_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              uint32_t batch_index);
struct yetty_ycore_void_result yetty_yscene_node_delete(struct yetty_yclass_object *obj,
                                                        uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_zero(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_commit(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *ctx);

/* Create a scene figure. `context == NULL` (or a context without a
 * runtime) is HEADLESS mode — tests/tooling: no shader load, no binder;
 * the tree, adapter, derive and hit-test all work, render draws
 * nothing. With a real context the scene binds the framework's shared
 * drawable registry and builds its GPU pipeline (shared ydraw-layer
 * machinery: binder + ygrid.wgsl + ysdf/effects libs). */
struct yetty_yscene_scene_ptr_result yetty_yscene_create(struct yetty_ycore_rectangle rect,
                                                         const struct yetty_context *context);
/* Register the "yscene" figure kind so containers can mint scenes from
 * wire CREATE_CHILD records. `args` is BORROWED for the registry's
 * lifetime (host-owned bundle); NULL registers a bare scene (no
 * composites, no default font). Call at terminal/host create time. */
struct yetty_ycore_void_result yetty_yscene_register_factory(
    struct yetty_yfigure_registry *registry, const struct yetty_yscene_factory_args *args);
/* Host-side default font (slot 0) for a hand-created scene. Borrowed. */
struct yetty_ycore_void_result yetty_yscene_set_default_font(struct yetty_yclass_object *obj,
                                                             struct yetty_yfont_font *font);
/* Host-side complex renderer for a hand-created scene. Borrowed. */
struct yetty_ycore_void_result yetty_yscene_set_composite_factory(
    struct yetty_yclass_object *obj, struct yetty_ydraw_composite_factory *factory);
/* The figure base of this scene (the container's handle on it). */
struct yetty_yfigure_figure *yetty_yscene_as_figure(struct yetty_yscene_scene *scene);
/* Rebuild the derived world state from the latest committed generation
 * (no-op when already current). Render does this implicitly; tests and
 * hit-test callers may want it explicitly. */
struct yetty_ycore_void_result yetty_yscene_derive(struct yetty_yclass_object *obj);
/* Number of derived paint leaves (after derive). */
struct yetty_ycore_uint32_result yetty_yscene_leaf_count(struct yetty_yclass_object *obj);
/* Hit-test a SCREEN point against the derived scene: reverse paint
 * order over world AABBs + effective clips. Returns the owning node's
 * external id; 0 = no leaf hit (the document root / background). */
struct yetty_ycore_uint64_result yetty_yscene_hit_test(struct yetty_yclass_object *obj,
                                                       float screen_x, float screen_y);
/* Render-plan snapshot for headless tests: derive, build staging
 * against the CURRENT rect/content extent (staging is pure CPU — no
 * binder needed), and dump the exact data destined for GPU upload:
 * staged count, extent, per-prim word summaries, cell occupancy, and
 * the view mapping. Caller frees the string. */
struct yetty_ycore_char_ptr_result yetty_yscene_render_plan(struct yetty_yclass_object *obj);
/* View scale (HiDPI / zoom) — derive/view state, never baked into wire
 * coordinates. */
struct yetty_ycore_void_result yetty_yscene_set_view_scale(struct yetty_yclass_object *obj,
                                                           float view_scale);

#ifdef __cplusplus
}
#endif

#endif
