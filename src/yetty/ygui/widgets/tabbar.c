/*
 * ygui-tabbar.c — tab strip with pill styling + click-to-activate.
 *
 * The tabbar inherits hbox (flex-row container). Each tab is represented
 * by a child "header" widget — a clickable rectangle with a label
 * string. Headers do not paint themselves; the tabbar's own paint walks
 * its children and renders the strip bg + pills (rounded box, active vs
 * inactive colors, accent bar under the active pill, hairline separator
 * between two adjacent inactive pills, label centered with left pad).
 *
 * Active state lives on the tabbar's data slice (one int). Each header
 * carries a back-pointer to its tabbar so its click handler can find
 * the tabbar without walking the parent chain.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/hbox.h>
#include <yetty/ygui/widgets/tabbar.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

/*-----------------------------------------------------------------------------
 * Pill geometry + brand palette. Mirrors ygui-old/ygui_tabbar.c so the
 * strip reads identically to the long-stabilised version.
 *---------------------------------------------------------------------------*/

#define TABBAR_DEFAULT_HEADER_H 32.0f
#define TABBAR_PILL_PAD_X 14.0f
#define TABBAR_PILL_GAP 4.0f
#define TABBAR_PILL_RADIUS 6.0f
#define TABBAR_ACCENT_BAR_H 3.0f
#define TABBAR_PILL_PREF_W 160.0f
#define TABBAR_SEPARATOR_W 1.0f
#define TABBAR_SEPARATOR_INSET_Y 6.0f

/* Packed RGBA (R in low byte). Matches the old theme constants:
 *   bg_strip   = BRAND_BG_ROW   (#1E262C) — flat band under the pills
 *   bg_active  = BRAND_BG       (#0B1014) — active pill body
 *   bg_inactive= BRAND_BG_ROW   (#1E262C) — inactive pill body (flush with strip)
 *   accent     = BRAND_ACCENT   (#6BA892) — 3px bar under the active pill
 *   text_act   = BRAND_TEXT_PRI (#E0E5E4)
 *   text_mut   = BRAND_TEXT_SEC (#9FA7A8)
 *   separator  = BRAND_BORDER   (#364A47) — hairline between two inactives
 */
#define COLOR_STRIP_BG 0xFF2C261Eu
#define COLOR_PILL_ACTIVE 0xFF14100Bu
#define COLOR_PILL_INACTIVE 0xFF2C261Eu
#define COLOR_ACCENT 0xFF92A86Bu
#define COLOR_TEXT_ACTIVE 0xFFE4E5E0u
#define COLOR_TEXT_MUTED 0xFFA8A79Fu
#define COLOR_SEPARATOR 0xFF474A36u

/*-----------------------------------------------------------------------------
 * Tabbar instance data.
 *---------------------------------------------------------------------------*/

struct tabbar_data {
    int active_index;
};

/*-----------------------------------------------------------------------------
 * Header subclass — clickable rectangle with a label. Inherits
 * primitive_widget so its emit_body invokes paint, but paint is a no-op
 * (the parent tabbar draws every pill). The header carries the label
 * string + a back-pointer to the tabbar for click routing.
 *---------------------------------------------------------------------------*/

struct header_data {
    char *label;
    struct yetty_ygui_object *tabbar; /* back-pointer, not owned */
};

static const struct yetty_ygui_class *header_class_get(void);

static int header_index_in_tabbar(struct yetty_ygui_object *header,
                                  struct yetty_ygui_object *tabbar)
{
    int idx = 0;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(tabbar); c;
         c = yetty_ygui_object_next_sibling(c)) {
        if (c == header) {
            return idx;
        }
        idx++;
    }
    return -1;
}

static struct yetty_ycore_void_result header_on_click(struct yetty_ygui_object *obj, void *userdata)
{
    (void)userdata;
    struct header_data *hd = yetty_ygui_data_get(obj, header_class_get());
    if (!hd->tabbar) {
        return YETTY_OK_VOID();
    }
    int idx = header_index_in_tabbar(obj, hd->tabbar);
    if (idx < 0) {
        return YETTY_OK_VOID();
    }
    return yetty_ygui_tabbar_set_active(hd->tabbar, idx);
}

static struct yetty_ycore_void_result header_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, header_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "header_constructor: super");
    struct header_data *hd = yetty_ygui_data_get(obj, header_class_get());
    hd->label = NULL;
    hd->tabbar = NULL;
    /* Each header gets a fixed preferred width so the row lays out
     * predictably; height is stretched by the hbox's ALIGN_STRETCH
     * default to match the tabbar's height. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.width = TABBAR_PILL_PREF_W;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "header_constructor: layout_set");
    return yetty_ygui_clickable_on_click_set(obj, header_on_click, NULL);
}

static struct yetty_ycore_void_result header_destructor(struct yetty_ygui_object *obj)
{
    struct header_data *hd = yetty_ygui_data_get(obj, header_class_get());
    free(hd->label);
    hd->label = NULL;
    return yetty_ygui_super_void(obj, header_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static const struct yetty_ygui_class *header_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (cls) return cls;
    static const struct yetty_ygui_op ops[] = {
        YETTY_YGUI_OP(yetty_ygui_constructor, header_constructor),
        YETTY_YGUI_OP(yetty_ygui_destructor, header_destructor),
        /* No paint override — the parent tabbar paints every pill. */
    };
    static const struct yetty_ygui_class_descriptor desc = {
        .name = "yetty_ygui_tabbar_header",
        .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct header_data),
    };
    const struct yetty_ygui_class *mixins[] = {yetty_ygui_clickable_mixin_get()};
    struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]),
        yetty_ygui_primitive_widget_class_get(), mixins, sizeof(mixins) / sizeof(mixins[0]));
    if (YETTY_IS_ERR(r)) return NULL;
    cls = r.value;
    return cls;
}

static struct yetty_ycore_void_result header_set_label(struct yetty_ygui_object *obj,
                                                       const char *label)
{
    struct header_data *hd = yetty_ygui_data_get(obj, header_class_get());
    free(hd->label);
    if (!label) {
        hd->label = NULL;
        return YETTY_OK_VOID();
    }
    size_t n = strlen(label);
    hd->label = malloc(n + 1);
    if (!hd->label) {
        return YETTY_ERR(yetty_ycore_void, "header_set_label: malloc failed");
    }
    memcpy(hd->label, label, n + 1);
    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------------
 * Tabbar — public API + paint.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result tabbar_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_tabbar_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tabbar_constructor: super");
    struct tabbar_data *td = yetty_ygui_data_get(obj, yetty_ygui_tabbar_class_get());
    td->active_index = -1;
    /* Default to the canonical 32-px tall strip; apps can override via
     * yetty_ygui_widget_layout_set before the first emit. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    if (l.height < 0.0f) {
        l.height = TABBAR_DEFAULT_HEADER_H;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "tabbar_constructor: layout_set");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result paint_pill(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                 float w, float h, uint32_t fill, float radius)
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
    if (radius <= 0.0f) {
        struct yetty_ysdf_box geom = {
            .center_x = x + w * 0.5f,
            .center_y = y + h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = h * 0.5f,
        };
        return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u, 0.0f,
                                                     &geom);
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

static struct yetty_ycore_void_result paint_label(struct yetty_ygui_emit_ctx *ctx, const char *text,
                                                  float x, float y, uint32_t color, float font_size)
{
    if (!text || !text[0]) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_buffer text_buf = {
        .data = (uint8_t *)text,
        .capacity = strlen(text),
        .size = strlen(text),
    };
    return yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, x, y, &text_buf, font_size, color,
                                          /*layer=*/0, /*font_id=*/-1, /*rotation=*/0.0f);
}

static struct yetty_ycore_void_result tabbar_paint(struct yetty_ygui_object *obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float strip_w = r.max.x - r.min.x;
    float strip_h = r.max.y - r.min.y;
    if (strip_w <= 0.0f || strip_h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* Strip background — a flat band the width of the widget. */
    struct yetty_ycore_void_result rr =
        paint_pill(ctx, r.min.x, r.min.y, strip_w, strip_h, COLOR_STRIP_BG, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: strip bg");

    struct tabbar_data *td = yetty_ygui_data_get(obj, yetty_ygui_tabbar_class_get());
    int active = td->active_index;

    int idx = 0;
    struct yetty_ygui_object *prev_header = NULL;
    int prev_active = 0;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(obj); c;
         c = yetty_ygui_object_next_sibling(c)) {
        struct yetty_ycore_rectangle pr = yetty_ygui_widget_rect(c);
        float pw = pr.max.x - pr.min.x;
        float ph = pr.max.y - pr.min.y;
        int is_active = (idx == active);
        uint32_t fill = is_active ? COLOR_PILL_ACTIVE : COLOR_PILL_INACTIVE;
        uint32_t text_color = is_active ? COLOR_TEXT_ACTIVE : COLOR_TEXT_MUTED;

        rr = paint_pill(ctx, pr.min.x, pr.min.y, pw, ph, fill, TABBAR_PILL_RADIUS);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: pill");

        if (is_active && ph > TABBAR_ACCENT_BAR_H) {
            rr = paint_pill(ctx, pr.min.x, pr.max.y - TABBAR_ACCENT_BAR_H, pw,
                            TABBAR_ACCENT_BAR_H, COLOR_ACCENT, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: accent bar");
        }

        /* Separator between two adjacent inactive pills. The active tab
         * gets visual breathing room on both sides. */
        if (prev_header && !prev_active && !is_active) {
            struct yetty_ycore_rectangle ppr = yetty_ygui_widget_rect(prev_header);
            float sep_x = (ppr.max.x + pr.min.x) * 0.5f - TABBAR_SEPARATOR_W * 0.5f;
            float sep_y = pr.min.y + TABBAR_SEPARATOR_INSET_Y;
            float sep_h = ph - 2.0f * TABBAR_SEPARATOR_INSET_Y;
            if (sep_h > 0.0f) {
                rr = paint_pill(ctx, sep_x, sep_y, TABBAR_SEPARATOR_W, sep_h, COLOR_SEPARATOR,
                                0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: separator");
            }
        }

        /* Label — vertically centered, TABBAR_PILL_PAD_X from the left. */
        struct header_data *hd = yetty_ygui_data_get(c, header_class_get());
        if (hd->label && hd->label[0]) {
            float font_size = 14.0f;
            float tx = pr.min.x + TABBAR_PILL_PAD_X;
            float ty = pr.min.y + (ph + font_size) * 0.5f - 2.0f;
            rr = paint_label(ctx, hd->label, tx, ty, text_color, font_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: label");
        }

        prev_header = c;
        prev_active = is_active;
        idx++;
    }
    return YETTY_OK_VOID();
}

struct yetty_ygui_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_ygui_object *tabbar,
                                                              const char *label)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_tabbar_add_tab: NULL tabbar");
    }
    struct yetty_ygui_object_ptr_result r = yetty_ygui_add(header_class_get(), tabbar);
    if (YETTY_IS_ERR(r)) {
        return r;
    }
    struct yetty_ygui_object *header = r.value;
    struct header_data *hd = yetty_ygui_data_get(header, header_class_get());
    hd->tabbar = tabbar;
    if (label) {
        struct yetty_ycore_void_result lr = header_set_label(header, label);
        if (YETTY_IS_ERR(lr)) {
            yetty_ycore_error_destroy(lr.error);
        }
    }
    struct tabbar_data *td = yetty_ygui_data_get(tabbar, yetty_ygui_tabbar_class_get());
    if (td->active_index < 0) {
        td->active_index = 0;
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(tabbar);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }
    return YETTY_OK(yetty_ygui_object_ptr, header);
}

struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_ygui_object *tabbar,
                                                            int index)
{
    if (!tabbar || index < 0) {
        return YETTY_OK_VOID();
    }
    int i = 0;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(tabbar); c;) {
        struct yetty_ygui_object *next = yetty_ygui_object_next_sibling(c);
        if (i == index) {
            struct yetty_ycore_void_result dr = yetty_ygui_del(c);
            if (YETTY_IS_ERR(dr)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_remove_tab: del", dr);
            }
            struct tabbar_data *td = yetty_ygui_data_get(tabbar, yetty_ygui_tabbar_class_get());
            int n = yetty_ygui_tabbar_count(tabbar);
            if (td->active_index >= n) {
                td->active_index = n - 1;
            }
            return yetty_ygui_object_set_dirty(tabbar);
        }
        c = next;
        i++;
    }
    return YETTY_OK_VOID();
}

int yetty_ygui_tabbar_count(const struct yetty_ygui_object *tabbar)
{
    if (!tabbar) {
        return 0;
    }
    int n = 0;
    for (struct yetty_ygui_object *c =
             yetty_ygui_object_first_child((struct yetty_ygui_object *)tabbar);
         c; c = yetty_ygui_object_next_sibling(c)) {
        n++;
    }
    return n;
}

int yetty_ygui_tabbar_active(const struct yetty_ygui_object *tabbar)
{
    if (!tabbar) {
        return -1;
    }
    struct tabbar_data *td =
        yetty_ygui_data_get((struct yetty_ygui_object *)tabbar, yetty_ygui_tabbar_class_get());
    return td->active_index;
}

struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_ygui_object *tabbar,
                                                            int index)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_set_active: NULL tabbar");
    }
    int n = yetty_ygui_tabbar_count(tabbar);
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= n) {
        index = n - 1;
    }
    struct tabbar_data *td = yetty_ygui_data_get(tabbar, yetty_ygui_tabbar_class_get());
    if (td->active_index == index) {
        return YETTY_OK_VOID();
    }
    td->active_index = index;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(tabbar);
    if (YETTY_IS_ERR(dr)) {
        return dr;
    }
    struct yetty_ygui_event ev = {0};
    ev.type = YETTY_YGUI_EVENT_VALUE_CHANGED;
    ev.source = tabbar;
    ev.i0 = index;
    return yetty_ygui_object_emit(tabbar, &ev);
}

/*-----------------------------------------------------------------------------
 * Class registration. Tabbar inherits hbox (flex-row container) and
 * overrides the constructor + paint.
 *---------------------------------------------------------------------------*/

const struct yetty_ygui_class *yetty_ygui_tabbar_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (cls) return cls;
    static const struct yetty_ygui_op ops[] = {
        YETTY_YGUI_OP(yetty_ygui_constructor, tabbar_constructor),
        YETTY_YGUI_OP(yetty_ygui_widget_paint, tabbar_paint),
    };
    static const struct yetty_ygui_class_descriptor desc = {
        .name = "yetty_ygui_tabbar",
        .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct tabbar_data),
    };
    struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), yetty_ygui_hbox_class_get(), NULL, 0);
    if (YETTY_IS_ERR(r)) return NULL;
    cls = r.value;
    return cls;
}
