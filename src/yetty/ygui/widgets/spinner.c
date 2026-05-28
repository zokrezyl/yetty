/*
 * ygui-spinner.c — - / value / + . Three hit-zones on a single
 * widget: left third = decrement, middle = display, right third =
 * increment. Each click adjusts value by `step` (default 1.0).
 */
#include "../internal.h"
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/spinner.h>
#include <yetty/ysdf/funcs.gen.h>
#include <stdio.h>
#include <string.h>

#define COLOR_BG 0xFF1F1A14u
#define COLOR_BTN 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_ACC 0xFF92A86Bu

struct [[clang::annotate("class@ygui:spinner")]]
       [[clang::annotate("parent@ygui:primitive_widget")]] spinner_data {
    float value, min_v, max_v, step;
};

/* Inline helper to silently drop unused result errors. */
static inline void yetty_ycore_error_destroy_unused(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
}

[[clang::annotate("override@ygui:spinner:constructor")]]
static struct yetty_ycore_void_result spinner_constructor(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_spinner_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "spinner_constructor: super");
    struct spinner_data *d = yetty_ygui_data_get(obj, yetty_ygui_spinner_class_get().value);
    d->value = 0;
    d->min_v = 0;
    d->max_v = 100;
    d->step = 1;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:spinner:widget_on_press")]]
static struct yetty_ycore_int_result spinner_on_press(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj, float x,
                                                     float y, int button)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    (void)y;
    (void)button;
    struct spinner_data *d = yetty_ygui_data_get(obj, yetty_ygui_spinner_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float third = w / 3.0f;
    float lx = x - r.min.x;
    if (lx < third)
        d->value -= d->step;
    else if (lx > 2.0f * third)
        d->value += d->step;
    if (d->value < d->min_v) d->value = d->min_v;
    if (d->value > d->max_v) d->value = d->max_v;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) return YETTY_ERR(yetty_ycore_int, "spinner_on_press: dirty", dr);
    struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_VALUE_CHANGED,
                                  .source = obj,
                                  .x = d->value};
    yetty_ycore_error_destroy_unused(yetty_ygui_object_emit(obj, &ev));
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    if (radius <= 0) {
        struct yetty_ysdf_box g = {.center_x = x + w * 0.5f, .center_y = y + h * 0.5f,
                                   .half_width = w * 0.5f, .half_height = h * 0.5f};
        return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0, 0, fill, 0, 0, &g);
    }
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    struct yetty_ysdf_rounded_box g = {.center_x = x + w * 0.5f, .center_y = y + h * 0.5f,
                                       .half_width = w * 0.5f, .half_height = h * 0.5f,
                                       .radius_top_right = radius, .radius_bottom_right = radius,
                                       .radius_top_left = radius, .radius_bottom_left = radius};
    return yetty_ydraw_draw_list_add_cmd_add_rounded_box(ctx->ygrid_draw_list, 0, 0, fill, 0, 0,
                                                         &g);
}

static struct yetty_ycore_void_result paint_text(struct yetty_ygui_emit_ctx *ctx, const char *t,
                                                 float x, float y, float fs, uint32_t c)
{
    size_t n = strlen(t);
    struct yetty_ycore_buffer tb = {.data = (uint8_t *)t, .capacity = n, .size = n};
    return yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, x, y, &tb, fs, c, 0, -1, 0);
}

[[clang::annotate("override@ygui:spinner:widget_paint")]]
static struct yetty_ycore_void_result spinner_paint(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    if (!ctx || !ctx->ygrid_draw_list)
        return YETTY_ERR(yetty_ycore_void, "spinner_paint: NULL ctx");
    struct spinner_data *d = yetty_ygui_data_get(obj, yetty_ygui_spinner_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    float third = w / 3.0f;
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        paint_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4),
                        "spinner_paint: bg");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        paint_box(ctx, r.min.x + 2, r.min.y + 2, third - 4, h - 4, COLOR_BTN, 3),
                        "spinner_paint: minus_bg");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        paint_box(ctx, r.min.x + 2 * third + 2, r.min.y + 2, third - 4, h - 4,
                                  COLOR_BTN, 3),
                        "spinner_paint: plus_bg");
    float fs = 14.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        paint_text(ctx, "-", r.min.x + third * 0.5f - 4, ty, fs, COLOR_ACC),
                        "spinner_paint: minus");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        paint_text(ctx, "+", r.min.x + 2.5f * third - 4, ty, fs, COLOR_ACC),
                        "spinner_paint: plus");
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", d->value);
    return paint_text(ctx, buf, r.min.x + third + 8, ty, fs, COLOR_TEXT);
}

struct yetty_ycore_void_result yetty_ygui_spinner_set_value(struct yetty_ygui_object *obj, float v)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "spinner_set_value: NULL");
    struct spinner_data *d = yetty_ygui_data_get(obj, yetty_ygui_spinner_class_get().value);
    d->value = v;
    return yetty_ygui_object_set_dirty(obj);
}
struct yetty_ycore_void_result yetty_ygui_spinner_set_range(struct yetty_ygui_object *obj, float mn,
                                                            float mx, float step)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "spinner_set_range: NULL");
    struct spinner_data *d = yetty_ygui_data_get(obj, yetty_ygui_spinner_class_get().value);
    d->min_v = mn;
    d->max_v = mx;
    d->step = step > 0 ? step : 1;
    return yetty_ygui_object_set_dirty(obj);
}
float yetty_ygui_spinner_get_value(const struct yetty_ygui_object *obj)
{
    if (!obj) return 0;
    return ((struct spinner_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                       yetty_ygui_spinner_class_get().value))
        ->value;
}

#include "spinner.gen.c"
