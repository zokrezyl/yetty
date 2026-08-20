// YDraw Complex Primitive Factory - Abstract Factory Pattern (GPU side)
//
// This header bundles the *server-side* runtime that turns wire-format
// complex bytes (defined in yetty/ydraw-list/complex.h)
// into renderable GPU objects.
//
// Architecture:
//   Abstract Factory = registry mapping type_id -> concrete factory
//   Concrete Factory = per-type, owns shared RS (compiled pipeline), creates instances
//   Instance         = per-primitive, holds buffer data copy, render(target, x, y)
//
// Anything in this header pulls in <webgpu/webgpu.h> and the GPU resource
// types — keep client-only code (DCS emit, YAML, wire serialize) on the
// ydraw-list side.
//
// See docs/ydraw.md for full documentation.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/complex.h>
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
struct yetty_ydraw_complex;
struct yetty_ydraw_target;
struct yetty_ydraw_gpu_allocator;

/* Bounds how many complexes may hold live GPU resources at once, decoupling
 * GPU residency from how deep a figure sits in scrollback. See the residency
 * block in yetty_ydraw_complex and gpu-residency.c. */
struct yetty_ydraw_gpu_residency;

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

struct yetty_ydraw_complex_ops {
    /* Destroy: tear down GPU resources + free the instance. Called
     * via yetty_ydraw_complex_destroy(). Must release everything the
     * figure owns; the caller frees nothing after. */
    void (*destroy)(struct yetty_ydraw_complex *self);

    /* Apply a CMD_UPDATE addressed to this instance. `target_field`
     * is the first u32 of the wire payload; `body`/`body_size` is
     * the rest. NULL on figures that don't accept incremental
     * updates — the scene-canvas drops the wire record silently in
     * that case. */
    struct yetty_ycore_void_result (*update)(struct yetty_ydraw_complex *self,
                                             uint32_t target_field, const void *body,
                                             size_t body_size);
};

//=============================================================================
// Instance - per primitive occurrence, stored in grid
//=============================================================================

struct yetty_ydraw_complex {
    /* Per-instance vtable. Shared across all instances of one
     * concrete type — pointer to a static const ops table. Method
     * dispatch (update / destroy) goes through here; the factory is
     * out of the runtime path. NULL until create_instance wires it. */
    const struct yetty_ydraw_complex_ops *ops;

    uint32_t type;
    struct yetty_ydraw_concrete_factory *factory; // back-pointer
    uint8_t *buffer_data;
    size_t buffer_size;
    struct yetty_ycore_rectangle bounds;
    uint32_t rolling_row;
    /* HiDPI logical->physical scale for THIS render. `bounds` is in the
     * coordinate space the host laid the figure out in: for the absolute
     * (ygui chrome) compositor that is LOGICAL pixels, so the figure must
     * paint at bounds*content_scale to fill its physical footprint; for the
     * local (terminal scrolling-layer) compositor bounds are already
     * framebuffer pixels, so this stays 1.0. The hosting grid sets it before
     * each render() call; producers read it (treating <=0 as 1.0). */
    float content_scale;
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
     * yetty_ydraw_complex, listener)`. Figures that don't
     * subscribe simply leave the handler NULL — registration is opt-in. */
    struct yetty_yevent_event_listener listener;

    /* Per-instance GPU resources. The factory owns the shared
     * yetty_yrender_pipeline (compiled once for the type); each instance
     * owns its own resource_set (per-instance uniform/buffer values) and
     * binder (its own GPU uniform_buffer, storage_buffer, bind_group).
     * Both are heap-allocated and owned by the instance — destroyed in
     * yetty_ydraw_complex_destroy. May be NULL during
     * partial initialisation. */
    struct yetty_yrender_gpu_resource_set *resource_set;
    struct yetty_yrender_gpu_resource_binder *binder;

    /* GPU residency bookkeeping — a figure's binder holds real allocator slots
     * (storage + uniform buffers, atlas textures), and a scrollback of 10k
     * lines can hold far more figures than the allocator's slot budget. The
     * residency manager (owned by the abstract factory) keeps only the most
     * recently rendered figures GPU-resident: an intrusive LRU list threaded
     * through res_prev/res_next, with `residency` back-pointing at the manager
     * and `resident` marking membership. When a figure falls off the LRU tail
     * its binder->release_gpu() hands the slots back; the next render()
     * reacquires them via binder->finalize(). All fields zero on a fresh
     * instance (every figure type callocs) → "not yet resident". */
    struct yetty_ydraw_gpu_residency *residency;
    struct yetty_ydraw_complex *res_prev;
    struct yetty_ydraw_complex *res_next;
    int resident;

    /* Stream-update routing — scrollback figures that accept CMD_UPDATE
     * records from LATER envelopes (live plots, chunked video) register
     * in the host's stream registry under a small id; the registry entry
     * is cleared centrally in yetty_ydraw_complex_destroy via this
     * back-pointer (same lifetime pattern as `residency` above). Zero on
     * a fresh instance → not registered. */
    uint32_t stream_id;
    struct yetty_ydraw_stream_registry *stream_registry;

    // Render to target at x,y (canvas provides x,y for scrolling)
    struct yetty_ycore_void_result (*render)(struct yetty_ydraw_complex *self,
                                             struct yetty_ydraw_target *target, float x, float y);
};

YETTY_YRESULT_DECLARE(yetty_ydraw_complex_ptr, struct yetty_ydraw_complex *);

//=============================================================================
// Stream-update registry
//=============================================================================

/* Routes CMD_UPDATE records arriving in later envelopes to live complex
 * instances. Ids are small per-host handles (the terminal scrollback path
 * assigns each envelope's complexes ordinals 1, 2, ...); registering an
 * id again replaces the previous target (most-recent-wins), and instances
 * unregister themselves on destroy. Fixed capacity: streaming producers
 * address at most a handful of concurrently-live figures. */
#define YETTY_YDRAW_STREAM_TARGETS_MAX 16u

struct yetty_ydraw_stream_registry {
    struct yetty_ydraw_complex *targets[YETTY_YDRAW_STREAM_TARGETS_MAX];
};

/* Register `instance` under `stream_id` (1-based; ids outside
 * 1..YETTY_YDRAW_STREAM_TARGETS_MAX are ignored). */
void yetty_ydraw_stream_registry_register(struct yetty_ydraw_stream_registry *registry,
                                          uint32_t stream_id, struct yetty_ydraw_complex *instance);

/* The live instance registered under `stream_id`, or NULL. */
struct yetty_ydraw_complex *yetty_ydraw_stream_registry_find(
    struct yetty_ydraw_stream_registry *registry, uint32_t stream_id);

//=============================================================================
// Concrete factory interface - one per type (yplot, image, video, etc.)
// Owns shared RS and pre-compiled pipeline, creates instances
//=============================================================================

struct yetty_ydraw_concrete_factory {
    uint32_t type_id;

    /* Destroy the concrete factory itself (shared pipeline + the factory
     * allocation). Registration transfers ownership of the concrete
     * factory to the complex factory, which invokes this from
     * yetty_ydraw_complex_factory_destroy. Generated factories wire
     * this to their yetty_<name>_factory_destroy. */
    void (*destroy)(struct yetty_ydraw_concrete_factory *self);

    /* Event loop available to the factory's instances. Set by the
     * abstract factory at registration time (mirror of what's stashed
     * on the complex_factory). Concrete factories that need to register
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

    /* Compile the shared pipeline. Deferred: the complex factory calls this
     * once, right before the FIRST create_instance of this type — never at
     * registration — so startup doesn't pay for figure shaders that are never
     * used. Implementations may rely on it having run before create_instance /
     * get_pipeline / get_shared_rs, but must not expect it at register time. */
    struct yetty_ycore_void_result (*compile_pipeline)(struct yetty_ydraw_concrete_factory *self,
                                                       WGPUDevice device, WGPUQueue queue,
                                                       WGPUTextureFormat target_format,
                                                       struct yetty_ydraw_gpu_allocator *allocator);

    // Get the shared pipeline (compiled on first instance creation)
    WGPURenderPipeline (*get_pipeline)(struct yetty_ydraw_concrete_factory *self);

    // Create instance from buffer data
    struct yetty_ydraw_complex_ptr_result (*create_instance)(
        struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
        uint32_t rolling_row);

    // Destroy instance
    void (*destroy_instance)(struct yetty_ydraw_concrete_factory *self,
                             struct yetty_ydraw_complex *instance);

    /* Apply a CMD_UPDATE payload to an existing instance. Optional — leave
     * NULL on factories that don't accept incremental updates; the canvas
     * silently drops the wire record in that case. `payload` and `size`
     * come straight from the wire; the prim parses them per its own
     * schema (yplot: [buffer_index u32][sample_offset u32][count u32]
     * [samples × f32]). The instance pointer was previously returned by
     * create_instance — the canvas resolves the wire id to it. */
    struct yetty_ycore_void_result (*update_instance)(struct yetty_ydraw_concrete_factory *self,
                                                      struct yetty_ydraw_complex *instance,
                                                      const void *payload, size_t size);

    // Get shared RS (for buffer data access)
    struct yetty_yrender_gpu_resource_set *(*get_shared_rs)(
        struct yetty_ydraw_concrete_factory *self);

    // Push visual (shader-level) zoom state into this type's shared uniforms.
    // Optional — may be NULL if the type doesn't care about visual zoom.
    // Applies to all instances the factory has produced (they share the RS).
    struct yetty_ycore_void_result (*set_visual_zoom)(struct yetty_ydraw_concrete_factory *self,
                                                      float scale, float offset_x, float offset_y);

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

struct yetty_ydraw_complex_factory;

YETTY_YRESULT_DECLARE(yetty_ydraw_complex_factory_ptr, struct yetty_ydraw_complex_factory *);

// Create (after device/queue available) / destroy. `event_loop` is
// stashed on the registry and propagated to every concrete factory at
// register time so instances can subscribe to timers / mouse / etc.
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_complex_factory_ptr_result yetty_ydraw_complex_factory_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
    struct yetty_ydraw_gpu_allocator *allocator, struct yetty_yevent_event_loop *event_loop);

void yetty_ydraw_complex_factory_destroy(
    struct yetty_ydraw_complex_factory *factory YETTY_ANNOT_CALLEE_OWNED);

// Register concrete factory. On success ownership of `concrete` transfers
// to the complex factory (its `destroy` op runs at complex factory
// destroy). On failure the caller still owns `concrete` and must destroy it.
struct yetty_ycore_void_result yetty_ydraw_complex_factory_register(
    struct yetty_ydraw_complex_factory *factory, struct yetty_ydraw_concrete_factory *concrete);

// Create instance (reads type from buffer_data, dispatches to concrete factory)
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_complex_ptr_result yetty_ydraw_complex_factory_create_instance(
    struct yetty_ydraw_complex_factory *factory, const void *buffer_data YETTY_ANNOT_ARRAY(size),
    size_t size, uint32_t rolling_row);

// Destroy instance (uses instance->factory back-pointer)
void yetty_ydraw_complex_destroy(struct yetty_ydraw_complex *instance YETTY_ANNOT_CALLEE_OWNED);

// Render one instance into `target` at (x, y). The host grid supplies the
// position for its scrolling/anchored placement. Thin wrapper over the
// per-instance render op; a figure type with no render op doesn't paint.
struct yetty_ycore_void_result yetty_ydraw_complex_render(struct yetty_ydraw_complex *instance,
                                                          struct yetty_ydraw_target *target,
                                                          float x, float y);

// Laid-out pixel height of the figure (bounds.max.y - bounds.min.y), in the
// host's layout space. The hosting grid uses it to keep a multi-row figure
// drawn while any part of its vertical extent still intersects the viewport
// (e.g. its top line has scrolled off but lower rows are still visible).
// Returns 0 for a NULL instance or one with no/empty bounds.
float yetty_ydraw_complex_pixel_height(const struct yetty_ydraw_complex *instance);

/* Bottom edge of the figure's wire bounds (envelope-local px) — the
 * culling extent for records positioned inside a multi-figure block. */
float yetty_ydraw_complex_pixel_bottom(const struct yetty_ydraw_complex *instance);

// Set the figure's content scale (paint size relative to its laid-out bounds).
// The hosting grid uses it to magnify a figure under visual (Ctrl+wheel) zoom in
// lockstep with the zoomed text, so the figure keeps filling its reserved rows
// instead of shrinking away from them. <=0 is treated as 1.0.
void yetty_ydraw_complex_set_content_scale(struct yetty_ydraw_complex *instance, float scale);

// Fan out visual-zoom state to every registered concrete factory (yplot,
// yimage, ...). Safe to call with no registrations. Concrete factories that
// don't implement set_visual_zoom are silently skipped.
void yetty_ydraw_complex_factory_set_visual_zoom(struct yetty_ydraw_complex_factory *factory,
                                                 float scale, float offset_x, float offset_y);

// Fan out "intrusive" cell-zoom state the same way (separate uniforms,
// separate semantics — see set_cell_zoom in the concrete factory ops).
void yetty_ydraw_complex_factory_set_cell_zoom(struct yetty_ydraw_complex_factory *factory,
                                               float scale, float offset_x, float offset_y);

#ifdef __cplusplus
}
#endif
