// YPaint Complex Primitive Types - Abstract Factory Pattern
//
// Architecture:
//   Abstract Factory = registry mapping type_id -> concrete factory
//   Concrete Factory = per-type, owns shared RS (compiled pipeline), creates instances
//   Instance = per-primitive, holds buffer data copy, render(render_target, x, y)
//
// See docs/ypaint.md for full documentation.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Complex primitive type IDs (0x80000000+ to avoid collision with SDF 0-255)
//=============================================================================

/* Tier ranges for ypaint primitive types:
 *   [0x00000000, 0x000000FF]   Simple SDF (fixed-size, generated)
 *   [0x40000000, 0x7FFFFFFF]   Flyweight (variable-size, no GPU pipeline)
 *                                FONT       — yetty/ypaint-core/font-prim.h
 *                                TEXT_SPAN  — yetty/ypaint-core/text-span-prim.h
 *   [0x80000000, 0xFFFFFFFF]   Complex (factory + per-instance GPU resources)
 *                                each concrete factory owns its own type id
 *                                (e.g. YETTY_YPLOT_TYPE_ID in yplot-gen.h).
 */
#define YETTY_YPAINT_COMPLEX_TYPE_BASE 0x80000000u

//=============================================================================
// Complex primitive header (FAM wire format)
//=============================================================================

struct yetty_ypaint_complex_prim {
    uint32_t type;
    uint32_t payload_size;
    uint8_t data[];
};

// Check if type uses FAM format
bool yetty_ypaint_is_complex_type(uint32_t type);

// Get total size (reads FAM header)
size_t yetty_ypaint_complex_prim_size(const void *data);

// Get AABB (reads bounds from standard offset 0-15 in payload)
struct rectangle_result yetty_ypaint_complex_prim_aabb(const void *data);

//=============================================================================
// Base ops for complex primitives (for flyweight registry)
// Returns base ops pointer for buffer iteration
//=============================================================================

#include <yetty/ypaint-core/flyweight.h>

// Handler for complex prim types (>= 0x80000000)
struct yetty_ypaint_prim_base_ops_ptr_result yetty_ypaint_complex_prim_handler(uint32_t prim_type);

//=============================================================================
// Forward declarations
//=============================================================================

struct yetty_ypaint_core_concrete_factory;
struct yetty_ypaint_complex_prim_instance;
struct yetty_yrender_target;
struct yetty_yrender_gpu_allocator;

//=============================================================================
// Instance - per primitive occurrence, stored in grid
//=============================================================================

struct yetty_ypaint_complex_prim_instance {
    uint32_t type;
    struct yetty_ypaint_core_concrete_factory *factory; // back-pointer
    uint8_t *buffer_data;
    size_t buffer_size;
    struct rectangle bounds;
    uint32_t rolling_row;
    void *instance_data; // type-specific, managed by concrete factory

    /* Per-instance GPU resources. The factory owns the shared
     * yetty_yrender_pipeline (compiled once for the type); each instance
     * owns its own resource_set (per-instance uniform/buffer values) and
     * binder (its own GPU uniform_buffer, storage_buffer, bind_group).
     * Both are heap-allocated and owned by the instance — destroyed in
     * yetty_ypaint_complex_prim_instance_destroy. May be NULL during
     * partial initialisation. */
    struct yetty_yrender_gpu_resource_set *resource_set;
    struct yetty_yrender_gpu_resource_binder *binder;

    // Render to target at x,y (canvas provides x,y for scrolling)
    struct yetty_ycore_void_result (*render)(struct yetty_ypaint_complex_prim_instance *self,
                                             struct yetty_yrender_target *target, float x, float y);
};

YETTY_YRESULT_DECLARE(yetty_ypaint_complex_prim_instance_ptr,
                      struct yetty_ypaint_complex_prim_instance *);

//=============================================================================
// Concrete factory interface - one per type (yplot, image, video, etc.)
// Owns shared RS and pre-compiled pipeline, creates instances
//=============================================================================

#include <webgpu/webgpu.h>

struct yetty_ypaint_core_concrete_factory {
    uint32_t type_id;

    // Compile pipeline (called once during registration)
    struct yetty_ycore_void_result (*compile_pipeline)(
        struct yetty_ypaint_core_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
        WGPUTextureFormat target_format, struct yetty_yrender_gpu_allocator *allocator);

    // Get pre-compiled pipeline
    WGPURenderPipeline (*get_pipeline)(struct yetty_ypaint_core_concrete_factory *self);

    // Create instance from buffer data
    struct yetty_ypaint_complex_prim_instance_ptr_result (*create_instance)(
        struct yetty_ypaint_core_concrete_factory *self, const void *buffer_data, size_t size,
        uint32_t rolling_row);

    // Destroy instance
    void (*destroy_instance)(struct yetty_ypaint_core_concrete_factory *self,
                             struct yetty_ypaint_complex_prim_instance *instance);

    // Get shared RS (for buffer data access)
    struct yetty_yrender_gpu_resource_set *(*get_shared_rs)(
        struct yetty_ypaint_core_concrete_factory *self);

    // Push visual (shader-level) zoom state into this type's shared uniforms.
    // Optional — may be NULL if the type doesn't care about visual zoom.
    // Applies to all instances the factory has produced (they share the RS).
    struct yetty_ycore_void_result (*set_visual_zoom)(struct yetty_ypaint_core_concrete_factory *self,
                                                      float scale, float offset_x, float offset_y);

    // Push "intrusive" cell-size zoom. Semantically SEPARATE from set_visual_zoom
    // — cell_zoom tracks the cumulative ratio between the current and baseline
    // cell size, visual_zoom tracks Ctrl+Scroll mouse-anchored zoom. The shader
    // applies them as two independent transforms so they can be reset without
    // interfering with each other.
    struct yetty_ycore_void_result (*set_cell_zoom)(struct yetty_ypaint_core_concrete_factory *self,
                                                    float scale, float offset_x, float offset_y);
};

//=============================================================================
// Abstract factory - registry of concrete factories
//=============================================================================

struct yetty_ypaint_complex_prim_factory;

YETTY_YRESULT_DECLARE(yetty_ypaint_complex_prim_factory_ptr,
                      struct yetty_ypaint_complex_prim_factory *);

// Create (after device/queue available) / destroy
struct yetty_ypaint_complex_prim_factory_ptr_result yetty_ypaint_complex_prim_factory_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
    struct yetty_yrender_gpu_allocator *allocator);

void yetty_ypaint_complex_prim_factory_destroy(struct yetty_ypaint_complex_prim_factory *factory);

// Register concrete factory
struct yetty_ycore_void_result yetty_ypaint_complex_prim_factory_register(
    struct yetty_ypaint_complex_prim_factory *factory,
    struct yetty_ypaint_core_concrete_factory *concrete);

// Get concrete factory by type
struct yetty_ypaint_core_concrete_factory *yetty_ypaint_complex_prim_factory_get(
    struct yetty_ypaint_complex_prim_factory *factory, uint32_t type_id);

// Create instance (reads type from buffer_data, dispatches to concrete factory)
struct yetty_ypaint_complex_prim_instance_ptr_result
yetty_ypaint_complex_prim_factory_create_instance(struct yetty_ypaint_complex_prim_factory *factory,
                                                  const void *buffer_data, size_t size,
                                                  uint32_t rolling_row);

// Destroy instance (uses instance->factory back-pointer)
void yetty_ypaint_complex_prim_instance_destroy(
    struct yetty_ypaint_complex_prim_instance *instance);

// Fan out visual-zoom state to every registered concrete factory (yplot,
// yimage, ...). Safe to call with no registrations. Concrete factories that
// don't implement set_visual_zoom are silently skipped.
void yetty_ypaint_complex_prim_factory_set_visual_zoom(
    struct yetty_ypaint_complex_prim_factory *factory, float scale, float offset_x, float offset_y);

// Fan out "intrusive" cell-zoom state the same way (separate uniforms,
// separate semantics — see set_cell_zoom in the concrete factory ops).
void yetty_ypaint_complex_prim_factory_set_cell_zoom(
    struct yetty_ypaint_complex_prim_factory *factory, float scale, float offset_x, float offset_y);

#ifdef __cplusplus
}
#endif
