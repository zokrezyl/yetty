// YSDF primitive handler for buffer iteration
#pragma once

#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ysdf/types.gen.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline struct yetty_ycore_size_result yetty_ysdf_prim_size(const uint32_t *prim)
{
    size_t size = yetty_ysdf_primitive_size(prim[0]);
    if (size == 0) {
        return YETTY_ERR(yetty_ycore_size, "unknown SDF type");
    }
    return YETTY_OK(yetty_ycore_size, size);
}

static inline struct rectangle_result yetty_ysdf_prim_aabb(const uint32_t *prim)
{
    uint32_t word_count = yetty_ysdf_word_count((enum yetty_ysdf_type)prim[0]);
    if (word_count == 0) {
        return YETTY_ERR(rectangle, "unknown SDF type");
    }
    return yetty_ysdf_compute_aabb((const float *)prim, word_count);
}

// Base ops for SDF primitives (size, aabb only)
static const struct yetty_ydraw_core_prim_base_ops yetty_ysdf_prim_base_ops = {
    .size = yetty_ysdf_prim_size,
    .aabb = yetty_ysdf_prim_aabb,
};

// Extended ops for SDF primitives (includes base + destroy + get_gpu_resource_set)
static const struct yetty_ydraw_core_prim_ops yetty_ysdf_prim_ops = {
    .base =
        {
            .size = yetty_ysdf_prim_size,
            .aabb = yetty_ysdf_prim_aabb,
        },
    .destroy = NULL,              // SDF prims have no cached data
    .get_gpu_resource_set = NULL, // SDF prims rendered by main shader
};

// Handler returns base ops (for flyweight registry).
// SDF tier is [0x10000000, 0x1FFFFFFF] in the ydraw type-id space.
static inline struct yetty_ydraw_core_prim_base_ops_ptr_result yetty_ysdf_handler(
    uint32_t prim_type)
{
    if (prim_type >= 0x10000000u && prim_type <= 0x1FFFFFFFu &&
        yetty_ysdf_primitive_size(prim_type) > 0) {
        return YETTY_OK(yetty_ydraw_core_prim_base_ops_ptr, &yetty_ysdf_prim_base_ops);
    }
    return YETTY_ERR(yetty_ydraw_core_prim_base_ops_ptr, "not an SDF type");
}

#ifdef __cplusplus
}
#endif
