// YDraw Complex Primitive Types - Wire-format helpers (GPU-less)
//
// The GPU-side runtime (factory registry, instance lifecycle, zoom fan-out)
// lives in src/yetty/ydraw-factory/complex-prim-factory.c.

#include <yetty/ydraw-core/complex-prim-types.h>
#include <stdlib.h>
#include <string.h>

#include "complex-prim-types-internal.h"

//=============================================================================
// Helper functions
//=============================================================================

bool yetty_ydraw_is_complex_type(uint32_t type)
{
    return (type >= YETTY_YDRAW_COMPLEX_TYPE_BASE);
}

//=============================================================================
// Base ops wrappers for flyweight compatibility
//=============================================================================

static struct yetty_ycore_size_result figure_size_wrapper(const uint32_t *prim)
{
    size_t size = yetty_ydraw_figure_size(prim);
    return YETTY_OK(yetty_ycore_size, size);
}

static struct rectangle_result figure_aabb_wrapper(const uint32_t *prim)
{
    return yetty_ydraw_figure_aabb(prim);
}

static const struct yetty_ydraw_drawable_base_ops g_figure_base_ops = {
    .size = figure_size_wrapper,
    .aabb = figure_aabb_wrapper,
};

struct yetty_ydraw_drawable_base_ops_ptr_result yetty_ydraw_figure_handler(
    uint32_t prim_type)
{
    if (yetty_ydraw_is_complex_type(prim_type)) {
        return YETTY_OK(yetty_ydraw_drawable_base_ops_ptr, &g_figure_base_ops);
    }
    return YETTY_ERR(yetty_ydraw_drawable_base_ops_ptr, "not a complex type");
}

size_t yetty_ydraw_figure_size(const void *data)
{
    const struct yetty_ydraw_figure *prim = data;
    return sizeof(struct yetty_ydraw_figure) + prim->payload_size;
}

//=============================================================================
// Generic AABB - reads bounds from standard offset 0-15 in payload
// Wire format: [bounds_x:f32][bounds_y:f32][bounds_w:f32][bounds_h:f32][...]
//=============================================================================

#define COMPLEX_PRIM_BOUNDS_SIZE 16 /* 4 floats */

struct rectangle_result yetty_ydraw_figure_aabb(const void *data)
{
    if (!data) {
        return YETTY_ERR(rectangle, "NULL data");
    }

    const struct yetty_ydraw_figure *prim = data;
    if (prim->payload_size < COMPLEX_PRIM_BOUNDS_SIZE) {
        return YETTY_ERR(rectangle, "payload too small for bounds");
    }

    const uint8_t *payload = prim->data;
    float x, y, w, h;
    memcpy(&x, payload + 0, sizeof(float));
    memcpy(&y, payload + 4, sizeof(float));
    memcpy(&w, payload + 8, sizeof(float));
    memcpy(&h, payload + 12, sizeof(float));

    struct yetty_ycore_rectangle rect = {.min = {.x = x, .y = y}, .max = {.x = x + w, .y = y + h}};
    return YETTY_OK(rectangle, rect);
}
