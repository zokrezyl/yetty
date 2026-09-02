// YSDF primitive handler for buffer iteration
#pragma once

#include <string.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/drawable-list-registry.h>
#include <yetty/ysdf/types.gen.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SDF wire records come in two shapes (see ydraw-list/cmds.h):
 *   [type][z_order][fill][stroke][stroke_width][geometry...]
 *   [type|HAS_ID][id][z_order][fill][stroke][stroke_width][geometry...]
 * Every accessor here masks the id flag off the type word and accounts
 * for the extra id word, so both shapes stride and decode exactly. */

/* Largest generated SDF record in words (yetty_ysdf_word_count max),
 * used to rebuild the id-less shape on the stack for decoding. */
#define YETTY_YSDF_MAX_RECORD_WORDS 16u

static inline struct yetty_ycore_size_result yetty_ysdf_drawable_size(const uint32_t *prim)
{
    uint32_t id_words = (prim[0] & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
    size_t size = yetty_ysdf_primitive_size(prim[0] & ~YETTY_YDRAW_HAS_ID_FLAG);
    if (size == 0) {
        return YETTY_ERR(yetty_ycore_size, "unknown SDF type");
    }
    return YETTY_OK(yetty_ycore_size, size + id_words * sizeof(uint32_t));
}

static inline struct rectangle_result yetty_ysdf_drawable_aabb(const uint32_t *prim)
{
    uint32_t masked_type = prim[0] & ~YETTY_YDRAW_HAS_ID_FLAG;
    uint32_t word_count = yetty_ysdf_word_count((enum yetty_ysdf_type)masked_type);
    if (word_count == 0) {
        return YETTY_ERR(rectangle, "unknown SDF type");
    }
    if (prim[0] & YETTY_YDRAW_HAS_ID_FLAG) {
        /* compute_aabb expects the id-less shape — rebuild it. */
        uint32_t unshifted[YETTY_YSDF_MAX_RECORD_WORDS];
        if (word_count > YETTY_YSDF_MAX_RECORD_WORDS) {
            return YETTY_ERR(rectangle, "SDF record exceeds max words");
        }
        unshifted[0] = masked_type;
        memcpy(&unshifted[1], &prim[2], (size_t)(word_count - 1u) * sizeof(uint32_t));
        return yetty_ysdf_compute_aabb((const float *)unshifted, word_count);
    }
    return yetty_ysdf_compute_aabb((const float *)prim, word_count);
}

// Paint z of an SDF primitive. Wire layout puts the z word right after
// the type word and the optional id word:
//   [type][id?][z_order][fill][stroke][stroke_width][geometry...]
// The 32 wire bits reinterpret as signed so producers can stack below
// the default plane (z < 0) as well as above it.
static inline int32_t yetty_ysdf_drawable_paint_z(const uint32_t *prim)
{
    uint32_t shift = (prim[0] & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
    int32_t paint_z;
    memcpy(&paint_z, &prim[1u + shift], sizeof(paint_z));
    return paint_z;
}

// Base ops for SDF primitives (size, aabb, paint_z)
static const struct yetty_ydraw_drawable_list_entry_ops yetty_ysdf_drawable_base_ops = {
    .size = yetty_ysdf_drawable_size,
    .aabb = yetty_ysdf_drawable_aabb,
    .paint_z = yetty_ysdf_drawable_paint_z,
};

// Extended ops for SDF primitives (includes base + destroy + get_gpu_resource_set)
static const struct yetty_ydraw_primitive_ops yetty_ysdf_drawable_ops = {
    .base =
        {
            .size = yetty_ysdf_drawable_size,
            .aabb = yetty_ysdf_drawable_aabb,
            .paint_z = yetty_ysdf_drawable_paint_z,
        },
    .destroy = NULL,              // SDF prims have no cached data
    .get_gpu_resource_set = NULL, // SDF prims rendered by main shader
};

// Handler returns base ops (for drawable-list registry).
// yetty_ysdf_primitive_size returns 0 for any type id not registered in
// the SDF YAML, so the size lookup is itself the SDF discriminator —
// no hardcoded range gate. The id flag masks off first so id-carrying
// SDF prims resolve to these ops (and their exact stride) instead of
// falling through to the complex range handler with a garbage stride.
static inline struct yetty_ydraw_drawable_list_entry_ops_ptr_result yetty_ysdf_handler(
    uint32_t drawable_type)
{
    if (yetty_ysdf_primitive_size(drawable_type & ~YETTY_YDRAW_HAS_ID_FLAG) > 0) {
        return YETTY_OK(yetty_ydraw_drawable_list_entry_ops_ptr, &yetty_ysdf_drawable_base_ops);
    }
    return YETTY_ERR(yetty_ydraw_drawable_list_entry_ops_ptr, "not an SDF type");
}

#ifdef __cplusplus
}
#endif
