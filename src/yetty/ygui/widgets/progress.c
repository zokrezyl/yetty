/*
 * ygui-progress.c — horizontal progress bar.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/progress.h>
#include <yetty/ysdf/funcs.gen.h>

#define COLOR_TRACK 0xFF2C261Eu
#define COLOR_FILL 0xFF92A86Bu

struct [[clang::annotate("class@ygui:progress")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] progress_data {
    float value;
};

[[clang::annotate("override@ygui:progress:constructor")]]
static struct yetty_ycore_void_result progress_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj,
        yetty_ygui_class_expect(yetty_ygui_progress_class_get(), "yetty_ygui_progress_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "progress_constructor: super");
    struct progress_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_progress_class_get(),
                                                         "yetty_ygui_progress_class_get"));
    d->value = 0.0f;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result paint_rounded(struct yetty_ygui_emit_ctx *ctx, float x,
                                                    float y, float w, float h, uint32_t fill,
                                                    float radius)
{
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box geom = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = radius,
        .radius_bottom_right = radius,
        .radius_top_left = radius,
        .radius_bottom_left = radius,
    };
    return yetty_ydraw_draw_list_add_cmd_add_rounded_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
}

[[clang::annotate("override@ygui:progress:widget_paint")]]
static struct yetty_ycore_void_result progress_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "progress_paint: NULL ctx");
    }
    struct progress_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_progress_class_get(),
                                                         "yetty_ygui_progress_class_get"));
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float radius = h * 0.5f;
    struct yetty_ycore_void_result rr =
        paint_rounded(ctx, r.min.x, r.min.y, w, h, COLOR_TRACK, radius);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "progress_paint: track");
    float fw = w * d->value;
    if (fw > 0.0f) {
        rr = paint_rounded(ctx, r.min.x, r.min.y, fw, h, COLOR_FILL, radius);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "progress_paint: fill");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_progress_set_value(struct yetty_ygui_object *obj,
                                                             float value)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "progress_set_value: NULL obj");
    }
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 1.0f) {
        value = 1.0f;
    }
    struct progress_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_progress_class_get(),
                                                         "yetty_ygui_progress_class_get"));
    d->value = value;
    return yetty_ygui_object_set_dirty(obj);
}

float yetty_ygui_progress_get_value(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0.0f;
    }
    struct progress_data *d = yetty_ygui_data_get(
        (struct yetty_ygui_object *)obj,
        yetty_ygui_class_expect(yetty_ygui_progress_class_get(), "yetty_ygui_progress_class_get"));
    return d->value;
}

#include "progress.gen.c"
