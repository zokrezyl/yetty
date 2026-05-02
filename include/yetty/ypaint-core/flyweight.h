// YPaint Flyweight - primitive handler registry (instance-based)
// Decoupled from buffer, usable by buffer and canvas
//
// Two-level ops structure:
//   Base ops (all primitives - SDF and complex): size, aabb
//   Extended ops (SDF only): destroy, get_gpu_resource_set
//
// Complex primitives use the factory pattern instead of extended ops.
// See complex-prim-types.h for complex prim handling.
#pragma once

#include <stdint.h>
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

struct yetty_ypaint_prim_base_ops {
    // Size in bytes (for buffer iteration)
    struct yetty_ycore_size_result (*size)(const uint32_t *prim);

    // Bounding box (for spatial grid)
    struct rectangle_result (*aabb)(const uint32_t *prim);
};

//=============================================================================
// Extended ops - for SDF primitives only (inherits base)
// Complex primitives use factory pattern instead
//=============================================================================

struct yetty_ypaint_prim_ops {
    // Base ops (size, aabb) - MUST be first for C inheritance
    struct yetty_ypaint_prim_base_ops base;

    // Cleanup cached data (optional, may be NULL for simple prims)
    void (*destroy)(void *cache);

    // Get GPU resources for rendering (optional, NULL for simple SDF prims)
    // cache_ptr: pointer to cache storage (caller provides, callee allocates)
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(const uint32_t *prim,
                                                                         void **cache_ptr);
};

// Flyweight - wraps pointer to primitive data + base ops
// Works for ALL primitives (SDF and complex)
struct yetty_ypaint_prim_flyweight {
    const uint32_t *data;                         // type at data[0]
    const struct yetty_ypaint_prim_base_ops *ops; // base ops (size, aabb)
};

YETTY_YRESULT_DECLARE(yetty_ypaint_prim_base_ops_ptr, const struct yetty_ypaint_prim_base_ops *);
YETTY_YRESULT_DECLARE(yetty_ypaint_prim_flyweight_ptr, struct yetty_ypaint_prim_flyweight *);

// Handler function - takes prim_type, returns base ops pointer or error
typedef struct yetty_ypaint_prim_base_ops_ptr_result (*yetty_ypaint_prim_handler_fn)(
    uint32_t prim_type);

// Flyweight registry instance (opaque)
struct yetty_ypaint_flyweight_registry;

YETTY_YRESULT_DECLARE(yetty_ypaint_flyweight_registry_ptr,
                      struct yetty_ypaint_flyweight_registry *);

// Create/destroy registry instance
struct yetty_ypaint_flyweight_registry_ptr_result yetty_ypaint_flyweight_registry_create(void);

void yetty_ypaint_flyweight_registry_destroy(struct yetty_ypaint_flyweight_registry *reg);

// Set default handler (SDF) - called first, fast path
void yetty_ypaint_flyweight_registry_set_default(struct yetty_ypaint_flyweight_registry *reg,
                                                 yetty_ypaint_prim_handler_fn handler);

// Register additional handler for type range [type_min, type_max]
struct yetty_ycore_void_result yetty_ypaint_flyweight_registry_add(
    struct yetty_ypaint_flyweight_registry *reg, uint32_t type_min, uint32_t type_max,
    yetty_ypaint_prim_handler_fn handler);

// Get flyweight for primitive (tries default first, then by type range)
struct yetty_ypaint_prim_flyweight_ptr_result yetty_ypaint_flyweight_registry_get(
    const struct yetty_ypaint_flyweight_registry *reg, uint32_t prim_type,
    const uint32_t *prim_data);

#ifdef __cplusplus
}
#endif
