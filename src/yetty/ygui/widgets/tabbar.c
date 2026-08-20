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
#include "yetty/gen/impl/ygui/widget.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * tabbar.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_tabbar_ptr, struct yetty_ygui_tabbar *);
struct yetty_yclass_ptr_result yetty_ygui_tabbar_class_get(void);
struct yetty_ygui_tabbar_ptr_result yetty_ygui_tabbar_from(struct yetty_yclass_object *obj);
/* Tab callback types. Defined here in the owning .c; codegen reproduces them
 * into the generated header for any public signature that references them. */
typedef void (*yetty_ygui_tab_close_cb)(struct yetty_yclass_object *tabbar, int index,
                                        void *userdata);
typedef void (*yetty_ygui_tab_new_cb)(struct yetty_yclass_object *tabbar, void *userdata);

/* Forward declarations of this TU's own exposed helpers that are called
 * before their definitions below (the generated header — which this TU no
 * longer includes — previously supplied these). */
struct yetty_ycore_int_result yetty_ygui_tabbar_count(const struct yetty_yclass_object *tabbar);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_yclass_object *tabbar,
                                                            int index);

#include <yetty/ydraw-list/drawable-list.h>
#include "yetty/gen/impl/ygui/mixins/clickable.h"
#include "yetty/gen/impl/ygui/primitive-widget.h"
#include "yetty/gen/impl/ygui/widgets/hbox.h"
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

struct YETTY_ANNOTATE("class@ygui:tabbar") YETTY_ANNOTATE("parent@ygui:hbox") yetty_ygui_tabbar {
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
    struct yetty_yclass_object *tabbar; /* back-pointer, not owned */
};

static struct yetty_yclass_ptr_result header_class_get(void);

static struct yetty_ycore_int_result header_index_in_tabbar(struct yetty_yclass_object *header,
                                                            struct yetty_yclass_object *tabbar)
{
    int idx = 0;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, child_res, "header_index_in_tabbar: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        if (c == header) {
            return YETTY_OK(yetty_ycore_int, idx);
        }
        idx++;
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, next_res, "header_index_in_tabbar: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK(yetty_ycore_int, -1);
}

static struct yetty_ycore_void_result header_on_click(struct yetty_yclass_object *yclass_obj,
                                                      void *userdata)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)userdata;
    struct yetty_yclass_void_ptr_result hd_dr =
        yetty_yclass_object_data(obj, header_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hd_dr, "header_on_click: data_get");
    struct header_data *hd = hd_dr.value;
    if (!hd->tabbar) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result idx_res = header_index_in_tabbar(obj, hd->tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, idx_res, "header_on_click: index_in_tabbar");
    int idx = idx_res.value;
    if (idx < 0) {
        return YETTY_OK_VOID();
    }
    /* If the tabbar has a close handler and the click landed in the
     * pill's right close band, fire close instead of activate. */
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(hd->tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "header_on_click: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    if (td->close_cb) {
        float px = 0.0f, py = 0.0f;
        {
            struct yetty_ycore_void_result pp_r = yetty_ygui_clickable_press_pos(obj, &px, &py);
            if (YETTY_IS_ERR(pp_r)) {
                yetty_ycore_error_destroy(pp_r.error);
            }
        }
        struct yetty_ycore_rectangle_result pr_res = yetty_ygui_widget_rect(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr_res, "header_on_click: rect");
        struct yetty_ycore_rectangle pr = pr_res.value;
        if (px >= pr.max.x - TABBAR_CLOSE_W) {
            td->close_cb(hd->tabbar, idx, td->close_userdata);
            return YETTY_OK_VOID();
        }
    }
    return yetty_ygui_tabbar_set_active(hd->tabbar, idx);
}

static struct yetty_ycore_void_result header_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, header_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "header_constructor: super");
    struct yetty_yclass_void_ptr_result hd_dr =
        yetty_yclass_object_data(obj, header_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hd_dr, "header_constructor: data_get");
    struct header_data *hd = hd_dr.value;
    hd->label = NULL;
    hd->tabbar = NULL;
    /* Each header gets a fixed preferred width so the row lays out
     * predictably; height is stretched by the hbox's ALIGN_STRETCH
     * default to match the tabbar's height. */
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "header_constructor: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.width = TABBAR_PILL_PREF_W;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "header_constructor: layout_set");
    return yetty_ygui_clickable_on_click_set(obj, header_on_click, NULL);
}

static struct yetty_ycore_void_result header_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result hd_dr =
        yetty_yclass_object_data(obj, header_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hd_dr, "header_destructor: data_get");
    struct header_data *hd = hd_dr.value;
    free(hd->label);
    hd->label = NULL;
    return yetty_ygui_super_void(obj, header_class_get().value,
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
    .data_align = _Alignof(struct header_data),
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

static struct yetty_ycore_void_result header_set_label(struct yetty_yclass_object *yclass_obj,
                                                       const char *label)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_void_ptr_result hd_dr =
        yetty_yclass_object_data(obj, header_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hd_dr, "header_set_label: data_get");
    struct header_data *hd = hd_dr.value;
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
static struct yetty_ycore_float_result tabbar_tabs_right_edge(struct yetty_yclass_object *tabbar,
                                                              float fallback)
{
    struct yetty_yclass_object *last = NULL;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, child_res, "tabbar_tabs_right_edge: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        last = c;
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, next_res, "tabbar_tabs_right_edge: next_sibling");
        c = next_res.value;
    }
    if (!last) {
        return YETTY_OK(yetty_ycore_float, fallback);
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(last);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, rect_res, "tabbar_tabs_right_edge: rect");
    return YETTY_OK(yetty_ycore_float, rect_res.value.max.x);
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

static struct yetty_ycore_int_result tabbar_pt_in_plus(struct yetty_yclass_object *obj, float x,
                                                       float y)
{
    struct yetty_yclass_ptr_result class_result = yetty_ygui_tabbar_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, class_result, "tabbar_pt_in_plus: class");
    struct yetty_yclass_void_ptr_result td_dr = yetty_yclass_object_data(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, td_dr, "tabbar_pt_in_plus: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    if (!td->new_tab_cb) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_rectangle_result strip_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, strip_res, "tabbar_pt_in_plus: rect");
    struct yetty_ycore_rectangle strip = strip_res.value;
    struct yetty_ycore_float_result tabs_right_res = tabbar_tabs_right_edge(obj, strip.min.x);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, tabs_right_res, "tabbar_pt_in_plus: tabs_right_edge");
    float tabs_right = tabs_right_res.value;
    struct yetty_ycore_rectangle pr = tabbar_plus_rect(strip, tabs_right);
    return YETTY_OK(yetty_ycore_int,
                    x >= pr.min.x && x < pr.max.x && y >= pr.min.y && y < pr.max.y);
}

/* A press reaches the tabbar only when it missed every tab header (headers
 * consume their own clicks). We consume it ONLY when it lands on the "+";
 * a press anywhere else on the strip stays unconsumed so it falls through
 * to the host's window drag-to-move handler. */
YETTY_ANNOTATE("override@ygui:tabbar:widget_on_press")
static struct yetty_ycore_int_result tabbar_on_press(struct yetty_yclass_object *yclass_obj,
                                                     float x, float y, int button)
{
    (void)button;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, td_dr, "tabbar_on_press: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    struct yetty_ycore_int_result in_plus = tabbar_pt_in_plus(obj, x, y);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, in_plus, "tabbar_on_press: pt_in_plus");
    if (!in_plus.value) {
        td->plus_pressed = 0;
        return YETTY_OK(yetty_ycore_int, 0);
    }
    td->plus_pressed = 1;
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:tabbar:widget_on_release")
static struct yetty_ycore_int_result tabbar_on_release(struct yetty_yclass_object *yclass_obj,
                                                       float x, float y, int button)
{
    (void)button;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, td_dr, "tabbar_on_release: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    int was_pressed = td->plus_pressed;
    td->plus_pressed = 0;
    if (!was_pressed) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Fire only if the release lands back on the "+", like a button. */
    if (td->new_tab_cb) {
        struct yetty_ycore_int_result in_plus = tabbar_pt_in_plus(obj, x, y);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, in_plus, "tabbar_on_release: pt_in_plus");
        if (in_plus.value) {
            td->new_tab_cb(obj, td->new_tab_userdata);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:tabbar:constructor")
static struct yetty_ycore_void_result tabbar_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_tabbar_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tabbar_constructor: super");
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "tabbar_constructor: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    td->active_index = -1;
    /* Default to the canonical 32-px tall strip; apps can override via
     * yetty_ygui_widget_layout_set before the first emit. */
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "tabbar_constructor: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
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
        return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
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
    return yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, x, y, &text_buf, font_size,
                                              color,
                                              /*layer=*/0, /*font_id=*/-1, /*rotation=*/0.0f);
}

/* Paint at most `max_glyphs` of `text`; a longer label gets its tail
 * replaced by one ellipsis glyph so it stops before whatever sits to its
 * right (the close-x band). Glyphs are counted as UTF-8 sequence starts —
 * good enough for tab titles. */
static struct yetty_ycore_void_result paint_label_elided(struct yetty_ygui_emit_ctx *ctx,
                                                         const char *text, int max_glyphs, float x,
                                                         float y, uint32_t color, float font_size)
{
    if (!text || !text[0] || max_glyphs <= 0) {
        return YETTY_OK_VOID();
    }
    int glyph_count = 0;
    for (const char *p = text; *p; p++) {
        if (((unsigned char)*p & 0xC0) != 0x80) {
            glyph_count++;
        }
    }
    if (glyph_count <= max_glyphs) {
        return paint_label(ctx, text, x, y, color, font_size);
    }
    char clipped[256];
    int keep_glyphs = max_glyphs - 1; /* room for the ellipsis */
    size_t out = 0;
    int glyphs_kept = 0;
    for (const char *p = text; *p && out < sizeof(clipped) - 4; p++) {
        if (((unsigned char)*p & 0xC0) != 0x80) {
            if (glyphs_kept == keep_glyphs) {
                break;
            }
            glyphs_kept++;
        }
        clipped[out++] = *p;
    }
    memcpy(clipped + out, "\xE2\x80\xA6", 3); /* … */
    out += 3;
    clipped[out] = '\0';
    return paint_label(ctx, clipped, x, y, color, font_size);
}

YETTY_ANNOTATE("override@ygui:tabbar:widget_paint")
static struct yetty_ycore_void_result tabbar_paint(struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: NULL ctx");
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "tabbar_paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float strip_w = r.max.x - r.min.x;
    float strip_h = r.max.y - r.min.y;
    if (strip_w <= 0.0f || strip_h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Pull colors + font size from the engine theme so style.yui.* and
     * style.ygui.font-size in user config reach the renderer. The
     * fallback literals are belt-and-braces — theme is engine-owned and
     * non-NULL after framework_create. */
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_widget_framework(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "tabbar_paint: framework");
    const struct yetty_ygui_theme *theme = yetty_ygui_framework_theme(framework_res.value);
    uint32_t color_strip = theme ? theme->yui_strip : 0xFF1F1A14u;
    uint32_t color_active = theme ? theme->yui_tab_active : 0xFF14100Bu;
    uint32_t color_inactive = theme ? theme->yui_tab_inactive : 0xFF2C261Eu;
    uint32_t color_accent = theme ? theme->yui_accent : 0xFF92A86Bu;
    uint32_t color_text_active = theme ? theme->text_primary : 0xFFE4E5E0u;
    uint32_t color_text_muted = theme ? theme->text_muted : 0xFFA8A79Fu;
    uint32_t color_separator = theme ? theme->border : 0xFF474A36u;
    float base_font_size = theme && theme->font_size > 0.0f ? theme->font_size : 14.0f;

    /* Strip background — a flat band the width of the widget. */
    ydebug("tabbar_paint: strip rect=(%.1f,%.1f)+%.1fx%.1f color=0x%08X", r.min.x, r.min.y, strip_w,
           strip_h, color_strip);
    struct yetty_ycore_void_result rr =
        paint_pill(ctx, r.min.x, r.min.y, strip_w, strip_h, color_strip, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: strip bg");

    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "tabbar_paint: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    int active = td->active_index;
    ydebug("tabbar_paint: active_index=%d", active);

    int idx = 0;
    struct yetty_yclass_object *prev_header = NULL;
    int prev_active = 0;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "tabbar_paint: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ycore_rectangle_result pr_res = yetty_ygui_widget_rect(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr_res, "tabbar_paint: pill rect");
        struct yetty_ycore_rectangle pr = pr_res.value;
        float pw = pr.max.x - pr.min.x;
        float ph = pr.max.y - pr.min.y;
        int is_active = (idx == active);
        uint32_t fill = is_active ? color_active : color_inactive;
        uint32_t text_color = is_active ? color_text_active : color_text_muted;

        rr = paint_pill(ctx, pr.min.x, pr.min.y, pw, ph, fill, TABBAR_PILL_RADIUS);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: pill");

        if (is_active && ph > TABBAR_ACCENT_BAR_H) {
            ydebug("tabbar_paint: accent rect=(%.1f,%.1f)+%.1fx%.1f color=0x%08X", pr.min.x,
                   pr.max.y - TABBAR_ACCENT_BAR_H, pw, TABBAR_ACCENT_BAR_H, color_accent);
            rr = paint_pill(ctx, pr.min.x, pr.max.y - TABBAR_ACCENT_BAR_H, pw, TABBAR_ACCENT_BAR_H,
                            color_accent, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: accent bar");
        }

        /* Separator between two adjacent inactive pills. The active tab
         * gets visual breathing room on both sides. */
        if (prev_header && !prev_active && !is_active) {
            struct yetty_ycore_rectangle_result ppr_res = yetty_ygui_widget_rect(prev_header);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ppr_res, "tabbar_paint: prev rect");
            struct yetty_ycore_rectangle ppr = ppr_res.value;
            float sep_x = (ppr.max.x + pr.min.x) * 0.5f - TABBAR_SEPARATOR_W * 0.5f;
            float sep_y = pr.min.y + TABBAR_SEPARATOR_INSET_Y;
            float sep_h = ph - 2.0f * TABBAR_SEPARATOR_INSET_Y;
            if (sep_h > 0.0f) {
                rr =
                    paint_pill(ctx, sep_x, sep_y, TABBAR_SEPARATOR_W, sep_h, color_separator, 0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "tabbar_paint: separator");
            }
        }

        /* Label — vertically centered, TABBAR_PILL_PAD_X from the left.
         * Elided so it never runs under the close-x band: long URLs used
         * to collide with the × and hide it. The host renders with its
         * mono MSDF font (~0.60 em advance); 0.62 is deliberately a touch
         * generous so we elide a character early rather than collide. */
        struct yetty_yclass_void_ptr_result hd_dr =
            yetty_yclass_object_data(c, header_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hd_dr, "tabbar_paint: data_get");
        struct header_data *hd = hd_dr.value;
        if (hd->label && hd->label[0]) {
            float font_size = base_font_size;
            float tx = pr.min.x + TABBAR_PILL_PAD_X;
            float ty = pr.min.y + (ph + font_size) * 0.5f - 2.0f;
            float reserved_right = td->close_cb ? TABBAR_CLOSE_W : TABBAR_PILL_PAD_X;
            float label_avail = pw - TABBAR_PILL_PAD_X - reserved_right;
            int max_glyphs = (int)(label_avail / (font_size * 0.62f));
            rr = paint_label_elided(ctx, hd->label, max_glyphs, tx, ty, text_color, font_size);
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
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "tabbar_paint: next_sibling");
        c = next_res.value;
    }

    /* New-tab "+" affordance, immediately right of the rightmost tab.
     * A divider in front of it marks the last tab's trailing edge (unless
     * that tab is active — the active tab keeps breathing room on both
     * sides, matching the between-tab separators above). */
    if (td->new_tab_cb) {
        float tabs_right = r.min.x;
        if (prev_header) {
            struct yetty_ycore_rectangle_result last_rect_res = yetty_ygui_widget_rect(prev_header);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, last_rect_res, "tabbar_paint: last tab rect");
            tabs_right = last_rect_res.value.max.x;
        }

        if (prev_header && !prev_active) {
            float sep_x = tabs_right + TABBAR_PLUS_GAP * 0.5f - TABBAR_SEPARATOR_W * 0.5f;
            float sep_y = r.min.y + TABBAR_SEPARATOR_INSET_Y;
            float sep_h = strip_h - 2.0f * TABBAR_SEPARATOR_INSET_Y;
            if (sep_h > 0.0f) {
                rr =
                    paint_pill(ctx, sep_x, sep_y, TABBAR_SEPARATOR_W, sep_h, color_separator, 0.0f);
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

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_yclass_object *tabbar,
                                                                const char *label)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_tabbar_add_tab: NULL tabbar");
    }
    struct yetty_yclass_object_ptr_result r =
        yetty_ygui_widget_add(tabbar, header_class_get().value);
    if (YETTY_IS_ERR(r)) {
        return r;
    }
    struct yetty_yclass_object *header = r.value;
    struct yetty_yclass_void_ptr_result hd_dr =
        yetty_yclass_object_data(header, header_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, hd_dr, "yetty_ygui_tabbar_add_tab: data_get");
    struct header_data *hd = hd_dr.value;
    hd->tabbar = tabbar;
    if (label) {
        struct yetty_ycore_void_result lr =
            header_set_label((struct yetty_yclass_object *)header, label);
        if (YETTY_IS_ERR(lr)) {
            yetty_ycore_error_destroy(lr.error);
        }
    }
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(tabbar);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, td_dr, "yetty_ygui_tabbar_add_tab: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    if (td->active_index < 0) {
        td->active_index = 0;
        struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(tabbar);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }
    return YETTY_OK(yetty_yclass_object_ptr, header);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_yclass_object *tabbar,
                                                            int index)
{
    if (!tabbar || index < 0) {
        return YETTY_OK_VOID();
    }
    int i = 0;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "yetty_ygui_tabbar_remove_tab: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res,
                            "yetty_ygui_tabbar_remove_tab: next_sibling");
        struct yetty_yclass_object *next = next_res.value;
        if (i == index) {
            struct yetty_ycore_void_result dr = yetty_ygui_widget_destroy(c);
            if (YETTY_IS_ERR(dr)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_remove_tab: del", dr);
            }
            struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(tabbar);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "yetty_ygui_tabbar_remove_tab: data_get");
            struct yetty_ygui_tabbar *td = td_dr.value;
            struct yetty_ycore_int_result count_res = yetty_ygui_tabbar_count(tabbar);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "yetty_ygui_tabbar_remove_tab: count");
            int n = count_res.value;
            if (td->active_index >= n) {
                td->active_index = n - 1;
            }
            return yetty_ygui_widget_set_dirty(tabbar);
        }
        c = next;
        i++;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_tabbar_set_label(struct yetty_yclass_object *tabbar,
                                                           int index, const char *label)
{
    if (!tabbar || index < 0) {
        return YETTY_OK_VOID();
    }
    int i = 0;
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "yetty_ygui_tabbar_set_label: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        if (i == index) {
            struct yetty_ycore_void_result lr =
                header_set_label((struct yetty_yclass_object *)c, label);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "yetty_ygui_tabbar_set_label: set");
            return yetty_ygui_widget_set_dirty(tabbar);
        }
        i++;
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res,
                            "yetty_ygui_tabbar_set_label: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_tabbar_count(const struct yetty_yclass_object *tabbar)
{
    if (!tabbar) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int n = 0;
    struct yetty_yclass_object_ptr_result child_res =
        yetty_ygui_widget_first_child((struct yetty_yclass_object *)tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, child_res, "yetty_ygui_tabbar_count: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        n++;
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, next_res, "yetty_ygui_tabbar_count: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK(yetty_ycore_int, n);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_tabbar_active(const struct yetty_yclass_object *tabbar)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_tabbar_active: NULL obj");
    }
    struct yetty_ygui_tabbar_ptr_result td_dr =
        yetty_ygui_tabbar_from((struct yetty_yclass_object *)tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, td_dr, "yetty_ygui_tabbar_active: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    return YETTY_OK(yetty_ycore_int, td->active_index);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_yclass_object *tabbar,
                                                            int index)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_set_active: NULL tabbar");
    }
    struct yetty_ycore_int_result count_res = yetty_ygui_tabbar_count(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, count_res, "yetty_ygui_tabbar_set_active: count");
    int n = count_res.value;
    if (n == 0) {
        return YETTY_OK_VOID();
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= n) {
        index = n - 1;
    }
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "yetty_ygui_tabbar_set_active: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    if (td->active_index == index) {
        return YETTY_OK_VOID();
    }
    td->active_index = index;
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(tabbar);
    if (YETTY_IS_ERR(dr)) {
        return dr;
    }
    struct yetty_ygui_event ev = {0};
    ev.type = YETTY_YGUI_EVENT_VALUE_CHANGED;
    ev.source = tabbar;
    ev.i0 = index;
    return yetty_ygui_widget_emit(tabbar, &ev);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_close(struct yetty_yclass_object *tabbar,
                                                              yetty_ygui_tab_close_cb cb,
                                                              void *userdata)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_set_on_close: NULL tabbar");
    }
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "yetty_ygui_tabbar_set_on_close: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    td->close_cb = cb;
    td->close_userdata = userdata;
    return yetty_ygui_widget_set_dirty(tabbar);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_new_tab(struct yetty_yclass_object *tabbar,
                                                                yetty_ygui_tab_new_cb cb,
                                                                void *userdata)
{
    if (!tabbar) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tabbar_set_on_new_tab: NULL tabbar");
    }
    struct yetty_ygui_tabbar_ptr_result td_dr = yetty_ygui_tabbar_from(tabbar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "yetty_ygui_tabbar_set_on_new_tab: data_get");
    struct yetty_ygui_tabbar *td = td_dr.value;
    td->new_tab_cb = cb;
    td->new_tab_userdata = userdata;
    return yetty_ygui_widget_set_dirty(tabbar);
}

/*-----------------------------------------------------------------------------
 * Class registration. Tabbar inherits hbox (flex-row container) and
 * overrides the constructor + paint.
 *---------------------------------------------------------------------------*/

#include "yetty/gen/impl/ygui/widgets/tabbar.c"
