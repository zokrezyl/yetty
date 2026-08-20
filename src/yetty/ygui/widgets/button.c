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
#include "yetty/gen/impl/ygui/widget.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * button.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_button_ptr, struct yetty_ygui_button *);
struct yetty_yclass_ptr_result yetty_ygui_button_class_get(void);
struct yetty_ygui_button_ptr_result yetty_ygui_button_from(struct yetty_yclass_object *obj);

#include "yetty/gen/impl/ygui/primitive-widget.h"

#include <yetty/ydraw-list/drawable-list.h>
#include "yetty/gen/impl/ygui/mixins/clickable.h"
#include <yetty/ygui/theme.h>
#include <yetty/ysdf/funcs.gen.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ygui:button") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    YETTY_ANNOTATE("uses@ygui:clickable") yetty_ygui_button {
    char *label;
    /* Icon drawn as SDF primitives instead of a text label. 0 = none (plain
     * label button).
     * Window controls (flat caption-strip cell, square hover wash, matching
     * ychrome so titlebars read identically): 1=minimize (bar), 2=maximize
     * (box outline), 3=close (X).
     * Browser-toolbar navigation (round hover wash, Chrome-style glyphs):
     * 4=back arrow, 5=forward arrow, 6=reload (open ring + arrowhead),
     * 7=stop (X) — 6/7 are the two states of a reload/stop toggle. */
    int chrome_icon;
};

YETTY_ANNOTATE("override@ygui:button:constructor")
static struct yetty_ycore_void_result button_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_button_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "button_constructor: super");
    struct yetty_ygui_button_ptr_result d_dr = yetty_ygui_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "button_constructor: data_get");
    struct yetty_ygui_button *d = d_dr.value;
    d->label = NULL;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:button:destructor")
static struct yetty_ycore_void_result button_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_button_ptr_result d_dr = yetty_ygui_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "button_destructor: data_get");
    struct yetty_ygui_button *d = d_dr.value;
    free(d->label);
    d->label = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_button_class_get().value,
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

YETTY_ANNOTATE("override@ygui:button:widget_paint")
static struct yetty_ycore_void_result button_paint(struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "button_paint: NULL ctx");
    }
    struct yetty_ygui_button_ptr_result d_dr = yetty_ygui_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "button_paint: data_get");
    struct yetty_ygui_button *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "button_paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result pressed_r = yetty_ygui_clickable_is_pressed(obj);
    int pressed = 0;
    if (YETTY_IS_OK(pressed_r)) {
        pressed = pressed_r.value;
    } else {
        yetty_ycore_error_destroy(pressed_r.error);
    }
    struct yetty_ycore_int_result hovered_res = yetty_ygui_widget_is_hovered(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hovered_res, "button_paint: is_hovered");
    int hovered = hovered_res.value;

    /* Browser-toolbar navigation icons: a round hover wash (Chrome-style)
     * under a stroked SDF glyph. 4=back, 5=forward, 6=reload, 7=stop. */
    if (d->chrome_icon >= 4) {
        float cx = r.min.x + w * 0.5f;
        float cy = r.min.y + h * 0.5f;
        if (hovered || pressed) {
            float wash_radius = (w < h ? w : h) * 0.5f - 3.0f;
            /* Pressed darkens to BRAND_BORDER, hover to BRAND_BG_ROW. */
            uint32_t wash_color = pressed ? 0xFF474A36u : BTN_BG_IDLE;
            struct yetty_ysdf_circle wash = {cx, cy, wash_radius};
            struct yetty_ycore_void_result wash_res = yetty_ydraw_drawable_list_add_cmd_add_circle(
                ctx->ygrid_drawable_list, 0, 0, wash_color, 0u, 0.0f, &wash);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, wash_res, "button_paint: nav hover wash");
        }
        float ext = h * 0.16f;
        float stroke = 1.5f;
        struct yetty_ycore_void_result icon_res = YETTY_OK_VOID();
        if (d->chrome_icon == 4 || d->chrome_icon == 5) {
            /* Arrow: horizontal shaft plus a chevron head at the tip. */
            float dir = (d->chrome_icon == 4) ? -1.0f : 1.0f;
            float tip_x = cx + dir * ext;
            float tail_x = cx - dir * ext;
            struct yetty_ysdf_segment shaft = {tail_x, cy, tip_x, cy};
            icon_res = yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->ygrid_drawable_list, 0, 1,
                                                                     0u, BTN_FG, stroke, &shaft);
            if (YETTY_IS_OK(icon_res)) {
                struct yetty_ysdf_segment head_upper = {tip_x, cy, tip_x - dir * ext * 0.9f,
                                                        cy - ext * 0.9f};
                icon_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                    ctx->ygrid_drawable_list, 0, 1, 0u, BTN_FG, stroke, &head_upper);
            }
            if (YETTY_IS_OK(icon_res)) {
                struct yetty_ysdf_segment head_lower = {tip_x, cy, tip_x - dir * ext * 0.9f,
                                                        cy + ext * 0.9f};
                icon_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                    ctx->ygrid_drawable_list, 0, 1, 0u, BTN_FG, stroke, &head_lower);
            }
        } else if (d->chrome_icon == 6) {
            /* Reload: a ring with its gap centered on top plus a filled
			 * arrowhead at the gap's left edge pointing clockwise across
			 * it (Material "refresh" reads the same way).
			 *
			 * Ring-SDF orientation: the shader folds x (mirror), rotates
			 * by the normal (cos t, sin t), and keeps the half-plane x<0
			 * — the visible span works out to 2t centered on the screen
			 * BOTTOM. For a gap of 2g on top, t = 180° - g, so
			 * normal = (-cos g, sin g). */
            float gap_half_rad = 0.61f; /* ~35° — a ~70° top gap */
            float ring_radius = ext * 1.15f;
            float sin_gap = sinf(gap_half_rad);
            float cos_gap = cosf(gap_half_rad);
            struct yetty_ysdf_ring ring = {cx, cy, -cos_gap, sin_gap, ring_radius, stroke * 2.0f};
            icon_res = yetty_ydraw_drawable_list_add_cmd_add_ring(ctx->ygrid_drawable_list, 0, 1,
                                                                  BTN_FG, 0u, 0.0f, &ring);
            if (YETTY_IS_OK(icon_res)) {
                /* Arrowhead at the top-left gap edge; tangent (clockwise,
				 * across the gap) points right and slightly up. */
                float edge_x = cx - ring_radius * sin_gap;
                float edge_y = cy - ring_radius * cos_gap;
                float tangent_x = cos_gap;
                float tangent_y = -sin_gap;
                float head_len = ext * 0.85f;
                float head_half_width = ext * 0.6f;
                struct yetty_ysdf_triangle head = {
                    edge_x + tangent_x * head_len,      edge_y + tangent_y * head_len,
                    edge_x + sin_gap * head_half_width, edge_y + cos_gap * head_half_width,
                    edge_x - sin_gap * head_half_width, edge_y - cos_gap * head_half_width,
                };
                icon_res = yetty_ydraw_drawable_list_add_cmd_add_triangle(
                    ctx->ygrid_drawable_list, 0, 1, BTN_FG, 0u, 0.0f, &head);
            }
        } else {
            /* Stop: an X, slightly larger than the window-control close
			 * glyph so it fills the round wash the way Chrome's does. */
            float x_ext = ext * 0.95f;
            struct yetty_ysdf_segment diag_a = {cx - x_ext, cy - x_ext, cx + x_ext, cy + x_ext};
            struct yetty_ysdf_segment diag_b = {cx - x_ext, cy + x_ext, cx + x_ext, cy - x_ext};
            icon_res = yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->ygrid_drawable_list, 0, 1,
                                                                     0u, BTN_FG, stroke, &diag_a);
            if (YETTY_IS_OK(icon_res)) {
                icon_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
                    ctx->ygrid_drawable_list, 0, 1, 0u, BTN_FG, stroke, &diag_b);
            }
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void, icon_res, "button_paint: nav icon");
        return YETTY_OK_VOID();
    }

    /* Window-control mode: paint a flat caption cell with an SDF icon (no
     * rounded gradient surface, no text label), matching ychrome's controls so
     * yetty's yui titlebar reads identically to the ychrome-driven tools.
     * Colours mirror ychrome: strip bg, off-white icon, a lifted hover wash for
     * minimize/maximize and a red one for close. */
    if (d->chrome_icon) {
        float cx = r.min.x + w * 0.5f;
        float cy = r.min.y + h * 0.5f;
        if (hovered || pressed) {
            uint32_t wash = (d->chrome_icon == 3) ? 0xFF3B3BC8u  /* close → red */
                                                  : 0xFF463A30u; /* lifted strip */
            struct yetty_ysdf_box hl = {cx, cy, w * 0.5f, h * 0.5f, 0.0f};
            struct yetty_ycore_void_result hr = yetty_ydraw_drawable_list_add_cmd_add_box(
                ctx->ygrid_drawable_list, 0, 0, wash, 0u, 0.0f, &hl);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "button_paint: chrome hover");
        }
        float ext = h * 0.18f; /* icon half-extent */
        float stroke = 1.5f;
        struct yetty_ycore_void_result ir = YETTY_OK_VOID();
        if (d->chrome_icon == 1) {
            struct yetty_ysdf_segment seg = {cx - ext, cy, cx + ext, cy};
            ir = yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->ygrid_drawable_list, 0, 1, 0u,
                                                               BTN_FG, stroke, &seg);
        } else if (d->chrome_icon == 2) {
            struct yetty_ysdf_box box = {cx, cy, ext, ext, 1.0f};
            ir = yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0, 1, 0u,
                                                           BTN_FG, stroke, &box);
        } else {
            struct yetty_ysdf_segment a = {cx - ext, cy - ext, cx + ext, cy + ext};
            struct yetty_ysdf_segment b = {cx - ext, cy + ext, cx + ext, cy - ext};
            ir = yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->ygrid_drawable_list, 0, 1, 0u,
                                                               BTN_FG, stroke, &a);
            if (YETTY_IS_OK(ir)) {
                ir = yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->ygrid_drawable_list, 0, 1,
                                                                   0u, BTN_FG, stroke, &b);
            }
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "button_paint: chrome icon");
        return YETTY_OK_VOID();
    }

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
        struct yetty_ycore_void_result rr =
            yetty_ydraw_drawable_list_add_cmd_add_linear_gradient_box(
                ctx->ygrid_drawable_list, /*id=*/0, /*z_order=*/0, /*fill=*/0u, stroke, stroke_w,
                &gg);
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
        struct yetty_ycore_void_result rr = yetty_ydraw_drawable_list_add_cmd_add_rounded_box(
            ctx->ygrid_drawable_list, /*id=*/0, /*z_order=*/0, surface, /*stroke=*/0u,
            /*stroke_w=*/0.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "button_paint: pressed surface");
    }

    if (d->label && d->label[0]) {
        /* Pull body font size from the engine theme so style.ygui.font-size
         * in user config reaches the button. Belt-and-braces fallback if
         * theme is somehow NULL (engine owns it post-create). */
        struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_widget_framework(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "button_paint: framework");
        const struct yetty_ygui_theme *theme = yetty_ygui_framework_theme(framework_res.value);
        float font_size = theme && theme->font_size > 0.0f ? theme->font_size : 14.0f;
        float x = r.min.x + 12.0f;
        float y = r.min.y + press_offset + (h + font_size) * 0.5f - 2.0f;
        struct yetty_ycore_buffer text_buf = {
            .data = (uint8_t *)d->label,
            .capacity = strlen(d->label),
            .size = strlen(d->label),
        };
        struct yetty_ycore_void_result rr =
            yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, x, y, &text_buf, font_size,
                                               BTN_FG, /*layer=*/0, /*font_id=*/-1,
                                               /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "button_paint: label");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_yclass_object *obj,
                                                           const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_button_set_label: NULL obj");
    }
    struct yetty_ygui_button_ptr_result d_dr = yetty_ygui_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_button_set_label: data_get");
    struct yetty_ygui_button *d = d_dr.value;
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
    return yetty_ygui_widget_set_dirty(obj);
}

/* Draw the button as an SDF icon instead of a text label: 0=none (normal
 * label button); window controls 1=minimize, 2=maximize, 3=close (used by
 * yetty's yui titlebar so its controls match the ychrome-driven tools);
 * browser-toolbar navigation 4=back, 5=forward, 6=reload, 7=stop. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_button_set_chrome_icon(struct yetty_yclass_object *obj,
                                                                 int kind)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_button_set_chrome_icon: NULL obj");
    }
    struct yetty_ygui_button_ptr_result d_dr = yetty_ygui_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_button_set_chrome_icon: data_get");
    struct yetty_ygui_button *d = d_dr.value;
    d->chrome_icon = (kind >= 1 && kind <= 7) ? kind : 0;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_ygui_button_get_label(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ygui_button_get_label: invalid args");
    }
    struct yetty_ygui_button_ptr_result d_dr =
        yetty_ygui_button_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr, "yetty_ygui_button_get_label: data_get");
    struct yetty_ygui_button *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->label);
}

#include "yetty/gen/impl/ygui/widgets/button.c"
