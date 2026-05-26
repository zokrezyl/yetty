/*
 * ygui-toggle.c — on/off pill switch.
 *
 * Inherits primitive_widget + clickable. on_release fires the click cb
 * we install in the ctor, which flips `on` and emits VALUE_CHANGED.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/toggle.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_TRACK_OFF 0xFF2C261Eu
#define COLOR_TRACK_ON 0xFF92A86Bu
#define COLOR_KNOB 0xFFE4E5E0u
#define COLOR_TEXT 0xFFE4E5E0u

struct toggle_data {
    char *label;
    int on;
};

static struct yetty_ycore_void_result on_click_flip(struct yetty_ygui_object *obj, void *userdata)
{
    (void)userdata;
    struct toggle_data *d = yetty_ygui_data_get(obj, yetty_ygui_toggle_class_get());
    d->on = !d->on;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) return dr;
    struct yetty_ygui_event ev = {0};
    ev.type = YETTY_YGUI_EVENT_VALUE_CHANGED;
    ev.source = obj;
    ev.i0 = d->on;
    return yetty_ygui_object_emit(obj, &ev);
}

static struct yetty_ycore_void_result toggle_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_toggle_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "toggle_constructor: super");
    struct toggle_data *d = yetty_ygui_data_get(obj, yetty_ygui_toggle_class_get());
    d->label = NULL;
    d->on = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click_flip, NULL);
}

static struct yetty_ycore_void_result toggle_destructor(struct yetty_ygui_object *obj)
{
    struct toggle_data *d = yetty_ygui_data_get(obj, yetty_ygui_toggle_class_get());
    free(d->label);
    d->label = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_toggle_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint_rounded(struct yetty_ygui_emit_ctx *ctx, float x,
                                                    float y, float w, float h, uint32_t fill,
                                                    float radius)
{
    if (w <= 0.0f || h <= 0.0f) return YETTY_OK_VOID();
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    struct yetty_ysdf_rounded_box geom = {
        .center_x = x + w * 0.5f, .center_y = y + h * 0.5f,
        .half_width = w * 0.5f, .half_height = h * 0.5f,
        .radius_top_right = radius, .radius_bottom_right = radius,
        .radius_top_left = radius, .radius_bottom_left = radius,
    };
    return yetty_ydraw_draw_list_add_cmd_add_rounded_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
}

static struct yetty_ycore_void_result paint_circle(struct yetty_ygui_emit_ctx *ctx, float cx,
                                                   float cy, float radius, uint32_t fill)
{
    struct yetty_ysdf_circle geom = {
        .center_x = cx, .center_y = cy, .radius = radius,
    };
    return yetty_ydraw_draw_list_add_cmd_add_circle(ctx->ygrid_draw_list, 0u, 0u, fill, 0u, 0.0f,
                                                    &geom);
}

static struct yetty_ycore_void_result toggle_paint(struct yetty_ygui_object *obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "toggle_paint: NULL ctx");
    }
    struct toggle_data *d = yetty_ygui_data_get(obj, yetty_ygui_toggle_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float h = r.max.y - r.min.y;
    float pill_h = h - 4.0f;
    if (pill_h < 14.0f) pill_h = 14.0f;
    float pill_w = pill_h * 1.9f;
    float pill_y = r.min.y + (h - pill_h) * 0.5f;
    uint32_t track = d->on ? COLOR_TRACK_ON : COLOR_TRACK_OFF;
    struct yetty_ycore_void_result rr =
        paint_rounded(ctx, r.min.x, pill_y, pill_w, pill_h, track, pill_h * 0.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "toggle_paint: track");
    float knob_radius = pill_h * 0.4f;
    float knob_cx = d->on ? (r.min.x + pill_w - pill_h * 0.5f) : (r.min.x + pill_h * 0.5f);
    rr = paint_circle(ctx, knob_cx, pill_y + pill_h * 0.5f, knob_radius, COLOR_KNOB);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "toggle_paint: knob");
    if (d->label && d->label[0]) {
        float font_size = 14.0f;
        float tx = r.min.x + pill_w + 8.0f;
        float ty = r.min.y + (h + font_size) * 0.5f - 2.0f;
        struct yetty_ycore_buffer tb = {.data = (uint8_t *)d->label,
                                        .capacity = strlen(d->label),
                                        .size = strlen(d->label)};
        rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, tx, ty, &tb, font_size,
                                            COLOR_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "toggle_paint: label");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_toggle_set_label(struct yetty_ygui_object *obj,
                                                           const char *label)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "toggle_set_label: NULL obj");
    struct toggle_data *d = yetty_ygui_data_get(obj, yetty_ygui_toggle_class_get());
    free(d->label);
    if (!label) {
        d->label = NULL;
    } else {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) return YETTY_ERR(yetty_ycore_void, "toggle_set_label: malloc");
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_toggle_set_on(struct yetty_ygui_object *obj, int on)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "toggle_set_on: NULL obj");
    struct toggle_data *d = yetty_ygui_data_get(obj, yetty_ygui_toggle_class_get());
    d->on = on ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_toggle_get_on(const struct yetty_ygui_object *obj)
{
    if (!obj) return 0;
    struct toggle_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_toggle_class_get());
    return d->on;
}

static const struct yetty_ygui_op toggle_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, toggle_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, toggle_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, toggle_paint),
};

static const struct yetty_ygui_class_descriptor toggle_desc = {
    .name = "yetty_ygui_toggle",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct toggle_data),
};

const struct yetty_ygui_class *yetty_ygui_toggle_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (!cls) {
        const struct yetty_ygui_class *mixins[] = {yetty_ygui_clickable_mixin_get()};
        struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
            &toggle_desc, toggle_ops, sizeof(toggle_ops) / sizeof(toggle_ops[0]),
            yetty_ygui_primitive_widget_class_get(), mixins,
            sizeof(mixins) / sizeof(mixins[0]));
        if (YETTY_IS_ERR(r)) return NULL;
        cls = r.value;
    }
    return cls;
}
