/* ygui-combobox.c — textinput + chevron + popup_menu. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * combobox.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_combobox_ptr, struct yetty_ygui_combobox *);
struct yetty_yclass_ptr_result yetty_ygui_combobox_class_get(void);
struct yetty_ygui_combobox_ptr_result yetty_ygui_combobox_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/popup_menu.h>
#include <stdlib.h>

#define COLOR_BG 0xFF1F1A14u
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_CHEV 0xFFA8A79Fu

struct [[clang::annotate("class@ygui:combobox")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] [[clang::annotate("uses@ygui:clickable")]]
yetty_ygui_combobox {
    char *text;
    struct yetty_yclass_object *menu;
};

static struct yetty_ycore_void_result on_pick(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, int item,
                                              void *ud)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *menu = (struct yetty_yclass_object *)yclass_obj;
    (void)menu;
    (void)item;
    (void)ud;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_click(struct yetty_yclass_ctx *yclass_ctx,
                                               struct yetty_yclass_object *yclass_obj, void *ud)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)ud;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "on_click: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    if (!d->menu) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_popup_menu_toggle_at(d->menu, r.min.x, r.max.y + 2);
}

[[clang::annotate("override@ygui:combobox:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_combobox_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "combo: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    d->text = NULL;
    d->menu = NULL;
    return yetty_ygui_clickable_on_click_set(obj, on_click, NULL);
}

[[clang::annotate("override@ygui:combobox:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    free(d->text);
    return yetty_ygui_super_void(obj, yetty_ygui_combobox_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:combobox:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "combo paint: NULL ctx");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_104 = yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_104, "combo: bg");
    float fs = 14.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    struct yetty_ycore_void_result result_108 =
        yguix_text(ctx, d->text ? d->text : "", r.min.x + 10, ty, fs, COLOR_TEXT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_108, "combo: text");
    struct yetty_ycore_void_result result_111 =
        yguix_text(ctx, "v", r.max.x - 16, ty, fs, COLOR_CHEV);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_111, "combo: chev");
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_combobox_set_text(struct yetty_yclass_object *obj,
                                                            const char *t)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "combo_set_text: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_combobox_set_text: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    free(d->text);
    d->text = NULL;
    if (t) {
        size_t n = strlen(t);
        d->text = malloc(n + 1);
        if (!d->text) {
            return YETTY_ERR(yetty_ycore_void, "combo_set_text: malloc");
        }
        memcpy(d->text, t, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_combobox_add_suggestion(struct yetty_yclass_object *obj,
                                                                  const char *t)
{
    if (!obj || !t) {
        return YETTY_ERR(yetty_ycore_void, "combo_add_suggestion: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_combobox_add_suggestion: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    if (!d->menu) {
        return YETTY_OK_VOID();
    }
    return yetty_ygui_popup_menu_add_item(d->menu, t, on_pick, obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_combobox_set_menu(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *menu)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "combo_set_menu: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_combobox_set_menu: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    d->menu = menu;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_const_char_ptr_result yetty_ygui_combobox_get_text(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_const_char_ptr, "");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(
        (struct yetty_yclass_object *)obj, yetty_ygui_combobox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr, "yetty_ygui_combobox_get_text: data_get");
    struct yetty_ygui_combobox *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->text ? d->text : "");
}

#include "combobox.gen.c"
