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
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

/*-----------------------------------------------------------------------------
 * Pill geometry + brand palette.
 *---------------------------------------------------------------------------*/

#define TABBAR_DEFAULT_HEADER_H 32.0f
#define TABBAR_PILL_PAD_X 14.0f
#define TABBAR_PILL_GAP 4.0f
#define TABBAR_PILL_RADIUS 6.0f
#define TABBAR_ACCENT_BAR_H 3.0f
#define TABBAR_PILL_PREF_W 160.0f
/* 2px so the SDF box renders as a solid line — a 1px-wide box is nearly
 * erased by the shader's edge anti-aliasing, which is why the old hairline
 * was effectively invisible. */
#define TABBAR_SEPARATOR_W 2.0f
#define TABBAR_SEPARATOR_INSET_Y 6.0f

/* The new-tab "+" affordance sits immediately right of the rightmost tab
 * (not glued to the far edge). It is painted + hit-tested by the tabbar
 * itself rather than being a separate sibling button, so it always tracks
 * the last tab's trailing edge. Enabled only when a new-tab callback is
 * installed via yetty_ygui_tabbar_set_on_new_tab. */
#define TABBAR_PLUS_GAP 4.0f
#define TABBAR_PLUS_W 30.0f

/* Colors are pulled from engine->theme at paint time so user config
 * (style.yui.* / style.ygui.*) reaches the renderer. The defaults
 * live in src/yetty/ygui/theme.c::theme_create_default; this file
 * names the role each theme field plays in the tabbar:
 *
 *   strip      = theme->yui_strip         flat band under the pills
 *   active     = theme->yui_tab_active    active pill body
 *   inactive   = theme->yui_tab_inactive  inactive pill body
 *   accent     = theme->yui_accent        3px bar under the active pill
 *   text_act   = theme->text_primary      active tab label
 *   text_mut   = theme->text_muted        inactive tab label
 *   separator  = theme->border            hairline between two inactives
 */
#include <yetty/ygui/theme.h>

/*-----------------------------------------------------------------------------
 * Tabbar instance data.
 *---------------------------------------------------------------------------*/

struct [[clang::annotate("class@ygui:tabbar")]] [[clang::annotate("parent@ygui:hbox")]]
tabbar_data {
    int active_index;
    /* When set, each pill paints a close-x in its right band and a
     * click landing there fires this callback (with the tab index)
     * instead of activating the tab. */
    yetty_ygui_tab_close_cb close_cb;
    void *close_userdata;
    /* When set, a "+" affordance is painted right of the last tab; a
     * press+release landing on it fires this callback. The tabbar
     * overrides press/release itself (rather than using the clickable
     * mixin) so that it consumes ONLY presses on the "+": a press on the
     * empty strip stays unconsumed and falls through to the host's
     * window drag-to-move handler. Tab headers consume their own clicks
     * before the tabbar ever sees them. */
    yetty_ygui_tab_new_cb new_tab_cb;
    void *new_tab_userdata;
    int plus_pressed; /* a press landed on the "+"; fire on matching release */
};

/* Width of the close-x hit band at the right edge of each pill. */
#define TABBAR_CLOSE_W 22.0f

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

static struct yetty_yclass_ptr_result header_class_get(void);

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

static struct yetty_ycore_void_result header_on_click(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      void *userdata)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)userdata;
    struct header_data *hd =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(header_class_get(), "header_class_get"));
    if (!hd->tabbar) {
        return YETTY_OK_VOID();
    }
    int idx = header_index_in_tabbar(obj, hd->tabbar);
    if (idx < 0) {
        return YETTY_OK_VOID();
    }
    /* If the tabbar has a close handler and the click landed in the
     * pill's right close band, fire close instead of activate. */
    struct tabbar_data *td =
        yetty_ygui_data_get(hd->tabbar, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(),
                                                                "yetty_ygui_tabbar_class_get"));
    if (td->close_cb) {
        float px = 0.0f, py = 0.0f;
        yetty_ygui_clickable_press_pos(obj, &px, &py);
        struct yetty_ycore_rectangle pr = yetty_ygui_widget_rect(obj);
        if (px >= pr.max.x - TABBAR_CLOSE_W) {
            td->close_cb(hd->tabbar, idx, td->close_userdata);
            return YETTY_OK_VOID();
        }
    }
    return yetty_ygui_tabbar_set_active(hd->tabbar, idx);
}

static struct yetty_ycore_void_result header_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_class_expect(header_class_get(), "header_class_get"),
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "header_constructor: super");
    struct header_data *hd =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(header_class_get(), "header_class_get"));
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

static struct yetty_ycore_void_result header_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct header_data *hd =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(header_class_get(), "header_class_get"));
    free(hd->label);
    hd->label = NULL;
    return yetty_ygui_super_void(obj,
                                 yetty_ygui_class_expect(header_class_get(), "header_class_get"),
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static const struct yetty_yclass_op header_ops[] = {
    {YETTY_YGUI_DOMAIN, "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
     (yetty_yclass_impl_t)header_constructor},
    {YETTY_YGUI_DOMAIN, "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
     (yetty_yclass_impl_t)header_destructor}};

static const struct yetty_yclass_descriptor header_desc = {
    .name = "yetty_ygui_tabbar_header",
    .type = YETTY_YCLASS_TYPE_REGULAR,
    .data_size = sizeof(struct header_data),
};

struct yetty_yclass_ptr_result header_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    struct yetty_yclass_ptr_result _pr = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(_pr)) {
        return YETTY_ERR(yetty_yclass_ptr, "header_class_get: parent class failed", _pr);
    }
    const struct yetty_yclass *parent = _pr.value;
    struct yetty_yclass_ptr_result _mxr0 = yetty_ygui_clickable_mixin_get();
    if (YETTY_IS_ERR(_mxr0)) {
        return YETTY_ERR(yetty_yclass_ptr, "header_class_get: mixin0 failed", _mxr0);
    }
    const struct yetty_yclass *_mixins[] = {_mxr0.value};
    const struct yetty_yclass *const *mixins = _mixins;
    size_t mixin_count = 1;
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&header_desc, header_ops, sizeof(header_ops) / sizeof(header_ops[0]),
                              parent, mixins, mixin_count);
    if (YETTY_IS_OK(_r)) {
        cls = _r.value;
    }
    return _r;
}

static struct yetty_ycore_void_result header_set_label(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj,
                                                       const char *label)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct header_data *hd =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(header_class_get(), "header_class_get"));
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

/* Trailing edge (in viewport coords) of the rightmost tab header, or
 * `fallback` when the tabbar has no tabs yet. The "+" affordance and the
 * divider in front of it both anchor to this x. */
static float tabbar_tabs_right_edge(struct yetty_ygui_object *tabbar, float fallback)
{
    struct yetty_ygui_object *last = NULL;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(tabbar); c;
         c = yetty_ygui_object_next_sibling(c)) {
        last = c;
    }
    if (!last) {
        return fallback;
    }
    return yetty_ygui_widget_rect(last).max.x;
}

/* Rect of the "+" pill given the strip rect and the tabs' right edge. */
static struct yetty_ycore_rectangle tabbar_plus_rect(struct yetty_ycore_rectangle strip,
                                                     float tabs_right)
{
    struct yetty_ycore_rectangle pr;
    pr.min.x = tabs_right + TABBAR_PLUS_GAP;
    pr.max.x = pr.min.x + TABBAR_PLUS_W;
    pr.min.y = strip.min.y;
    pr.max.y = strip.max.y;
    return pr;
}

static int tabbar_pt_in_plus(struct yetty_ygui_object *obj, float x, float y)
{
    struct tabbar_data *td = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"));
    if (!td->new_tab_cb) {
        return 0;
    }
    struct yetty_ycore_rectangle strip = yetty_ygui_widget_rect(obj);
    float tabs_right = tabbar_tabs_right_edge(obj, strip.min.x);
    struct yetty_ycore_rectangle pr = tabbar_plus_rect(strip, tabs_right);
    return x >= pr.min.x && x < pr.max.x && y >= pr.min.y && y < pr.max.y;
}

/* A press reaches the tabbar only when it missed every tab header (headers
 * consume their own clicks). We consume it ONLY when it lands on the "+";
 * a press anywhere else on the strip stays unconsumed so it falls through
 * to the host's window drag-to-move handler. */
[[clang::annotate("override@ygui:tabbar:widget_on_press")]]
static struct yetty_ycore_int_result tabbar_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj, float x,
                                                     float y, int button)
{
    (void)yclass_ctx;
    (void)button;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct tabbar_data *td = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"));
    if (!tabbar_pt_in_plus(obj, x, y)) {
        td->plus_pressed = 0;
        return YETTY_OK(yetty_ycore_int, 0);
    }
    td->plus_pressed = 1;
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:tabbar:widget_on_release")]]
static struct yetty_ycore_int_result tabbar_on_release(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj,
                                                       float x, float y, int button)
{
    (void)yclass_ctx;
    (void)button;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct tabbar_data *td = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"));
    int was_pressed = td->plus_pressed;
    td->plus_pressed = 0;
    if (!was_pressed) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Fire only if the release lands back on the "+", like a button. */
    if (td->new_tab_cb && tabbar_pt_in_plus(obj, x, y)) {
        td->new_tab_cb(obj, td->new_tab_userdata);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:tabbar:constructor")]]
static struct yetty_ycore_void_result tabbar_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tabbar_constructor: super");
    struct tabbar_data *td = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"));
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

[[clang::annotate("override@ygui:tabbar:widget_paint")]]
static struct yetty_ycore_void_result tabbar_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float strip_w = r.max.x - r.min.x;
    float strip_h = r.max.y - r.min.y;
    if (strip_w <= 0.0f || strip_h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Pull colors + font size from the engine theme so style.yui.* and
     * style.ygui.font-size in user config reach the renderer. The
     * fallback literals are belt-and-braces — theme is engine-owned and
     * non-NULL after framework_create. */
    const struct yetty_ygui_theme *theme =
        yetty_ygui_framework_theme(yetty_ygui_object_framework(obj));
    uint32_t color_strip = theme ? theme->yui_strip : 0xFF1F1A14u;
    uint32_t color_active = theme ? theme->yui_tab_active : 0xFF14100Bu;
    uint32_t color_inactive = theme ? theme->yui_tab_inactive : 0xFF2C261Eu;
    uint32_t color_accent = theme ? theme->yui_accent : 0xFF92A86Bu;
    uint32_t color_text_active = theme ? theme->text_primary : 0xFFE4E5E0u;
    uint32_t color_text_muted = theme ? theme->text_muted : 0xFFA8A79Fu;
    uint32_t color_separator = theme ? theme->border : 0xFF474A36u;
    float base_font_size = theme && theme->font_size > 0.0f ? theme->font_size : 14.0f;

    /* Strip background — a flat band the width of the widget. */
    ydebug("tabbar_paint: strip rect=(%.1f,%.1f)+%.1fx%.1f color=0x%08X",
           r.min.x, r.min.y, strip_w, strip_h, color_strip);
    struct yetty_ycore_void_result rr =
        paint_pill(ctx, r.min.x, r.min.y, strip_w, strip_h, color_strip, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: strip bg");

    struct tabbar_data *td = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"));
    int active = td->active_index;
    ydebug("tabbar_paint: active_index=%d", active);

    int idx = 0;
    struct yetty_ygui_object *prev_header = NULL;
    int prev_active = 0;
    for (struct yetty_ygui_object *c = yetty_ygui_object_first_child(obj); c;
         c = yetty_ygui_object_next_sibling(c)) {
        struct yetty_ycore_rectangle pr = yetty_ygui_widget_rect(c);
        float pw = pr.max.x - pr.min.x;
        float ph = pr.max.y - pr.min.y;
        int is_active = (idx == active);
        uint32_t fill = is_active ? color_active : color_inactive;
        uint32_t text_color = is_active ? color_text_active : color_text_muted;

        rr = paint_pill(ctx, pr.min.x, pr.min.y, pw, ph, fill, TABBAR_PILL_RADIUS);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: pill");

        if (is_active && ph > TABBAR_ACCENT_BAR_H) {
            ydebug("tabbar_paint: accent rect=(%.1f,%.1f)+%.1fx%.1f color=0x%08X",
                   pr.min.x, pr.max.y - TABBAR_ACCENT_BAR_H, pw, TABBAR_ACCENT_BAR_H, color_accent);
            rr = paint_pill(ctx, pr.min.x, pr.max.y - TABBAR_ACCENT_BAR_H, pw, TABBAR_ACCENT_BAR_H,
                            color_accent, 0.0f);
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
                rr = paint_pill(ctx, sep_x, sep_y, TABBAR_SEPARATOR_W, sep_h, color_separator,
                                0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: separator");
            }
        }

        /* Label — vertically centered, TABBAR_PILL_PAD_X from the left. */
        struct header_data *hd =
            yetty_ygui_data_get(c, yetty_ygui_class_expect(header_class_get(), "header_class_get"));
        if (hd->label && hd->label[0]) {
            float font_size = base_font_size;
            float tx = pr.min.x + TABBAR_PILL_PAD_X;
            float ty = pr.min.y + (ph + font_size) * 0.5f - 2.0f;
            rr = paint_label(ctx, hd->label, tx, ty, text_color, font_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: label");
        }

        /* Close-x in the right band when the tabbar has a close handler. */
        if (td->close_cb && pw > TABBAR_CLOSE_W) {
            float font_size = base_font_size;
            float tx = pr.max.x - TABBAR_CLOSE_W + 4.0f;
            float ty = pr.min.y + (ph + font_size) * 0.5f - 2.0f;
            rr = paint_label(ctx, "\xC3\x97", tx, ty, text_color, font_size); /* × */
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: close-x");
        }

        prev_header = c;
        prev_active = is_active;
        idx++;
    }

    /* New-tab "+" affordance, immediately right of the rightmost tab.
     * A divider in front of it marks the last tab's trailing edge (unless
     * that tab is active — the active tab keeps breathing room on both
     * sides, matching the between-tab separators above). */
    if (td->new_tab_cb) {
        float tabs_right = prev_header ? yetty_ygui_widget_rect(prev_header).max.x : r.min.x;

        if (prev_header && !prev_active) {
            float sep_x = tabs_right + TABBAR_PLUS_GAP * 0.5f - TABBAR_SEPARATOR_W * 0.5f;
            float sep_y = r.min.y + TABBAR_SEPARATOR_INSET_Y;
            float sep_h = strip_h - 2.0f * TABBAR_SEPARATOR_INSET_Y;
            if (sep_h > 0.0f) {
                rr = paint_pill(ctx, sep_x, sep_y, TABBAR_SEPARATOR_W, sep_h, color_separator,
                                0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: plus separator");
            }
        }

        struct yetty_ycore_rectangle plus = tabbar_plus_rect(r, tabs_right);
        /* "+" glyph, centered in the plus rect. Muted like an inactive
         * tab — Chrome-style minimal affordance, no persistent pill. */
        if (plus.min.x < r.max.x) {
            /* The plus glyph is intentionally a bit larger than body
             * font — scale relative to base_font_size so config tuning
             * still keeps the visual hierarchy. */
            float font_size = base_font_size * (17.0f / 14.0f);
            float glyph_w = font_size * 0.45f;
            float tx = plus.min.x + (TABBAR_PLUS_W - glyph_w) * 0.5f;
            float ty = r.min.y + (strip_h + font_size) * 0.5f - 2.0f;
            rr = paint_label(ctx, "+", tx, ty, color_text_muted, font_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: plus glyph");
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ygui_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_ygui_object *tabbar,
                                                              const char *label)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_tabbar_add_tab: NULL tabbar");
    }
    struct yetty_ygui_object_ptr_result r =
        yetty_ygui_add(yetty_ygui_class_expect(header_class_get(), "header_class_get"), tabbar);
    if (YETTY_IS_ERR(r)) {
        return r;
    }
    struct yetty_ygui_object *header = r.value;
    struct header_data *hd = yetty_ygui_data_get(
        header, yetty_ygui_class_expect(header_class_get(), "header_class_get"));
    hd->tabbar = tabbar;
    if (label) {
        struct yetty_ycore_void_result lr =
            header_set_label(NULL, (struct yetty_yclass_object *)header, label);
        if (YETTY_IS_ERR(lr)) {
            yetty_ycore_error_destroy(lr.error);
        }
    }
    struct tabbar_data *td =
        yetty_ygui_data_get(tabbar, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(),
                                                            "yetty_ygui_tabbar_class_get"));
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
            struct tabbar_data *td =
                yetty_ygui_data_get(tabbar, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(),
                                                                    "yetty_ygui_tabbar_class_get"));
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
    struct tabbar_data *td = yetty_ygui_data_get(
        (struct yetty_ygui_object *)tabbar,
        yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(), "yetty_ygui_tabbar_class_get"));
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
    struct tabbar_data *td =
        yetty_ygui_data_get(tabbar, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(),
                                                            "yetty_ygui_tabbar_class_get"));
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

struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_close(struct yetty_ygui_object *tabbar,
                                                             yetty_ygui_tab_close_cb cb,
                                                             void *userdata)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_set_on_close: NULL tabbar");
    }
    struct tabbar_data *td =
        yetty_ygui_data_get(tabbar, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(),
                                                            "yetty_ygui_tabbar_class_get"));
    td->close_cb = cb;
    td->close_userdata = userdata;
    return yetty_ygui_object_set_dirty(tabbar);
}

struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_new_tab(struct yetty_ygui_object *tabbar,
                                                                yetty_ygui_tab_new_cb cb,
                                                                void *userdata)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_set_on_new_tab: NULL tabbar");
    }
    struct tabbar_data *td =
        yetty_ygui_data_get(tabbar, yetty_ygui_class_expect(yetty_ygui_tabbar_class_get(),
                                                            "yetty_ygui_tabbar_class_get"));
    td->new_tab_cb = cb;
    td->new_tab_userdata = userdata;
    return yetty_ygui_object_set_dirty(tabbar);
}

/*-----------------------------------------------------------------------------
 * Class registration. Tabbar inherits hbox (flex-row container) and
 * overrides the constructor + paint.
 *---------------------------------------------------------------------------*/

#include "tabbar.gen.c"
