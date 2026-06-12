/*
 * handler-shadertoy.c - WGSL mainImage text → yshadertoy prim envelope.
 *
 * Compiled only when YETTY_YCAT_HAS_YSHADERTOY is set (yshadertoy_core
 * linked). The bytes are the user shader verbatim — a WGSL `mainImage`
 * function per the contract in <yetty/yshadertoy/prim.h>; no parsing
 * happens sender-side, the receiving factory compiles (and rejects) it.
 *
 * Pixel size comes from the caller's config:
 *
 *   width_cells  * cell_width   → bounds_w
 *   height_cells * cell_height  → bounds_h
 *
 * Missing width falls back to 640 px; missing height to a 16:9 slice of
 * the width — a shader has no intrinsic size, so any default is a guess.
 * Pass --width / --height to override.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/yshadertoy/prim.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_shadertoy(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;
    if (!bytes || len == 0u) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ycat shadertoy: no bytes");
    }

    float bounds_w = 0.0f;
    float bounds_h = 0.0f;
    if (config) {
        if (config->width_cells > 0u && config->cell_width > 0u) {
            bounds_w = (float)(config->width_cells * config->cell_width);
        }
        if (config->height_cells > 0u && config->cell_height > 0u) {
            bounds_h = (float)(config->height_cells * config->cell_height);
        }
    }
    if (bounds_w <= 0.0f) {
        bounds_w = 640.0f;
    }
    if (bounds_h <= 0.0f) {
        bounds_h = bounds_w * 9.0f / 16.0f;
    }

    struct yetty_yshadertoy_prim_config prim_config = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = bounds_w,
        .bounds_h = bounds_h,
    };
    return yetty_yshadertoy_prim_render((const char *)bytes, len, &prim_config);
}
