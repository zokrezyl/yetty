/*
 * ygui-statusbar.c — flat strip + two text labels.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * statusbar.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_statusbar_ptr, struct yetty_ygui_statusbar *);
struct yetty_yclass_ptr_result yetty_ygui_statusbar_class_get(void);
struct yetty_ygui_statusbar_ptr_result yetty_ygui_statusbar_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG 0xFF14100Bu   /* BRAND_BG */
#define COLOR_TEXT 0xFFA8A79Fu /* BRAND_TEXT_SECONDARY */

struct YETTY_ANNOTATE("class@ygui:statusbar") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_statusbar {
    char *left;
    char *right;
};

YETTY_ANNOTATE("override@ygui:statusbar:constructor")
static struct yetty_ycore_void_result statusbar_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_statusbar_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "statusbar_constructor: super");
    struct yetty_ygui_statusbar_ptr_result d_dr = yetty_ygui_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "statusbar_constructor: data_get");
    struct yetty_ygui_statusbar *d = d_dr.value;
    d->left = NULL;
    d->right = NULL;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    if (l.height < 0.0f) {
        l.height = 24.0f;
    }
    return yetty_ygui_widget_layout_set(obj, &l);
}

YETTY_ANNOTATE("override@ygui:statusbar:destructor")
static struct yetty_ycore_void_result statusbar_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_statusbar_ptr_result d_dr = yetty_ygui_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "statusbar_destructor: data_get");
    struct yetty_ygui_statusbar *d = d_dr.value;
    free(d->left);
    free(d->right);
    d->left = d->right = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_statusbar_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:statusbar:widget_paint")
static struct yetty_ycore_void_result statusbar_paint(struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_paint: NULL ctx");
    }
    struct yetty_ygui_statusbar_ptr_result d_dr = yetty_ygui_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "statusbar_paint: data_get");
    struct yetty_ygui_statusbar *d = d_dr.value;
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
    struct yetty_ycore_void_result rr = yetty_ydraw_drawable_list_add_cmd_add_box(
        ctx->ygrid_drawable_list, 0u, 0u, COLOR_BG, 0u, 0.0f, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "statusbar_paint: bg");
    float fs = 12.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 2.0f;
    if (d->left && d->left[0]) {
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)d->left, .capacity = strlen(d->left), .size = strlen(d->left)};
        rr = yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, r.min.x + 12.0f, ty, &tb,
                                                fs, COLOR_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "statusbar_paint: left");
    }
    if (d->right && d->right[0]) {
        size_t n = strlen(d->right);
        struct yetty_ycore_buffer tb = {.data = (uint8_t *)d->right, .capacity = n, .size = n};
        /* Right-anchor by estimating glyph width = fs * 0.55 — text
         * measurement lands in a follow-up; this is the same heuristic
         * the old toolkit used. */
        float text_w = (float)n * fs * 0.55f;
        rr = yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, r.max.x - 12.0f - text_w,
                                                ty, &tb, fs, COLOR_TEXT, 0, -1, 0.0f);
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_statusbar_set_left(struct yetty_yclass_object *obj,
                                                             const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_left: NULL obj");
    }
    struct yetty_ygui_statusbar_ptr_result d_dr = yetty_ygui_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_statusbar_set_left: data_get");
    struct yetty_ygui_statusbar *d = d_dr.value;
    free(d->left);
    d->left = dup_or_null(text);
    if (text && !d->left) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_left: malloc");
    }
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_statusbar_set_right(struct yetty_yclass_object *obj,
                                                              const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_right: NULL obj");
    }
    struct yetty_ygui_statusbar_ptr_result d_dr = yetty_ygui_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_statusbar_set_right: data_get");
    struct yetty_ygui_statusbar *d = d_dr.value;
    free(d->right);
    d->right = dup_or_null(text);
    if (text && !d->right) {
        return YETTY_ERR(yetty_ycore_void, "statusbar_set_right: malloc");
    }
    return yetty_ygui_widget_set_dirty(obj);
}

#include "statusbar.gen.c"
