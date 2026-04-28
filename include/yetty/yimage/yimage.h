// yimage - Image complex primitive for ypaint
// Pure C API - accepts decoded RGBA8 pixel data
//
// yimage is a "complex primitive" in ypaint:
// - Has spatial presence in the ypaint canvas (AABB for culling)
// - Has its own gpu_resource_set (texture + uniforms)
// - Referenced from ypaint canvas like other primitives
// - The binder packs its texture into the atlas

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <yetty/ycore/result.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Constants
 *===========================================================================*/

/* Primitive type ID (must match shader) */
#define YETTY_YIMAGE_PRIM_TYPE_ID 0x000A

/* Sampling modes */
#define YETTY_YIMAGE_FILTER_NEAREST 0
#define YETTY_YIMAGE_FILTER_BILINEAR 1

/*=============================================================================
 * yetty_yimage - main image object (opaque)
 *===========================================================================*/

struct yetty_yimage;

/*=============================================================================
 * Result types
 *===========================================================================*/

YETTY_YRESULT_DECLARE(yetty_yimage, struct yetty_yimage *);

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_yimage_result yetty_yimage_create(void);
void yetty_yimage_destroy(struct yetty_yimage *img);

/*=============================================================================
 * Pixel data
 *===========================================================================*/

/* Set RGBA8 pixel data (copies the data).
 * pixels: raw RGBA8 bytes, 4 bytes per pixel
 * width/height: pixel dimensions
 */
struct yetty_ycore_void_result yetty_yimage_set_pixels(struct yetty_yimage *img,
                                                       const uint8_t *pixels, uint32_t width,
                                                       uint32_t height);

/* Get pixel dimensions */
uint32_t yetty_yimage_pixel_width(const struct yetty_yimage *img);
uint32_t yetty_yimage_pixel_height(const struct yetty_yimage *img);

/*=============================================================================
 * Spatial (for ypaint canvas integration)
 *===========================================================================*/

/* Set display bounds in ypaint coordinates (pixels relative to cursor) */
void yetty_yimage_set_bounds(struct yetty_yimage *img, float x, float y, float width, float height);

/* Get AABB for ypaint canvas culling */
void yetty_yimage_get_aabb(const struct yetty_yimage *img, float *min_x, float *min_y, float *max_x,
                           float *max_y);

/*=============================================================================
 * Display properties
 *===========================================================================*/

void yetty_yimage_set_zoom(struct yetty_yimage *img, float zoom);
float yetty_yimage_get_zoom(const struct yetty_yimage *img);

void yetty_yimage_set_pan(struct yetty_yimage *img, float center_x, float center_y);

void yetty_yimage_set_filter(struct yetty_yimage *img, uint32_t filter);

/*=============================================================================
 * GPU Resource Set
 *===========================================================================*/

struct yetty_yrender_gpu_resource_set_result yetty_yimage_get_gpu_resource_set(
    struct yetty_yimage *img);

bool yetty_yimage_is_dirty(const struct yetty_yimage *img);
void yetty_yimage_clear_dirty(struct yetty_yimage *img);

/*=============================================================================
 * Primitive serialization (for ypaint buffer)
 *===========================================================================*/

#define YETTY_YIMAGE_PRIM_HEADER_WORDS 8

/* Serialize image primitive header into ypaint buffer.
 * Layout: [packed_offset][type][z_order][x][y][w][h][image_index]
 */
uint32_t yetty_yimage_serialize_prim_header(struct yetty_yimage *img, uint32_t image_index,
                                            uint32_t col, uint32_t rolling_row, uint32_t z_order,
                                            uint32_t *buffer, uint32_t buffer_capacity);

#ifdef __cplusplus
}
#endif
