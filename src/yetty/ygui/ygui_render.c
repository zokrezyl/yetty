/*
 * ygui_render.c — render helpers that drop ypaint-core primitives directly.
 *
 * Bridges YGui widgets to a yetty_ypaint_core_buffer. Shapes go through
 * yetty_ysdf_add_* (generated from ysdf/primitives.yaml); text goes through
 * yetty_ypaint_core_buffer_add_text (canvas decomposes text_spans into
 * YPAINT_SDF_GLYPH primitives in PASS4). No shim layer in between.
 */

#include "ygui_internal.h"

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/*=============================================================================
 * Render Context
 *===========================================================================*/

void yetty_ygui_render_ctx_init(struct yetty_ygui_render_ctx *ctx, struct yetty_ypaint_core_buffer *buffer,
                          const struct yetty_ygui_theme *theme)
{
    ctx->buffer = buffer;
    ctx->theme = theme;
    ctx->offset_x = 0;
    ctx->offset_y = 0;
    ctx->clip_x = 0;
    ctx->clip_y = 0;
    ctx->clip_w = 0;
    ctx->clip_h = 0;
    ctx->has_clip = 0;
}

/*=============================================================================
 * Drawing Functions
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box(struct yetty_ygui_render_ctx *ctx, float x, float y, float w,
                                               float h, uint32_t color, float radius)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box: NULL buffer");
    }

    float ax = x + ctx->offset_x;
    float ay = y + ctx->offset_y;
    float cx = ax + w * 0.5f;
    float cy = ay + h * 0.5f;
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    struct yetty_ypaint_core_id_result r;
    if (radius > 0) {
        struct yetty_ysdf_rounded_box geom = {
            .center_x = cx,
            .center_y = cy,
            .half_width = hw,
            .half_height = hh,
            .radius_top_right = radius,
            .radius_bottom_right = radius,
            .radius_top_left = radius,
            .radius_bottom_left = radius,
        };
        r = yetty_ysdf_add_rounded_box(ctx->buffer, 0, color, 0, 0.0f, &geom);
    } else {
        struct yetty_ysdf_box geom = {
            .center_x = cx,
            .center_y = cy,
            .half_width = hw,
            .half_height = hh,
            .corner_radius = 0.0f,
        };
        r = yetty_ysdf_add_box(ctx->buffer, 0, color, 0, 0.0f, &geom);
    }
    if (r.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box: add failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_outline(struct yetty_ygui_render_ctx *ctx, float x, float y,
                                                      float w, float h, uint32_t color,
                                                      float radius, float stroke_width)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_outline: NULL buffer");
    }

    float ax = x + ctx->offset_x;
    float ay = y + ctx->offset_y;
    float cx = ax + w * 0.5f;
    float cy = ay + h * 0.5f;
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    struct yetty_ypaint_core_id_result r;
    if (radius > 0) {
        struct yetty_ysdf_rounded_box geom = {
            .center_x = cx,
            .center_y = cy,
            .half_width = hw,
            .half_height = hh,
            .radius_top_right = radius,
            .radius_bottom_right = radius,
            .radius_top_left = radius,
            .radius_bottom_left = radius,
        };
        r = yetty_ysdf_add_rounded_box(ctx->buffer, 0, 0, color, stroke_width, &geom);
    } else {
        struct yetty_ysdf_box geom = {
            .center_x = cx,
            .center_y = cy,
            .half_width = hw,
            .half_height = hh,
            .corner_radius = 0.0f,
        };
        r = yetty_ysdf_add_box(ctx->buffer, 0, 0, color, stroke_width, &geom);
    }
    if (r.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_outline: add failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_text(struct yetty_ygui_render_ctx *ctx, const char *text, float x,
                                                float y, uint32_t color, float font_size)
{
    if (!ctx->buffer || !text) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_text: NULL buffer or text");
    }

    float ax = x + ctx->offset_x;
    float ay = y + ctx->offset_y;

    /* Text spans live on the ypaint-core buffer and get decomposed into
     * YPAINT_SDF_GLYPH primitives by the canvas's add_buffer (PASS4). */
    size_t tlen = 0;
    while (text[tlen] != '\0') {
        tlen++;
    }
    struct yetty_ycore_buffer tbuf = {
        .data = (uint8_t *)(uintptr_t)text,
        .size = tlen,
        .capacity = tlen,
    };
    return yetty_ypaint_core_buffer_add_text(ctx->buffer, ax, ay + font_size * 0.8f, &tbuf,
                                             font_size, color,
                                             /*layer*/ 0, /*font_id*/ -1,
                                             /*rotation*/ 0.0f);
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_circle(struct yetty_ygui_render_ctx *ctx, float cx, float cy,
                                                  float r, uint32_t color)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_circle: NULL buffer");
    }

    float ax = cx + ctx->offset_x;
    float ay = cy + ctx->offset_y;

    struct yetty_ysdf_circle geom = {.center_x = ax, .center_y = ay, .radius = r};
    struct yetty_ypaint_core_id_result rr = yetty_ysdf_add_circle(ctx->buffer, 0, color, 0, 0.0f, &geom);
    if (rr.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_circle: add failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_circle_outline(struct yetty_ygui_render_ctx *ctx, float cx,
                                                          float cy, float r, uint32_t color,
                                                          float stroke_width)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_circle_outline: NULL buffer");
    }

    float ax = cx + ctx->offset_x;
    float ay = cy + ctx->offset_y;

    struct yetty_ysdf_circle geom = {.center_x = ax, .center_y = ay, .radius = r};
    struct yetty_ypaint_core_id_result rr =
        yetty_ysdf_add_circle(ctx->buffer, 0, 0, color, stroke_width, &geom);
    if (rr.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_circle_outline: add failed");
    }
    return YETTY_OK_VOID();
}

/* Mix the alpha channel of a 0xAARRGGBB color by a 0..1 scalar. */
static uint32_t color_scale_alpha(uint32_t color, float scale)
{
    if (scale <= 0.0f) {
        return color & 0x00FFFFFFu;
    }
    if (scale >= 1.0f) {
        return color;
    }
    uint32_t a = (color >> 24) & 0xFFu;
    uint32_t scaled = (uint32_t)((float)a * scale);
    if (scaled > 0xFFu) scaled = 0xFFu;
    return (color & 0x00FFFFFFu) | (scaled << 24);
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_shadow(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, float radius,
    float elevation, uint32_t shadow_color, float alpha_mul)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_shadow: NULL buffer");
    }
    if (elevation <= 0.0f || alpha_mul <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* Three-layer fake gaussian:
     *   layer 0: largest spread, faintest        (outer halo)
     *   layer 1: mid spread, mid alpha           (core shadow)
     *   layer 2: small spread, near-full alpha   (sharp contact)
     * Each layer is offset down by `elevation` and outset by an amount
     * proportional to its layer index. The SDF box's antialiased edge
     * does the soft-falloff work for us. */
    struct {
        float spread;     /* added to each side */
        float alpha_mul;  /* relative to alpha_mul argument */
    } layers[] = {
        {elevation * 1.50f, 0.30f},
        {elevation * 0.85f, 0.55f},
        {elevation * 0.35f, 1.00f},
    };

    for (size_t i = 0; i < sizeof layers / sizeof layers[0]; i++) {
        float s = layers[i].spread;
        float lx = x - s;
        float ly = y - s + elevation;
        float lw = w + 2.0f * s;
        float lh = h + 2.0f * s;
        float lr = radius + s;
        uint32_t color = color_scale_alpha(shadow_color, alpha_mul * layers[i].alpha_mul);
        struct yetty_ycore_void_result r =
            yetty_ygui_render_ctx_render_box(ctx, lx, ly, lw, lh, color, lr);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_linear_gradient(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, float radius,
    float gx0, float gy0, float gx1, float gy1, uint32_t color0, uint32_t color1)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_linear_gradient: NULL buffer");
    }

    float ax = x + ctx->offset_x;
    float ay = y + ctx->offset_y;
    struct yetty_ysdf_linear_gradient_box geom = {
        .center_x = ax + w * 0.5f,
        .center_y = ay + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = radius,
        .grad_x0 = gx0 + ctx->offset_x,
        .grad_y0 = gy0 + ctx->offset_y,
        .grad_x1 = gx1 + ctx->offset_x,
        .grad_y1 = gy1 + ctx->offset_y,
        .color0 = color0,
        .color1 = color1,
    };
    struct yetty_ypaint_core_id_result r =
        yetty_ysdf_add_linear_gradient_box(ctx->buffer, 0, /*fill=*/0, 0, 0.0f, &geom);
    if (r.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_linear_gradient: add failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_radial_gradient(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, float radius,
    float cx, float cy, float gradient_radius, uint32_t color_inner, uint32_t color_outer)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_radial_gradient: NULL buffer");
    }

    float ax = x + ctx->offset_x;
    float ay = y + ctx->offset_y;
    struct yetty_ysdf_radial_gradient_box geom = {
        .center_x = ax + w * 0.5f,
        .center_y = ay + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = radius,
        .grad_cx = cx + ctx->offset_x,
        .grad_cy = cy + ctx->offset_y,
        .grad_radius = gradient_radius,
        .color_inner = color_inner,
        .color_outer = color_outer,
    };
    struct yetty_ypaint_core_id_result r =
        yetty_ysdf_add_radial_gradient_box(ctx->buffer, 0, /*fill=*/0, 0, 0.0f, &geom);
    if (r.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_box_radial_gradient: add failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_render_ctx_render_triangle(struct yetty_ygui_render_ctx *ctx, float x0, float y0,
                                                    float x1, float y1, float x2, float y2,
                                                    uint32_t color)
{
    if (!ctx->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_triangle: NULL buffer");
    }

    float ox = ctx->offset_x;
    float oy = ctx->offset_y;

    struct yetty_ysdf_triangle geom = {
        .vertex_a_x = x0 + ox,
        .vertex_a_y = y0 + oy,
        .vertex_b_x = x1 + ox,
        .vertex_b_y = y1 + oy,
        .vertex_c_x = x2 + ox,
        .vertex_c_y = y2 + oy,
    };
    struct yetty_ypaint_core_id_result rr =
        yetty_ysdf_add_triangle(ctx->buffer, 0, color, 0, 0.0f, &geom);
    if (rr.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "ygui_render_triangle: add failed");
    }
    return YETTY_OK_VOID();
}
