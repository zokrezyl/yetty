/*
 * ygui-statusbar.c — flat strip + two text labels.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/statusbar.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG 0xFF14100Bu   /* BRAND_BG */
#define COLOR_TEXT 0xFFA8A79Fu /* BRAND_TEXT_SECONDARY */

struct [[clang::annotate("class@ygui:statusbar")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] statusbar_data {
    char *left;
    char *right;
};

[[clang::annotate("override@ygui:statusbar:constructor")]]
static struct yetty_ycore_void_result statusbar_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                            struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj,
        yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(), "yetty_ygui_statusbar_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "statusbar_constructor: super");
    struct statusbar_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(),
                                                         "yetty_ygui_statusbar_class_get"));
    d->left = NULL;
    d->right = NULL;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    if (l.height < 0.0f) {
        l.height = 24.0f;
    }
    return yetty_ygui_widget_layout_set(obj, &l);
}

[[clang::annotate("override@ygui:statusbar:destructor")]]
static struct yetty_ycore_void_result statusbar_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct statusbar_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(),
                                                         "yetty_ygui_statusbar_class_get"));
    free(d->left);
    free(d->right);
    d->left = d->right = NULL;
    return yetty_ygui_super_void(
        obj,
        yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(), "yetty_ygui_statusbar_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:statusbar:widget_paint")]]
static struct yetty_ycore_void_result statusbar_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_paint: NULL ctx");
    }
    struct statusbar_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(),
                                                         "yetty_ygui_statusbar_class_get"));
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysdf_box geom = {
        .center_x = r.min.x + w * 0.5f,
        .center_y = r.min.y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
    };
    struct yetty_ycore_void_result rr = yetty_ydraw_draw_list_add_cmd_add_box(
        ctx->ygrid_draw_list, 0u, 0u, COLOR_BG, 0u, 0.0f, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "statusbar_paint: bg");
    float fs = 12.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 2.0f;
    if (d->left && d->left[0]) {
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)d->left, .capacity = strlen(d->left), .size = strlen(d->left)};
        rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.min.x + 12.0f, ty, &tb, fs,
                                            COLOR_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "statusbar_paint: left");
    }
    if (d->right && d->right[0]) {
        size_t n = strlen(d->right);
        struct yetty_ycore_buffer tb = {.data = (uint8_t *)d->right, .capacity = n, .size = n};
        /* Right-anchor by estimating glyph width = fs * 0.55 — text
         * measurement lands in a follow-up; this is the same heuristic
         * the old toolkit used. */
        float text_w = (float)n * fs * 0.55f;
        rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.max.x - 12.0f - text_w, ty, &tb,
                                            fs, COLOR_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "statusbar_paint: right");
    }
    return YETTY_OK_VOID();
}

static char *dup_or_null(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *c = malloc(n + 1);
    if (c) {
        memcpy(c, s, n + 1);
    }
    return c;
}

struct yetty_ycore_void_result yetty_ygui_statusbar_set_left(struct yetty_ygui_object *obj,
                                                             const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_left: NULL obj");
    }
    struct statusbar_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(),
                                                         "yetty_ygui_statusbar_class_get"));
    free(d->left);
    d->left = dup_or_null(text);
    if (text && !d->left) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_left: malloc");
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_statusbar_set_right(struct yetty_ygui_object *obj,
                                                              const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_right: NULL obj");
    }
    struct statusbar_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_class_expect(yetty_ygui_statusbar_class_get(),
                                                         "yetty_ygui_statusbar_class_get"));
    free(d->right);
    d->right = dup_or_null(text);
    if (text && !d->right) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_right: malloc");
    }
    return yetty_ygui_object_set_dirty(obj);
}

#include "statusbar.gen.c"
