// YDraw Drawable-list entry - primitive handler registry (instance-based)
// Decoupled from buffer, usable by buffer and canvas
//
// Two-level ops structure:
//   Base ops (all primitives - SDF and complex): size, aabb
//   Extended ops (SDF only): destroy, get_gpu_resource_set
//
// Composites use the factory pattern instead of extended ops.
// See composite.h for complex prim handling.
#pragma once

#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare gpu_resource_set result (defined in yrender/gpu-resource-set.h)
struct yetty_yrender_gpu_resource_set;
struct yetty_yrender_gpu_resource_set_result;

//=============================================================================
// Base ops - for ALL primitives (SDF and complex)
// Used by buffer iteration to get size and aabb
//=============================================================================

struct yetty_ydraw_drawable_list_entry_ops {
    // Size in bytes (for buffer iteration)
    struct yetty_ycore_size_result (*size)(const uint32_t *prim);

    // Bounding box (for spatial grid)
    struct rectangle_result (*aabb)(const uint32_t *prim);
};

//=============================================================================
// Extended ops - for SDF primitives only (inherits base)
// Composites use factory pattern instead
//=============================================================================

struct yetty_ydraw_primitive_ops {
    // Base ops (size, aabb) - MUST be first for C inheritance
    struct yetty_ydraw_drawable_list_entry_ops base;

    // Cleanup cached data (optional, may be NULL for simple prims)
    void (*destroy)(void *cache);

    // Get GPU resources for rendering (optional, NULL for simple SDF prims)
    // cache_ptr: pointer to cache storage (caller provides, callee allocates)
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(const uint32_t *prim,
                                                                         void **cache_ptr);
};

// Drawable-list entry - wraps pointer to primitive data + base ops
// Works for ALL primitives (SDF and complex)
struct yetty_ydraw_drawable_list_entry {
    const uint32_t *data;                            // type at data[0]
    const struct yetty_ydraw_drawable_list_entry_ops *ops; // base ops (size, aabb)
};

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_list_entry_ops_ptr,
                      const struct yetty_ydraw_drawable_list_entry_ops *);
YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_list_entry_ptr, struct yetty_ydraw_drawable_list_entry *);

// Drawable-list registry instance (opaque)
struct yetty_ydraw_drawable_list_registry;

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_list_registry_ptr, struct yetty_ydraw_drawable_list_registry *);

// Create/destroy registry instance
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_drawable_list_registry_ptr_result yetty_ydraw_drawable_list_registry_create(void);

void yetty_ydraw_drawable_list_registry_destroy(
    struct yetty_ydraw_drawable_list_registry *reg YETTY_ANNOT_CALLEE_OWNED);

// Set default handler (SDF) - called first, fast path
void yetty_ydraw_drawable_list_registry_set_default(
    struct yetty_ydraw_drawable_list_registry *reg,
    struct yetty_ydraw_drawable_list_entry_ops_ptr_result (*handler)(uint32_t));

// Register additional handler for type range [type_min, type_max]
struct yetty_ycore_void_result yetty_ydraw_drawable_list_registry_add(
    struct yetty_ydraw_drawable_list_registry *reg, uint32_t type_min, uint32_t type_max,
    struct yetty_ydraw_drawable_list_entry_ops_ptr_result (*handler)(uint32_t));

#ifdef __cplusplus
}
#endif
