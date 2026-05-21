// YDraw Complex Primitive Factory - Abstract Factory Pattern (GPU side)
//
// This header bundles the *server-side* runtime that turns wire-format
// complex-prim bytes (defined in yetty/ydraw-core/figure-types.h)
// into renderable GPU objects.
//
// Architecture:
//   Abstract Factory = registry mapping type_id -> concrete factory
//   Concrete Factory = per-type, owns shared RS (compiled pipeline), creates instances
//   Instance         = per-primitive, holds buffer data copy, render(target, x, y)
//
// Anything in this header pulls in <webgpu/webgpu.h> and the GPU resource
// types — keep client-only code (OSC emit, YAML, wire serialize) on the
// ydraw-core side.
//
// See docs/ydraw.md for full documentation.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Forward declarations
//=============================================================================

struct yetty_ydraw_concrete_factory;
struct yetty_ydraw_figure;
struct yetty_ydraw_target;
struct yetty_ydraw_gpu_allocator;

//=============================================================================
// Per-instance vtable
//
// Each concrete figure type (yplot / yvideo / yimage / ymesh) exposes a
// single static const ops table and points `figure->ops` at it during
// create_instance. Runtime method calls (update, destroy) go through
// `fi->ops->X(fi, ...)` — no factory in the call path. The factory
// keeps its role as a one-time registry: register pipeline, mint
// instances. After create, the factory back-pointer on the instance is
// only used for shared GPU state (pipeline, zoom uniforms), not for
// method dispatch.
//
// `update` payload shape:
//   `target_field` carries the schema-level slot id (which buffer /
//   uniform / texture region inside this figure). Codegen knows the
//   slot ids; on the wire the scene-canvas peels off the first u32
//   from the CMD_UPDATE payload and passes it as `target_field`,
//   leaving the rest of the bytes as `body`. The dispatcher inside
//   each figure's update method interprets `body` per slot semantics
//   (a buffer-slice body is `[u32 offset][u32 count][bytes]`, a
//   uniform body is the new scalar, etc.).
//=============================================================================

struct yetty_ydraw_figure_ops {
    /* Destroy: tear down GPU resources + free the instance. Called
     * via yetty_ydraw_figure_destroy(). Must release everything the
     * figure owns; the caller frees nothing after. */
    void (*destroy)(struct yetty_ydraw_figure *self);

    /* Apply a CMD_UPDATE addressed to this instance. `target_field`
     * is the first u32 of the wire payload; `body`/`body_size` is
     * the rest. NULL on figures that don't accept incremental
     * updates — the scene-canvas drops the wire record silently in
     * that case. */
    struct yetty_ycore_void_result (*update)(
        struct yetty_ydraw_figure *self,
        uint32_t target_field,
        const void *body, size_t body_size);
};

//=============================================================================
// Instance - per primitive occurrence, stored in grid
//=============================================================================

struct yetty_ydraw_figure {
    /* Per-instance vtable. Shared across all instances of one
     * concrete type — pointer to a static const ops table. Method
     * dispatch (update / destroy) goes through here; the factory is
     * out of the runtime path. NULL until create_instance wires it. */
    const struct yetty_ydraw_figure_ops *ops;

    uint32_t type;
    struct yetty_ydraw_concrete_factory *factory; // back-pointer
    uint8_t *buffer_data;
    size_t buffer_size;
    struct yetty_ycore_rectangle bounds;
    uint32_t rolling_row;
    void *instance_data; // type-specific, managed by concrete factory

    /* Per-instance dirty bit. The render loop renders this figure iff
     * `dirty || force`; cleared inside the layer's render path after
     * each render. Set by:
     *   - the canvas when a new envelope arrives and this is a fresh
     *     instance (so the first frame paints it),
     *   - the figure's own event listener (timer tick, decoded frame,
     *     mouse interaction, …) — see `listener` below.
     * Bypasses the layer-level dirty bit so per-figure animation
     * without text/SDF churn still gets the figure re-drawn. */
    int dirty;

    /* Event-listener interface — every figure_instance IS-A listener.
     * Concrete factories that want timer / mouse / keyboard ticks point
     * `listener.handler` at their own dispatcher and register with the
     * event loop (see `factory->event_loop`). The handler recovers the
     * instance pointer with `container_of(l, struct
     * yetty_ydraw_figure, listener)`. Figures that don't
     * subscribe simply leave the handler NULL — registration is opt-in. */
    struct yetty_yevent_event_listener listener;

    /* Per-instance GPU resources. The factory owns the shared
     * yetty_yrender_pipeline (compiled once for the type); each instance
     * owns its own resource_set (per-instance uniform/buffer values) and
     * binder (its own GPU uniform_buffer, storage_buffer, bind_group).
     * Both are heap-allocated and owned by the instance — destroyed in
     * yetty_ydraw_figure_destroy. May be NULL during
     * partial initialisation. */
    struct yetty_ydraw_gpu_resource_set *resource_set;
    struct yetty_yrender_gpu_resource_binder *binder;

    // Render to target at x,y (canvas provides x,y for scrolling)
    struct yetty_ycore_void_result (*render)(struct yetty_ydraw_figure *self,
                                             struct yetty_ydraw_target *target, float x,
                                             float y);
};

YETTY_YRESULT_DECLARE(yetty_ydraw_figure_ptr,
                      struct yetty_ydraw_figure *);

//=============================================================================
// Concrete factory interface - one per type (yplot, image, video, etc.)
// Owns shared RS and pre-compiled pipeline, creates instances
//=============================================================================

struct yetty_ydraw_concrete_factory {
    uint32_t type_id;

    /* Event loop available to the factory's instances. Set by the
     * abstract factory at registration time (mirror of what's stashed
     * on the figure_factory). Concrete factories that need to register
     * listeners (timers, mouse, …) use this; static-content factories
     * (yplot, yimage, ymesh) ignore it. */
    struct yetty_yevent_event_loop *event_loop;

    /* Free-form hook-managed state for the concrete factory. Used by
     * factories that need per-type (not per-instance) state — e.g. a
     * shared, refcounted animation timer that every instance subscribes
     * to. Layered above the generator's emitted factory struct so the
     * generator doesn't have to know about each prim's lifecycle data.
     * Allocator/free is entirely up to the hook impl. */
    void *hook_data;

    // Compile pipeline (called once during registration)
    struct yetty_ycore_void_result (*compile_pipeline)(
        struct yetty_ydraw_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
        WGPUTextureFormat target_format, struct yetty_ydraw_gpu_allocator *allocator);

    // Get pre-compiled pipeline
    WGPURenderPipeline (*get_pipeline)(struct yetty_ydraw_concrete_factory *self);

    // Create instance from buffer data
    struct yetty_ydraw_figure_ptr_result (*create_instance)(
        struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
        uint32_t rolling_row);

    // Destroy instance
    void (*destroy_instance)(struct yetty_ydraw_concrete_factory *self,
                             struct yetty_ydraw_figure *instance);

    /* Apply a CMD_UPDATE payload to an existing instance. Optional — leave
     * NULL on factories that don't accept incremental updates; the canvas
     * silently drops the wire record in that case. `payload` and `size`
     * come straight from the wire; the prim parses them per its own
     * schema (yplot: [buffer_index u32][sample_offset u32][count u32]
     * [samples × f32]). The instance pointer was previously returned by
     * create_instance — the canvas resolves the wire id to it. */
    struct yetty_ycore_void_result (*update_instance)(
        struct yetty_ydraw_concrete_factory *self,
        struct yetty_ydraw_figure *instance,
        const void *payload, size_t size);

    // Get shared RS (for buffer data access)
    struct yetty_ydraw_gpu_resource_set *(*get_shared_rs)(
        struct yetty_ydraw_concrete_factory *self);

    // Push visual (shader-level) zoom state into this type's shared uniforms.
    // Optional — may be NULL if the type doesn't care about visual zoom.
    // Applies to all instances the factory has produced (they share the RS).
    struct yetty_ycore_void_result (*set_visual_zoom)(
        struct yetty_ydraw_concrete_factory *self, float scale, float offset_x,
        float offset_y);

    // Push "intrusive" cell-size zoom. Semantically SEPARATE from set_visual_zoom
    // — cell_zoom tracks the cumulative ratio between the current and baseline
    // cell size, visual_zoom tracks Ctrl+Scroll mouse-anchored zoom. The shader
    // applies them as two independent transforms so they can be reset without
    // interfering with each other.
    struct yetty_ycore_void_result (*set_cell_zoom)(struct yetty_ydraw_concrete_factory *self,
                                                    float scale, float offset_x, float offset_y);
};

//=============================================================================
// Abstract factory - registry of concrete factories
//=============================================================================

struct yetty_ydraw_raw_figure_factory;

YETTY_YRESULT_DECLARE(yetty_ydraw_raw_figure_factory_ptr,
                      struct yetty_ydraw_raw_figure_factory *);

// Create (after device/queue available) / destroy. `event_loop` is
// stashed on the registry and propagated to every concrete factory at
// register time so instances can subscribe to timers / mouse / etc.
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_raw_figure_factory_ptr_result
yetty_ydraw_raw_figure_factory_create(WGPUDevice device, WGPUQueue queue,
                                              WGPUTextureFormat target_format,
                                              struct yetty_ydraw_gpu_allocator *allocator,
                                              struct yetty_yevent_event_loop *event_loop);

void yetty_ydraw_raw_figure_factory_destroy(
    struct yetty_ydraw_raw_figure_factory *factory YETTY_ANNOT_CALLEE_OWNED);

// Register concrete factory
struct yetty_ycore_void_result yetty_ydraw_raw_figure_factory_register(
    struct yetty_ydraw_raw_figure_factory *factory,
    struct yetty_ydraw_concrete_factory *concrete);

// Create instance (reads type from buffer_data, dispatches to concrete factory)
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_figure_ptr_result
yetty_ydraw_raw_figure_factory_create_instance(
    struct yetty_ydraw_raw_figure_factory *factory,
    const void *buffer_data YETTY_ANNOT_ARRAY(size), size_t size, uint32_t rolling_row);

// Destroy instance (uses instance->factory back-pointer)
void yetty_ydraw_figure_destroy(
    struct yetty_ydraw_figure *instance YETTY_ANNOT_CALLEE_OWNED);

// Fan out visual-zoom state to every registered concrete factory (yplot,
// yimage, ...). Safe to call with no registrations. Concrete factories that
// don't implement set_visual_zoom are silently skipped.
void yetty_ydraw_raw_figure_factory_set_visual_zoom(
    struct yetty_ydraw_raw_figure_factory *factory, float scale, float offset_x,
    float offset_y);

// Fan out "intrusive" cell-zoom state the same way (separate uniforms,
// separate semantics — see set_cell_zoom in the concrete factory ops).
void yetty_ydraw_raw_figure_factory_set_cell_zoom(
    struct yetty_ydraw_raw_figure_factory *factory, float scale, float offset_x,
    float offset_y);

#ifdef __cplusplus
}
#endif
