/*
 * ygui-dropdown.c — button-style selector that opens a popup_menu.
 *
 * Inherits primitive_widget + clickable. Click toggles the bound menu
 * positioned just below the dropdown's rect. Each menu item's callback
 * updates the dropdown's selected index + fires VALUE_CHANGED.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/dropdown.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG 0xFF1F1A14u
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_CHEVRON 0xFFA8A79Fu

struct [[clang::annotate("class@ygui:dropdown")]]
       [[clang::annotate("parent@ygui:primitive_widget")]]
       [[clang::annotate("uses@ygui:clickable")]] dropdown_data {
    char **options;
    int n_options;
    int cap;
    int selected;
    struct yetty_ygui_object *menu;  /* borrowed; lives under root */
};

static struct yetty_ycore_void_result on_pick_item(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj, int item,
                                                   void *userdata)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *menu = (struct yetty_ygui_object *)_yc_obj;
    (void)menu;
    struct yetty_ygui_object *dd = (struct yetty_ygui_object *)userdata;
    struct dropdown_data *d = yetty_ygui_data_get(dd, yetty_ygui_dropdown_class_get().value);
    if (item < 0 || item >= d->n_options) return YETTY_OK_VOID();
    d->selected = item;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(dd);
    if (YETTY_IS_ERR(dr)) return dr;
    struct yetty_ygui_event ev = {0};
    ev.type = YETTY_YGUI_EVENT_VALUE_CHANGED;
    ev.source = dd;
    ev.i0 = item;
    return yetty_ygui_object_emit(dd, &ev);
}

static struct yetty_ycore_void_result on_click_open(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj, void *userdata)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    (void)userdata;
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    if (!d->menu) return YETTY_OK_VOID();
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_popup_menu_toggle_at(d->menu, r.min.x, r.max.y + 2.0f);
}

[[clang::annotate("override@ygui:dropdown:constructor")]]
static struct yetty_ycore_void_result dropdown_constructor(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_dropdown_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "dropdown_constructor: super");
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    d->options = NULL;
    d->n_options = 0;
    d->cap = 0;
    d->selected = -1;
    d->menu = NULL;
    return yetty_ygui_clickable_on_click_set(obj, on_click_open, NULL);
}

[[clang::annotate("override@ygui:dropdown:destructor")]]
static struct yetty_ycore_void_result dropdown_destructor(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    for (int i = 0; i < d->n_options; ++i) free(d->options[i]);
    free(d->options);
    d->options = NULL;
    /* Menu is borrowed; its parent owns its lifetime. */
    return yetty_ygui_super_void(obj, yetty_ygui_dropdown_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
    if (radius <= 0.0f) {
        struct yetty_ysdf_box geom = {.center_x = x + w * 0.5f, .center_y = y + h * 0.5f,
                                      .half_width = w * 0.5f, .half_height = h * 0.5f};
        return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u, 0.0f,
                                                     &geom);
    }
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

[[clang::annotate("override@ygui:dropdown:widget_paint")]]
static struct yetty_ycore_void_result dropdown_paint(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "dropdown_paint: NULL ctx");
    }
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) return YETTY_OK_VOID();
    struct yetty_ycore_void_result rr = paint_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dropdown_paint: bg");
    const char *label = "(none)";
    if (d->selected >= 0 && d->selected < d->n_options) label = d->options[d->selected];
    float fs = 14.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    struct yetty_ycore_buffer tb = {
        .data = (uint8_t *)label, .capacity = strlen(label), .size = strlen(label)};
    rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.min.x + 12.0f, ty, &tb, fs,
                                        COLOR_TEXT, 0, -1, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dropdown_paint: label");
    /* Down chevron glyph at right edge. */
    const char *chev = "v";
    struct yetty_ycore_buffer cb = {.data = (uint8_t *)chev, .capacity = 1, .size = 1};
    rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.max.x - 16.0f, ty, &cb, fs,
                                        COLOR_CHEVRON, 0, -1, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dropdown_paint: chev");
    return YETTY_OK_VOID();
}

static int options_grow(struct dropdown_data *d, int need)
{
    if (need <= d->cap) return 1;
    int cap = d->cap ? d->cap * 2 : 4;
    while (cap < need) cap *= 2;
    char **no = realloc(d->options, (size_t)cap * sizeof(*no));
    if (!no) return 0;
    d->options = no;
    d->cap = cap;
    return 1;
}

struct yetty_ycore_void_result yetty_ygui_dropdown_add_option(struct yetty_ygui_object *obj,
                                                              const char *label)
{
    if (!obj || !label) return YETTY_ERR(yetty_ycore_void, "dropdown_add_option: NULL arg");
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    if (!options_grow(d, d->n_options + 1)) {
        return YETTY_ERR(yetty_ycore_void, "dropdown_add_option: realloc");
    }
    size_t n = strlen(label);
    char *copy = malloc(n + 1);
    if (!copy) return YETTY_ERR(yetty_ycore_void, "dropdown_add_option: malloc");
    memcpy(copy, label, n + 1);
    d->options[d->n_options] = copy;
    if (d->selected < 0) d->selected = d->n_options;
    d->n_options++;
    if (d->menu) {
        struct yetty_ycore_void_result mr =
            yetty_ygui_popup_menu_add_item(d->menu, label, on_pick_item, obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "dropdown_add_option: menu item");
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_dropdown_set_selected(struct yetty_ygui_object *obj,
                                                                int index)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "dropdown_set_selected: NULL obj");
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    if (index < -1 || index >= d->n_options) return YETTY_OK_VOID();
    d->selected = index;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_dropdown_get_selected(const struct yetty_ygui_object *obj)
{
    if (!obj) return -1;
    struct dropdown_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_dropdown_class_get().value);
    return d->selected;
}

struct yetty_ycore_void_result yetty_ygui_dropdown_set_menu(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_object *menu)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "dropdown_set_menu: NULL obj");
    struct dropdown_data *d = yetty_ygui_data_get(obj, yetty_ygui_dropdown_class_get().value);
    d->menu = menu;
    /* Mirror any options that were already added before the bind. */
    if (menu) {
        for (int i = 0; i < d->n_options; ++i) {
            struct yetty_ycore_void_result mr =
                yetty_ygui_popup_menu_add_item(menu, d->options[i], on_pick_item, obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "dropdown_set_menu: prefill");
        }
    }
    return YETTY_OK_VOID();
}

#include "dropdown.gen.c"
