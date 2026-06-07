/*
 * paint-helpers.h — internal paint helpers shared across widgets.
 *
 * Each helper takes a yetty_ygui_emit_ctx and appends one prim to the
 * shared drawable_list. Headers exist so paint code in every widget reads
 * the same — instead of every widget redeclaring static paint_box /
 * paint_text / paint_circle copies.
 *
 * NOT part of the public API; #include with a relative path from a
 * widget source file.
 */
#ifndef YETTY_YGUI_WIDGETS_PAINT_HELPERS_H
#define YETTY_YGUI_WIDGETS_PAINT_HELPERS_H

#include "../internal.h"

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>

#include <string.h>

static inline struct yetty_ycore_void_result yguix_box(struct yetty_ygui_emit_ctx *ctx, float x,
                                                       float y, float w, float h, uint32_t fill,
                                                       float radius)
{
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    if (radius <= 0.0f) {
        struct yetty_ysdf_box g = {.center_x = x + w * 0.5f,
                                   .center_y = y + h * 0.5f,
                                   .half_width = w * 0.5f,
                                   .half_height = h * 0.5f};
        return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0, 0, fill, 0, 0,
                                                         &g);
    }
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box g = {.center_x = x + w * 0.5f,
                                       .center_y = y + h * 0.5f,
                                       .half_width = w * 0.5f,
                                       .half_height = h * 0.5f,
                                       .radius_top_right = radius,
                                       .radius_bottom_right = radius,
                                       .radius_top_left = radius,
                                       .radius_bottom_left = radius};
    return yetty_ydraw_drawable_list_add_cmd_add_rounded_box(ctx->ygrid_drawable_list, 0, 0, fill,
                                                             0, 0, &g);
}

static inline struct yetty_ycore_void_result yguix_text(struct yetty_ygui_emit_ctx *ctx,
                                                        const char *text, float x, float y,
                                                        float font_size, uint32_t color)
{
    if (!text || !text[0]) {
        return YETTY_OK_VOID();
    }
    size_t n = strlen(text);
    struct yetty_ycore_buffer tb = {.data = (uint8_t *)text, .capacity = n, .size = n};
    return yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, x, y, &tb, font_size, color,
                                              0, -1, 0.0f);
}

static inline struct yetty_ycore_void_result yguix_circle(struct yetty_ygui_emit_ctx *ctx, float cx,
                                                          float cy, float radius, uint32_t fill)
{
    struct yetty_ysdf_circle g = {.center_x = cx, .center_y = cy, .radius = radius};
    return yetty_ydraw_drawable_list_add_cmd_add_circle(ctx->ygrid_drawable_list, 0, 0, fill, 0, 0,
                                                        &g);
}

static inline void yguix_err_drop(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

#endif
