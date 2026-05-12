#ifndef YETTY_YUI_TABBAR_PAINT_H
#define YETTY_YUI_TABBAR_PAINT_H

/*
 * GPU pipeline + per-frame instance buffer for the SDF-shaded rectangles
 * the tabbar draws over the workspace render. Owned and driven by the
 * tabbar; yrender stays a generic texture/surface abstraction.
 *
 * Lifetime: created once when the tabbar gets its first render call
 * (when device/queue/format are known), destroyed with the tabbar.
 */

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_tabbar_paint;

YETTY_YRESULT_DECLARE(yetty_yui_tabbar_paint_ptr, struct yetty_yui_tabbar_paint *);

/* One instanced rect — must mirror tabbar.wgsl::InstanceIn (52 B,
 * 13 floats). Coordinates are in target pixel space, origin top-left,
 * y grows down. Color is straight RGBA in [0..1]. radii in CSS order
 * (tl, tr, br, bl); set all four to 0 for a sharp rect. rotation in
 * radians around the rect's center; 0 for axis-aligned. */
struct yetty_yui_tabbar_rect {
    float x, y, w, h;
    float r, g, b, a;
    float radius_tl, radius_tr, radius_br, radius_bl;
    float rotation;
};

struct yetty_yui_tabbar_paint_ptr_result yetty_yui_tabbar_paint_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format);

void yetty_yui_tabbar_paint_destroy(struct yetty_yui_tabbar_paint *paint);

/* Draw `count` rects into `view`. target_w/h are the texture's full pixel
 * dimensions (used by the shader to convert pixel coords → NDC). loadOp
 * inside is Load so anything previously drawn into the same view (the
 * workspace render) survives. */
struct yetty_ycore_void_result yetty_yui_tabbar_paint_draw(
    struct yetty_yui_tabbar_paint *paint, WGPUTextureView view, uint32_t target_w,
    uint32_t target_h, const struct yetty_yui_tabbar_rect *rects, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_TABBAR_PAINT_H */
