/*
 * ygui-button.c — push button.
 *
 * Inherits the base widget class; lists the clickable mixin so press /
 * release / on_click come for free.
 *
 * Paint writes a {"BTNN", id, pressed_flag, label_len, label} marker
 * into the ygrid body. Receiver-side rendering (SDF rounded rect +
 * label glyphs) lands when the widget catalog stabilises.
 */

#include "../internal.h"

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ysdf/funcs.gen.h>
#include <stdlib.h>
#include <string.h>

struct button_data {
    char *label;
};

static struct yetty_ycore_void_result button_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_button_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "button_constructor: super");
    struct button_data *d = yetty_ygui_data_get(obj, yetty_ygui_button_class_get());
    d->label = NULL;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result button_destructor(struct yetty_ygui_object *obj)
{
    struct button_data *d = yetty_ygui_data_get(obj, yetty_ygui_button_class_get());
    free(d->label);
    d->label = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_button_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

/* Brand colors as packed RGBA (R in low byte). */
#define BTN_BG_IDLE 0xFF1F2620u    /* BRAND_BG_ROW darkened */
#define BTN_BG_PRESSED 0xFF6BA892u /* BRAND_ACCENT */
#define BTN_FG 0xFFE4E5E0u         /* BRAND_TEXT_PRIMARY */

static struct yetty_ycore_void_result button_paint(struct yetty_ygui_object *obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "button_paint: NULL ctx");
    }
    struct button_data *d = yetty_ygui_data_get(obj, yetty_ygui_button_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    int pressed = yetty_ygui_clickable_is_pressed(obj);
    uint32_t fill = pressed ? BTN_BG_PRESSED : BTN_BG_IDLE;
    /* Rounded surface. */
    float radius = 6.0f;
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box geom = {
        .center_x = r.min.x + w * 0.5f,
        .center_y = r.min.y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = radius,
        .radius_bottom_right = radius,
        .radius_top_left = radius,
        .radius_bottom_left = radius,
    };
    struct yetty_ycore_void_result rr = yetty_ydraw_draw_list_add_cmd_add_rounded_box(
        ctx->ygrid_draw_list, /*id=*/0, /*z_order=*/0, fill, /*stroke=*/0u, /*stroke_w=*/0.0f,
        &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "button_paint: surface");

    if (d->label && d->label[0]) {
        /* Label centred-ish — leave a small left/top inset and let the
         * caller size the button. Real centring measurement lands when
         * the label widget grows text-width measurement. */
        float font_size = 14.0f;
        float x = r.min.x + 12.0f;
        float y = r.min.y + (h + font_size) * 0.5f - 2.0f;
        struct yetty_ycore_buffer text_buf = {
            .data = (uint8_t *)d->label,
            .capacity = strlen(d->label),
            .size = strlen(d->label),
        };
        rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, x, y, &text_buf, font_size,
                                            BTN_FG, /*layer=*/0, /*font_id=*/-1,
                                            /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "button_paint: label");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_ygui_object *obj,
                                                           const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_button_set_label: NULL obj");
    }
    struct button_data *d = yetty_ygui_data_get(obj, yetty_ygui_button_class_get());
    free(d->label);
    if (!label) {
        d->label = NULL;
    } else {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_button_set_label: malloc failed");
        }
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

const char *yetty_ygui_button_get_label(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct button_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_button_class_get());
    return d->label;
}

const struct yetty_ygui_class *yetty_ygui_button_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (!cls) {
        static const struct yetty_ygui_op ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, button_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, button_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, button_paint),
        };
        static const struct yetty_ygui_class_descriptor desc = {.name = "yetty_ygui_button",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct button_data),};
        const struct yetty_ygui_class *mixins[] = {yetty_ygui_clickable_mixin_get(), NULL};
        struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
            &desc, ops, sizeof(ops) / sizeof(ops[0]),
            yetty_ygui_primitive_widget_class_get(), mixins, sizeof(mixins) / sizeof(mixins[0]) - 1);
        if (YETTY_IS_ERR(r)) {
            return NULL;
        }
        cls = r.value;
    }
    return cls;
}
