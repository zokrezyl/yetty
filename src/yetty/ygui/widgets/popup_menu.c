/*
 * ygui-popup_menu.c — floating list of clickable items.
 *
 * The widget is positioned absolutely (layout.absolute = 1) so the
 * parent's flex layout leaves it alone; open_at sets pos_x/pos_y and
 * the open flag. Closed menus paint nothing AND have a zero rect so
 * they don't intercept hits.
 *
 * Inherits primitive_widget. The internal item array lives on the
 * widget's data slice; rendering walks it directly. No sub-widgets per
 * item — the menu is one hit-test target and routes per-row clicks
 * itself.
 */

#include "../internal.h"

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define MENU_ITEM_H 28.0f
#define MENU_SEPARATOR_H 8.0f
#define MENU_SEPARATOR_LINE 1.0f
#define MENU_PAD_X 12.0f
#define MENU_PAD_Y 6.0f
#define MENU_FONT_SIZE 14.0f

#define COLOR_BG 0xFF1F1A14u /* BRAND_BG_LIFTED */
#define COLOR_BORDER 0xFF474A36u
#define COLOR_HOVER 0xFF2C261Eu /* BRAND_BG_ROW */
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_SEP 0xFF474A36u

struct menu_item {
    char *label; /* NULL for separator */
    yetty_ygui_menu_item_cb cb;
    void *userdata;
    /* Drill / back rows repopulate the menu in place (clear → add…) and
     * must keep it OPEN after their callback. Normal action rows close
     * the menu before firing. */
    int is_drill;
};

struct [[clang::annotate("class@ygui:popup_menu")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] popup_menu_data {
    struct menu_item *items;
    int n_items;
    int cap;
    int open;
    int hover_index; /* -1 = none */
    float width;     /* fixed when set via open_at */
    char *title;     /* optional drill-menu header (set_title); may be NULL */
    int modal;       /* stored for parity; doesn't affect layout */
};

[[clang::annotate("override@ygui:popup_menu:constructor")]]
static struct yetty_ycore_void_result popup_menu_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                             struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_popup_menu_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "popup_menu_constructor: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "popup_menu_constructor: data_get");
    struct popup_menu_data *d = d_dr.value;
    d->items = NULL;
    d->n_items = 0;
    d->cap = 0;
    d->open = 0;
    d->hover_index = -1;
    d->width = 200.0f;
    d->title = NULL;
    d->modal = 0;
    /* popup_menus are absolutely positioned. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.absolute = 1;
    l.width = 0.0f;
    l.height = 0.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:popup_menu:destructor")]]
static struct yetty_ycore_void_result popup_menu_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                            struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "popup_menu_destructor: data_get");
    struct popup_menu_data *d = d_dr.value;
    for (int i = 0; i < d->n_items; ++i) {
        free(d->items[i].label);
    }
    free(d->items);
    d->items = NULL;
    free(d->title);
    d->title = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_popup_menu_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static int items_grow(struct popup_menu_data *d, int need)
{
    if (need <= d->cap) {
        return 1;
    }
    int cap = d->cap ? d->cap * 2 : 8;
    while (cap < need) {
        cap *= 2;
    }
    struct menu_item *ni = realloc(d->items, (size_t)cap * sizeof(*ni));
    if (!ni) {
        return 0;
    }
    d->items = ni;
    d->cap = cap;
    return 1;
}

static float item_row_h(int is_sep)
{
    return is_sep ? MENU_SEPARATOR_H : MENU_ITEM_H;
}

static float menu_total_h(const struct popup_menu_data *d)
{
    float h = 2.0f * MENU_PAD_Y;
    for (int i = 0; i < d->n_items; ++i) {
        h += item_row_h(d->items[i].label == NULL);
    }
    return h;
}

static float item_y(const struct popup_menu_data *d, int i)
{
    float y = MENU_PAD_Y;
    for (int k = 0; k < i; ++k) {
        y += item_row_h(d->items[k].label == NULL);
    }
    return y;
}

static int hit_item(const struct popup_menu_data *d, float local_y)
{
    if (local_y < MENU_PAD_Y) {
        return -1;
    }
    float y = MENU_PAD_Y;
    for (int k = 0; k < d->n_items; ++k) {
        float h = item_row_h(d->items[k].label == NULL);
        if (local_y >= y && local_y < y + h) {
            return d->items[k].label ? k : -1; /* separators ignored */
        }
        y += h;
    }
    return -1;
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
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

[[clang::annotate("override@ygui:popup_menu:widget_paint")]]
static struct yetty_ycore_void_result popup_menu_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj,
                                                       struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "popup_menu_paint: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (!d->open) {
        return YETTY_OK_VOID();
    }
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result rr = paint_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 6.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "popup_menu_paint: bg");

    for (int i = 0; i < d->n_items; ++i) {
        const char *label = d->items[i].label;
        float ry = r.min.y + item_y(d, i);
        if (!label) {
            float sy = ry + (MENU_SEPARATOR_H - MENU_SEPARATOR_LINE) * 0.5f;
            rr = paint_box(ctx, r.min.x + MENU_PAD_X * 0.5f, sy, w - MENU_PAD_X,
                           MENU_SEPARATOR_LINE, COLOR_SEP, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "popup_menu_paint: sep");
            continue;
        }
        if (i == d->hover_index) {
            rr = paint_box(ctx, r.min.x + 4.0f, ry, w - 8.0f, MENU_ITEM_H, COLOR_HOVER, 4.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "popup_menu_paint: hover");
        }
        float ty = ry + (MENU_ITEM_H + MENU_FONT_SIZE) * 0.5f - 3.0f;
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)label, .capacity = strlen(label), .size = strlen(label)};
        rr = yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, r.min.x + MENU_PAD_X, ty,
                                                &tb, MENU_FONT_SIZE, COLOR_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "popup_menu_paint: label");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:popup_menu:widget_on_press")]]
static struct yetty_ycore_int_result popup_menu_on_press(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj,
                                                         float x, float y, int button)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)button;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "popup_menu_on_press: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (!d->open) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    int idx = hit_item(d, y - r.min.y);
    if (idx >= 0 && idx < d->n_items && d->items[idx].label) {
        yetty_ygui_menu_item_cb cb = d->items[idx].cb;
        void *ud = d->items[idx].userdata;
        int is_drill = d->items[idx].is_drill;
        /* Action rows close the menu before firing. Drill / back rows
         * keep it open: their callback rebuilds the item list in place
         * (clear → set_title → add…), and closing first would leave the
         * repopulated menu invisible (open=0, zero rect). */
        if (!is_drill) {
            struct yetty_ycore_void_result cr = yetty_ygui_popup_menu_close(obj);
            if (YETTY_IS_ERR(cr)) {
                return YETTY_ERR(yetty_ycore_int, "popup_menu_on_press: close", cr);
            }
        }
        if (cb) {
            struct yetty_ycore_void_result fr =
                cb(NULL, (struct yetty_yclass_object *)obj, idx, ud);
            if (YETTY_IS_ERR(fr)) {
                return YETTY_ERR(yetty_ycore_int, "popup_menu_on_press: item cb", fr);
            }
        }
        /* Drill / back callbacks repopulated the (still-open) menu — its
         * row count changed, so re-measure the height (pos / width keep
         * their open_at values). */
        if (is_drill && d->open) {
            struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
            l.height = menu_total_h(d);
            struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
            if (YETTY_IS_ERR(lr)) {
                return YETTY_ERR(yetty_ycore_int, "popup_menu_on_press: re-measure", lr);
            }
            struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
            if (YETTY_IS_ERR(dr)) {
                return YETTY_ERR(yetty_ycore_int, "popup_menu_on_press: dirty", dr);
            }
        }
    }
    /* Always consume — clicks inside the menu rect must not fall
     * through to the widget below. */
    (void)x;
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:popup_menu:widget_on_motion")]]
static struct yetty_ycore_int_result popup_menu_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                          struct yetty_yclass_object *yclass_obj,
                                                          float x, float y)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)x;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "popup_menu_on_motion: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (!d->open) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    int idx = hit_item(d, y - r.min.y);
    if (idx != d->hover_index) {
        d->hover_index = idx;
        struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(dr)) {
            return YETTY_ERR(yetty_ycore_int, "popup_menu_on_motion: dirty", dr);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_item(struct yetty_ygui_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata)
{
    if (!obj || !label) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_add_item: NULL arg");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_add_item: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (!items_grow(d, d->n_items + 1)) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_add_item: realloc");
    }
    size_t n = strlen(label);
    char *copy = malloc(n + 1);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_add_item: malloc label");
    }
    memcpy(copy, label, n + 1);
    d->items[d->n_items].label = copy;
    d->items[d->n_items].cb = cb;
    d->items[d->n_items].userdata = userdata;
    d->items[d->n_items].is_drill = 0;
    d->n_items++;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_separator(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_add_separator: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_add_separator: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (!items_grow(d, d->n_items + 1)) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_add_separator: realloc");
    }
    d->items[d->n_items].label = NULL;
    d->items[d->n_items].cb = NULL;
    d->items[d->n_items].userdata = NULL;
    d->items[d->n_items].is_drill = 0;
    d->n_items++;
    return YETTY_OK_VOID();
}

/* Mark the most-recently-added item as a drill / back row (keeps the
 * menu open + re-measures after its callback). */
static struct yetty_ycore_void_result mark_last_drill(struct yetty_ygui_object *obj)
{
    struct yetty_yclass_ptr_result class_result = yetty_ygui_popup_menu_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, class_result, "mark_last_drill: class");
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "mark_last_drill: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (d->n_items > 0) {
        d->items[d->n_items - 1].is_drill = 1;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_open_at(struct yetty_ygui_object *obj, float x,
                                                             float y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_open_at: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_open_at: data_get");
    struct popup_menu_data *d = d_dr.value;
    d->open = 1;
    d->hover_index = -1;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.absolute = 1;
    l.pos_x = x;
    l.pos_y = y;
    l.width = d->width;
    l.height = menu_total_h(d);
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "popup_menu_open_at: layout");
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_close(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_close: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_close: data_get");
    struct popup_menu_data *d = d_dr.value;
    if (!d->open) {
        return YETTY_OK_VOID();
    }
    d->open = 0;
    d->hover_index = -1;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.width = 0.0f;
    l.height = 0.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "popup_menu_close: layout");
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_int_result yetty_ygui_popup_menu_is_open(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_popup_menu_is_open: invalid args");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(
        (struct yetty_ygui_object *)obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yetty_ygui_popup_menu_is_open: data_get");
    struct popup_menu_data *d = d_dr.value;
    return YETTY_OK(yetty_ycore_int, d->open);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_toggle_at(struct yetty_ygui_object *obj,
                                                               float x, float y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_toggle_at: NULL obj");
    }
    struct yetty_ycore_int_result open_r = yetty_ygui_popup_menu_is_open(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, open_r, "popup_menu_toggle_at: is_open");
    if (open_r.value) {
        return yetty_ygui_popup_menu_close(obj);
    }
    return yetty_ygui_popup_menu_open_at(obj, x, y);
}

/* Drill-down menu support. The menu has no separate submenu objects —
 * the host rebuilds the item list in place
 * (clear → set_title → set_back → add_drill_item…), so a "drill item" is
 * just a normal item whose callback repopulates the menu. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_clear(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_clear: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_clear: data_get");
    struct popup_menu_data *d = d_dr.value;
    for (int i = 0; i < d->n_items; ++i) {
        free(d->items[i].label);
    }
    d->n_items = 0;
    d->hover_index = -1;
    free(d->title);
    d->title = NULL;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_title(struct yetty_ygui_object *obj,
                                                               const char *title)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_set_title: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_set_title: data_get");
    struct popup_menu_data *d = d_dr.value;
    free(d->title);
    d->title = NULL;
    if (title) {
        size_t n = strlen(title);
        d->title = malloc(n + 1);
        if (!d->title) {
            return YETTY_ERR(yetty_ycore_void, "popup_menu_set_title: malloc");
        }
        memcpy(d->title, title, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_back(struct yetty_ygui_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata)
{
    /* A back row is just the first item; yui adds it before the drill
     * items. It repopulates the menu in place, so flag it as a drill row
     * (keeps the menu open after the callback). */
    struct yetty_ycore_void_result r =
        yetty_ygui_popup_menu_add_item(obj, label ? label : "‹ Back", cb, userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_popup_menu_set_back: add_item");
    return mark_last_drill(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_drill_item(struct yetty_ygui_object *obj,
                                                                    const char *label,
                                                                    yetty_ygui_menu_item_cb cb,
                                                                    void *userdata)
{
    struct yetty_ycore_void_result r = yetty_ygui_popup_menu_add_item(obj, label, cb, userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yetty_ygui_popup_menu_add_drill_item: add_item");
    return mark_last_drill(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_modal(struct yetty_ygui_object *obj,
                                                               int modal)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "popup_menu_set_modal: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_popup_menu_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_popup_menu_set_modal: data_get");
    struct popup_menu_data *d = d_dr.value;
    d->modal = modal ? 1 : 0;
    return YETTY_OK_VOID();
}

#include "popup_menu.gen.c"

#ifdef YCLASS_CODEGEN
typedef struct yetty_ycore_void_result (*yetty_ygui_menu_item_cb)(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *menu,
                                                                  int item_index, void *userdata);
#endif
