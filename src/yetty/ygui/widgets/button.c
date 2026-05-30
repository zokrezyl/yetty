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

struct [[clang::annotate("class@ygui:button")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] [[clang::annotate("uses@ygui:clickable")]] button_data {
    char *label;
};

[[clang::annotate("override@ygui:button:constructor")]]
static struct yetty_ycore_void_result button_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "button_constructor: super");
    struct button_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"));
    d->label = NULL;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:button:destructor")]]
static struct yetty_ycore_void_result button_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct button_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"));
    free(d->label);
    d->label = NULL;
    return yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

/* Brand colors as packed RGBA — R in low byte. Per rules/08-branding.md.
 *   BRAND_BG_ROW         #1E262C → 0xFF2C261E  inactive surface
 *   BRAND_BG_LIFTED      #141A1F → 0xFF1F1A14  hover-brighten target
 *   BRAND_ACCENT         #6BA892 → 0xFF92A86B  pressed surface
 *   BRAND_ACCENT_BRIGHT  #74C5A5 → 0xFFA5C574  hover outline
 *   BRAND_TEXT_PRI       #E0E5E4 → 0xFFE4E5E0  label colour
 */
#define BTN_BG_IDLE 0xFF2C261Eu       /* BRAND_BG_ROW */
#define BTN_BG_PRESSED 0xFF92A86Bu    /* BRAND_ACCENT */
#define BTN_HOVER_OUTLINE 0xFFA5C574u /* BRAND_ACCENT_BRIGHT */
#define BTN_FG 0xFFE4E5E0u            /* BRAND_TEXT_PRIMARY */

/* Lighten / darken a packed RGBA by ~+10% / -10% per channel — used to
 * build the convex gradient top / bottom colours. Alpha preserved. */
static uint32_t pack_lighten(uint32_t c)
{
    uint32_t r = c & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = (c >> 16) & 0xFF;
    uint32_t a = (c >> 24) & 0xFF;
    r = (r * 230 + 255 * 25) / 255;
    g = (g * 230 + 255 * 25) / 255;
    b = (b * 230 + 255 * 25) / 255;
    return (a << 24) | (b << 16) | (g << 8) | r;
}
static uint32_t pack_darken(uint32_t c)
{
    uint32_t r = c & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = (c >> 16) & 0xFF;
    uint32_t a = (c >> 24) & 0xFF;
    r = r * 230 / 255;
    g = g * 230 / 255;
    b = b * 230 / 255;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

[[clang::annotate("override@ygui:button:widget_paint")]]
static struct yetty_ycore_void_result button_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "button_paint: NULL ctx");
    }
    struct button_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"));
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    int pressed = yetty_ygui_clickable_is_pressed(obj);
    int hovered = yetty_ygui_object_is_hovered(obj);

    /* Pressed surface goes to accent + drops 1 px to give tactile feedback.
     * Hover replaces the idle row-grey with the slightly lighter BG_LIFTED
     * so the cursor's button stands out before commit. */
    uint32_t surface = pressed   ? BTN_BG_PRESSED
                       : hovered ? 0xFF1F1A14u /* BRAND_BG_LIFTED */
                                 : BTN_BG_IDLE;
    float press_offset = pressed ? 1.0f : 0.0f;

    float radius = 6.0f;
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }

    /* Surface paint:
     *  - idle:     convex gradient (top lighter, bottom darker)
     *  - hovered:  same gradient + a 2px accent-bright stroke around
     *              the perimeter, drawn by the same gradient primitive
     *              (fill_color/stroke_color are independent fields on
     *              the SDF op, the way panel.c uses bg+border).
     *  - pressed:  flat accent fill, 1 px y-offset so it reads as
     *              "depressed" against the surrounding surface. */
    if (!pressed) {
        uint32_t top = pack_lighten(surface);
        uint32_t bot = pack_darken(surface);
        struct yetty_ysdf_linear_gradient_box gg = {
            .center_x = r.min.x + w * 0.5f,
            .center_y = r.min.y + press_offset + h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = h * 0.5f,
            .corner_radius = radius,
            .grad_x0 = r.min.x,
            .grad_y0 = r.min.y + press_offset,
            .grad_x1 = r.min.x,
            .grad_y1 = r.min.y + press_offset + h,
            .color0 = top,
            .color1 = bot,
        };
        uint32_t stroke = hovered ? BTN_HOVER_OUTLINE : 0u;
        float stroke_w = hovered ? 2.0f : 0.0f;
        struct yetty_ycore_void_result rr = yetty_ydraw_draw_list_add_cmd_add_linear_gradient_box(
            ctx->ygrid_draw_list, /*id=*/0, /*z_order=*/0, /*fill=*/0u, stroke, stroke_w, &gg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "button_paint: gradient surface");
    } else {
        struct yetty_ysdf_rounded_box geom = {
            .center_x = r.min.x + w * 0.5f,
            .center_y = r.min.y + press_offset + h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = h * 0.5f,
            .radius_top_right = radius,
            .radius_bottom_right = radius,
            .radius_top_left = radius,
            .radius_bottom_left = radius,
        };
        struct yetty_ycore_void_result rr = yetty_ydraw_draw_list_add_cmd_add_rounded_box(
            ctx->ygrid_draw_list, /*id=*/0, /*z_order=*/0, surface, /*stroke=*/0u,
            /*stroke_w=*/0.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "button_paint: pressed surface");
    }

    if (d->label && d->label[0]) {
        float font_size = 14.0f;
        float x = r.min.x + 12.0f;
        float y = r.min.y + press_offset + (h + font_size) * 0.5f - 2.0f;
        struct yetty_ycore_buffer text_buf = {
            .data = (uint8_t *)d->label,
            .capacity = strlen(d->label),
            .size = strlen(d->label),
        };
        struct yetty_ycore_void_result rr = yetty_ydraw_draw_list_add_text(
            ctx->ygrid_draw_list, x, y, &text_buf, font_size, BTN_FG, /*layer=*/0, /*font_id=*/-1,
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
    struct button_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"));
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
    struct button_data *d = yetty_ygui_data_get(
        (struct yetty_ygui_object *)obj,
        yetty_ygui_class_expect(yetty_ygui_button_class_get(), "yetty_ygui_button_class_get"));
    return d->label;
}

#include "button.gen.c"
