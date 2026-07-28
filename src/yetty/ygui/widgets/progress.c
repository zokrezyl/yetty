/*
 * ygui-progress.c — horizontal progress bar.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * progress.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_progress_ptr, struct yetty_ygui_progress *);
struct yetty_yclass_ptr_result yetty_ygui_progress_class_get(void);
struct yetty_ygui_progress_ptr_result yetty_ygui_progress_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#define COLOR_TRACK 0xFF2C261Eu
#define COLOR_FILL 0xFF92A86Bu

struct YETTY_ANNOTATE("class@ygui:progress") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_progress {
    float value;
    /* Fill colour (0xAABBGGRR). 0 = use the default brand fill. Lets a
     * caller (ytop's per-core bars) tint each bar differently. */
    uint32_t accent;
};

YETTY_ANNOTATE("override@ygui:progress:constructor")
static struct yetty_ycore_void_result progress_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_progress_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "progress_constructor: super");
    struct yetty_ygui_progress_ptr_result d_dr = yetty_ygui_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "progress_constructor: data_get");
    struct yetty_ygui_progress *d = d_dr.value;
    d->value = 0.0f;
    d->accent = 0u;
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
    return yetty_ydraw_drawable_list_add_cmd_add_rounded_box(ctx->ygrid_drawable_list, 0u, 0u, fill,
                                                             0u, 0.0f, &geom);
}

YETTY_ANNOTATE("override@ygui:progress:widget_paint")
static struct yetty_ycore_void_result progress_paint(struct yetty_yclass_object *yclass_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "progress_paint: NULL ctx");
    }
    struct yetty_ygui_progress_ptr_result d_dr = yetty_ygui_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "progress_paint: data_get");
    struct yetty_ygui_progress *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "progress_paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
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
        uint32_t fill = d->accent ? d->accent : COLOR_FILL;
        rr = paint_rounded(ctx, r.min.x, r.min.y, fw, h, fill, radius);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "progress_paint: fill");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_progress_set_value(struct yetty_yclass_object *obj,
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
    struct yetty_ygui_progress_ptr_result d_dr = yetty_ygui_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_progress_set_value: data_get");
    struct yetty_ygui_progress *d = d_dr.value;
    d->value = value;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_ygui_progress_get_value(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    struct yetty_ygui_progress_ptr_result d_dr =
        yetty_ygui_progress_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, d_dr, "yetty_ygui_progress_get_value: data_get");
    struct yetty_ygui_progress *d = d_dr.value;
    return YETTY_OK(yetty_ycore_float, d->value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_progress_set_accent(struct yetty_yclass_object *obj,
                                                              uint32_t color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "progress_set_accent: NULL obj");
    }
    struct yetty_ygui_progress_ptr_result d_dr = yetty_ygui_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_progress_set_accent: data_get");
    struct yetty_ygui_progress *d = d_dr.value;
    d->accent = color;
    return yetty_ygui_widget_set_dirty(obj);
}

#include "yetty/gen/impl/ygui/widgets/progress.c"
