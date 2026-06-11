/*
 * ygui-slider.c — horizontal slider.
 *
 * Inherits primitive_widget. Owns its own on_press override (the
 * clickable mixin's press just sets a flag — sliders need to react
 * with the press x to map a value).
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * slider.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_slider_ptr, struct yetty_ygui_slider *);
struct yetty_yclass_ptr_result yetty_ygui_slider_class_get(void);
struct yetty_ygui_slider_ptr_result yetty_ygui_slider_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#define COLOR_TRACK 0xFF2C261Eu
#define COLOR_FILL 0xFF92A86Bu /* BRAND_ACCENT */
#define COLOR_THUMB 0xFFE4E5E0u

struct [[clang::annotate("class@ygui:slider")]] [[clang::annotate("parent@ygui:primitive_widget")]]
yetty_ygui_slider {
    float min_val;
    float max_val;
    float value;
};

[[clang::annotate("override@ygui:slider:constructor")]]
static struct yetty_ycore_void_result slider_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_slider_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "slider_constructor: super");
    struct yetty_ygui_slider_ptr_result d_dr = yetty_ygui_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "slider_constructor: data_get");
    struct yetty_ygui_slider *d = d_dr.value;
    d->min_val = 0.0f;
    d->max_val = 1.0f;
    d->value = 0.0f;
    return YETTY_OK_VOID();
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
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

static struct yetty_ycore_void_result paint_circle(struct yetty_ygui_emit_ctx *ctx, float cx,
                                                   float cy, float radius, uint32_t fill)
{
    struct yetty_ysdf_circle geom = {
        .center_x = cx,
        .center_y = cy,
        .radius = radius,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_circle(ctx->ygrid_drawable_list, 0u, 0u, fill, 0u,
                                                        0.0f, &geom);
}

[[clang::annotate("override@ygui:slider:widget_paint")]]
static struct yetty_ycore_void_result slider_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "slider_paint: NULL ctx");
    }
    struct yetty_ygui_slider_ptr_result d_dr = yetty_ygui_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "slider_paint: data_get");
    struct yetty_ygui_slider *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float track_h = 6.0f;
    float track_y = r.min.y + (h - track_h) * 0.5f;
    struct yetty_ycore_void_result rr =
        paint_rounded(ctx, r.min.x, track_y, w, track_h, COLOR_TRACK, track_h * 0.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "slider_paint: track");
    float range = d->max_val - d->min_val;
    float t = range > 0.0f ? (d->value - d->min_val) / range : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    float fw = w * t;
    if (fw > 0.0f) {
        rr = paint_rounded(ctx, r.min.x, track_y, fw, track_h, COLOR_FILL, track_h * 0.5f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "slider_paint: fill");
    }
    float thumb_radius = h * 0.5f - 2.0f;
    if (thumb_radius < 5.0f) {
        thumb_radius = 5.0f;
    }
    rr = paint_circle(ctx, r.min.x + fw, r.min.y + h * 0.5f, thumb_radius, COLOR_THUMB);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "slider_paint: thumb");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:slider:widget_on_press")]]
static struct yetty_ycore_int_result slider_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj,
                                                     float x, float y, int button)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)y;
    (void)button;
    struct yetty_ygui_slider_ptr_result d_dr = yetty_ygui_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "slider_on_press: data_get");
    struct yetty_ygui_slider *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    if (w <= 0.0f) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    float t = (x - r.min.x) / w;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    d->value = d->min_val + t * (d->max_val - d->min_val);
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "slider_on_press: dirty", dr);
    }
    struct yetty_ygui_event ev = {0};
    ev.type = YETTY_YGUI_EVENT_VALUE_CHANGED;
    ev.source = obj;
    ev.x = d->value;
    struct yetty_ycore_void_result er = yetty_ygui_object_emit(obj, &ev);
    if (YETTY_IS_ERR(er)) {
        return YETTY_ERR(yetty_ycore_int, "slider_on_press: emit", er);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:slider:widget_on_motion")]]
static struct yetty_ycore_int_result slider_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      float x, float y)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    /* Only track the cursor while this slider holds the pointer capture —
     * i.e. between its own press and release. The framework also delivers
     * motion to whatever the pointer merely hovers (not just the capture
     * target), so without this guard the value would change on a plain
     * hover instead of a click-drag. */
    struct yetty_ygui_framework *engine = yetty_ygui_widget_framework(obj);
    if (!engine || yetty_ygui_framework_pressed_widget(engine) != obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return slider_on_press(NULL, (struct yetty_yclass_object *)obj, x, y, 0);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_slider_set_range(struct yetty_yclass_object *obj,
                                                           float min, float max)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "slider_set_range: NULL obj");
    }
    struct yetty_ygui_slider_ptr_result d_dr = yetty_ygui_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_slider_set_range: data_get");
    struct yetty_ygui_slider *d = d_dr.value;
    d->min_val = min;
    d->max_val = max;
    d->value = clampf(d->value, min, max);
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_slider_set_value(struct yetty_yclass_object *obj,
                                                           float value)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "slider_set_value: NULL obj");
    }
    struct yetty_ygui_slider_ptr_result d_dr = yetty_ygui_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_slider_set_value: data_get");
    struct yetty_ygui_slider *d = d_dr.value;
    d->value = clampf(value, d->min_val, d->max_val);
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_float_result yetty_ygui_slider_get_value(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    struct yetty_ygui_slider_ptr_result d_dr =
        yetty_ygui_slider_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, d_dr, "yetty_ygui_slider_get_value: data_get");
    struct yetty_ygui_slider *d = d_dr.value;
    return YETTY_OK(yetty_ycore_float, d->value);
}

#include "slider.gen.c"
