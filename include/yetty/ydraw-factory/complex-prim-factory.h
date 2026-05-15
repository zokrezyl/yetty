// YDraw Complex Primitive Factory - Abstract Factory Pattern (GPU side)
//
// This header bundles the *server-side* runtime that turns wire-format
// complex-prim bytes (defined in yetty/ydraw-core/complex-prim-types.h)
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
#include <yetty/ydraw-core/complex-prim-types.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Forward declarations
//=============================================================================

struct yetty_ydraw_core_concrete_factory;
struct yetty_ydraw_core_figure_instance;
struct yetty_ydraw_core_target;
struct yetty_ydraw_core_gpu_allocator;

//=============================================================================
// Instance - per primitive occurrence, stored in grid
//=============================================================================

struct yetty_ydraw_core_figure_instance {
    uint32_t type;
    struct yetty_ydraw_core_concrete_factory *factory; // back-pointer
    uint8_t *buffer_data;
    size_t buffer_size;
    struct yetty_ycore_rectangle bounds;
    uint32_t rolling_row;
    void *instance_data; // type-specific, managed by concrete factory

    /* Per-instance GPU resources. The factory owns the shared
     * yetty_yrender_pipeline (compiled once for the type); each instance
     * owns its own resource_set (per-instance uniform/buffer values) and
     * binder (its own GPU uniform_buffer, storage_buffer, bind_group).
     * Both are heap-allocated and owned by the instance — destroyed in
     * yetty_ydraw_core_figure_instance_destroy. May be NULL during
     * partial initialisation. */
    struct yetty_ydraw_core_gpu_resource_set *resource_set;
    struct yetty_yrender_gpu_resource_binder *binder;

    // Render to target at x,y (canvas provides x,y for scrolling)
    struct yetty_ycore_void_result (*render)(struct yetty_ydraw_core_figure_instance *self,
                                             struct yetty_ydraw_core_target *target, float x,
                                             float y);
};

YETTY_YRESULT_DECLARE(yetty_ydraw_core_figure_instance_ptr,
                      struct yetty_ydraw_core_figure_instance *);

//=============================================================================
// Concrete factory interface - one per type (yplot, image, video, etc.)
// Owns shared RS and pre-compiled pipeline, creates instances
//=============================================================================

struct yetty_ydraw_core_concrete_factory {
    uint32_t type_id;

    // Compile pipeline (called once during registration)
    struct yetty_ycore_void_result (*compile_pipeline)(
        struct yetty_ydraw_core_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
        WGPUTextureFormat target_format, struct yetty_ydraw_core_gpu_allocator *allocator);

    // Get pre-compiled pipeline
    WGPURenderPipeline (*get_pipeline)(struct yetty_ydraw_core_concrete_factory *self);

    // Create instance from buffer data
    struct yetty_ydraw_core_figure_instance_ptr_result (*create_instance)(
        struct yetty_ydraw_core_concrete_factory *self, const void *buffer_data, size_t size,
        uint32_t rolling_row);

    // Destroy instance
    void (*destroy_instance)(struct yetty_ydraw_core_concrete_factory *self,
                             struct yetty_ydraw_core_figure_instance *instance);

    // Get shared RS (for buffer data access)
    struct yetty_ydraw_core_gpu_resource_set *(*get_shared_rs)(
        struct yetty_ydraw_core_concrete_factory *self);

    // Push visual (shader-level) zoom state into this type's shared uniforms.
    // Optional — may be NULL if the type doesn't care about visual zoom.
    // Applies to all instances the factory has produced (they share the RS).
    struct yetty_ycore_void_result (*set_visual_zoom)(
        struct yetty_ydraw_core_concrete_factory *self, float scale, float offset_x,
        float offset_y);

    // Push "intrusive" cell-size zoom. Semantically SEPARATE from set_visual_zoom
    // — cell_zoom tracks the cumulative ratio between the current and baseline
    // cell size, visual_zoom tracks Ctrl+Scroll mouse-anchored zoom. The shader
    // applies them as two independent transforms so they can be reset without
    // interfering with each other.
    struct yetty_ycore_void_result (*set_cell_zoom)(struct yetty_ydraw_core_concrete_factory *self,
                                                    float scale, float offset_x, float offset_y);
};

//=============================================================================
// Abstract factory - registry of concrete factories
//=============================================================================

struct yetty_ydraw_core_figure_factory;

YETTY_YRESULT_DECLARE(yetty_ydraw_core_figure_factory_ptr,
                      struct yetty_ydraw_core_figure_factory *);

// Create (after device/queue available) / destroy
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_core_figure_factory_ptr_result
yetty_ydraw_core_figure_factory_create(WGPUDevice device, WGPUQueue queue,
                                              WGPUTextureFormat target_format,
                                              struct yetty_ydraw_core_gpu_allocator *allocator);

void yetty_ydraw_core_figure_factory_destroy(
    struct yetty_ydraw_core_figure_factory *factory YETTY_ANNOT_CALLEE_OWNED);

// Register concrete factory
struct yetty_ycore_void_result yetty_ydraw_core_figure_factory_register(
    struct yetty_ydraw_core_figure_factory *factory,
    struct yetty_ydraw_core_concrete_factory *concrete);

// Create instance (reads type from buffer_data, dispatches to concrete factory)
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_core_figure_instance_ptr_result
yetty_ydraw_core_figure_factory_create_instance(
    struct yetty_ydraw_core_figure_factory *factory,
    const void *buffer_data YETTY_ANNOT_ARRAY(size), size_t size, uint32_t rolling_row);

// Destroy instance (uses instance->factory back-pointer)
void yetty_ydraw_core_figure_instance_destroy(
    struct yetty_ydraw_core_figure_instance *instance YETTY_ANNOT_CALLEE_OWNED);

// Fan out visual-zoom state to every registered concrete factory (yplot,
// yimage, ...). Safe to call with no registrations. Concrete factories that
// don't implement set_visual_zoom are silently skipped.
void yetty_ydraw_core_figure_factory_set_visual_zoom(
    struct yetty_ydraw_core_figure_factory *factory, float scale, float offset_x,
    float offset_y);

// Fan out "intrusive" cell-zoom state the same way (separate uniforms,
// separate semantics — see set_cell_zoom in the concrete factory ops).
void yetty_ydraw_core_figure_factory_set_cell_zoom(
    struct yetty_ydraw_core_figure_factory *factory, float scale, float offset_x,
    float offset_y);

#ifdef __cplusplus
}
#endif
