/*
 * ygui-dialog.c — absolute-positioned panel that holds body widgets.
 *
 * Inherits vbox so app-added children stack vertically. paint draws the
 * background panel + title bar; vbox layout handles children. open_at
 * sets absolute pos + size and the open flag. Closed dialogs have zero
 * width/height + paint nothing.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/widgets/dialog.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define DIALOG_TITLE_H 32.0f
#define DIALOG_PAD 16.0f
#define COLOR_BG 0xFF1F1A14u /* BRAND_BG_LIFTED */
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TITLE_BG 0xFF14100Bu /* BRAND_BG */
#define COLOR_TITLE_TEXT 0xFFE4E5E0u

struct [[clang::annotate("class@ygui:dialog")]] [[clang::annotate("parent@ygui:vbox")]]
dialog_data {
    char *title;
    int open;
};

[[clang::annotate("override@ygui:dialog:constructor")]]
static struct yetty_ycore_void_result dialog_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                         struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_dialog_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "dialog_constructor: super");
    struct dialog_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_dialog_class_get().value);
    d->title = NULL;
    d->open = 0;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.absolute = 1;
    l.width = 0.0f;
    l.height = 0.0f;
    /* Reserve top padding for the title bar; bottom + sides for body. */
    l.padding_top = DIALOG_TITLE_H + DIALOG_PAD;
    l.padding_bottom = DIALOG_PAD;
    l.padding_left = DIALOG_PAD;
    l.padding_right = DIALOG_PAD;
    l.gap = 8.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:dialog:destructor")]]
static struct yetty_ycore_void_result dialog_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct dialog_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_dialog_class_get().value);
    free(d->title);
    d->title = NULL;
    return yetty_ygui_super_void(
        obj, yetty_ygui_dialog_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
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
        return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u, 0.0f,
                                                     &geom);
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
    return yetty_ydraw_draw_list_add_cmd_add_rounded_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
}

[[clang::annotate("override@ygui:dialog:widget_paint")]]
static struct yetty_ycore_void_result dialog_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                   struct yetty_yclass_object *yclass_obj,
                                                   struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct dialog_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_dialog_class_get().value);
    if (!d->open) {
        return YETTY_OK_VOID();
    }
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "dialog_paint: NULL ctx");
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result rr = paint_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 8.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dialog_paint: bg");
    rr = paint_box(ctx, r.min.x, r.min.y, w, DIALOG_TITLE_H, COLOR_TITLE_BG, 8.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dialog_paint: title bg");
    if (d->title && d->title[0]) {
        float fs = 16.0f;
        float ty = r.min.y + (DIALOG_TITLE_H + fs) * 0.5f - 3.0f;
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)d->title, .capacity = strlen(d->title), .size = strlen(d->title)};
        rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.min.x + DIALOG_PAD, ty, &tb, fs,
                                            COLOR_TITLE_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dialog_paint: title");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_dialog_set_title(struct yetty_ygui_object *obj,
                                                           const char *title)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "dialog_set_title: NULL obj");
    }
    struct dialog_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_dialog_class_get().value);
    free(d->title);
    if (!title) {
        d->title = NULL;
    } else {
        size_t n = strlen(title);
        d->title = malloc(n + 1);
        if (!d->title) {
            return YETTY_ERR(yetty_ycore_void, "dialog_set_title: malloc");
        }
        memcpy(d->title, title, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_dialog_open_at(struct yetty_ygui_object *obj, float x,
                                                         float y, float width, float height)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "dialog_open_at: NULL obj");
    }
    struct dialog_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_dialog_class_get().value);
    d->open = 1;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.absolute = 1;
    l.pos_x = x;
    l.pos_y = y;
    l.width = width;
    l.height = height;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "dialog_open_at: layout");
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_dialog_close(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "dialog_close: NULL obj");
    }
    struct dialog_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_dialog_class_get().value);
    if (!d->open) {
        return YETTY_OK_VOID();
    }
    d->open = 0;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.width = 0.0f;
    l.height = 0.0f;
    struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "dialog_close: layout");
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_dialog_is_open(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    struct dialog_data *d = yetty_ygui_data_get(
        (struct yetty_ygui_object *)obj,
        yetty_ygui_dialog_class_get().value);
    return d->open;
}

#include "dialog.gen.c"
