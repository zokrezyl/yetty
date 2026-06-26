/*
 * ygui-checkbox.c — boolean input.
 *
 * Inherits primitive_widget + clickable mixin. The mixin's on_release
 * fires the on_click callback we install in the constructor, which
 * flips the checked bit and emits VALUE_CHANGED.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * checkbox.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_checkbox_ptr, struct yetty_ygui_checkbox *);
struct yetty_yclass_ptr_result yetty_ygui_checkbox_class_get(void);
struct yetty_ygui_checkbox_ptr_result yetty_ygui_checkbox_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG_IDLE 0xFF2C261Eu
#define COLOR_BG_CHECKED 0xFF92A86Bu /* BRAND_ACCENT */
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_CHECK 0xFF14100Bu /* BRAND_BG — dark check on bright accent */

struct YETTY_ANNOTATE("class@ygui:checkbox") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    YETTY_ANNOTATE("uses@ygui:clickable") yetty_ygui_checkbox {
    char *label;
    int checked;
};

static struct yetty_ycore_void_result on_click_toggle(struct yetty_yclass_object *yclass_obj,
                                                      void *userdata)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)userdata;
    struct yetty_ygui_checkbox_ptr_result d_dr = yetty_ygui_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "on_click_toggle: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    d->checked = !d->checked;
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return dr;
    }
    struct yetty_ygui_event ev = {0};
    ev.type = YETTY_YGUI_EVENT_VALUE_CHANGED;
    ev.source = obj;
    ev.i0 = d->checked;
    return yetty_ygui_widget_emit(obj, &ev);
}

YETTY_ANNOTATE("override@ygui:checkbox:constructor")
static struct yetty_ycore_void_result checkbox_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_checkbox_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "checkbox_constructor: super");
    struct yetty_ygui_checkbox_ptr_result d_dr = yetty_ygui_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "checkbox_constructor: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    d->label = NULL;
    d->checked = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click_toggle, NULL);
}

YETTY_ANNOTATE("override@ygui:checkbox:destructor")
static struct yetty_ycore_void_result checkbox_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_checkbox_ptr_result d_dr = yetty_ygui_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "checkbox_destructor: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    free(d->label);
    d->label = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_checkbox_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
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

YETTY_ANNOTATE("override@ygui:checkbox:widget_paint")
static struct yetty_ycore_void_result checkbox_paint(struct yetty_yclass_object *yclass_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "checkbox_paint: NULL ctx");
    }
    struct yetty_ygui_checkbox_ptr_result d_dr = yetty_ygui_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "checkbox_paint: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "checkbox_paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float h = r.max.y - r.min.y;
    float box_size = h - 4.0f;
    if (box_size < 12.0f) {
        box_size = 12.0f;
    }
    float box_y = r.min.y + (h - box_size) * 0.5f;
    uint32_t fill = d->checked ? COLOR_BG_CHECKED : COLOR_BG_IDLE;
    struct yetty_ycore_void_result rr =
        paint_box(ctx, r.min.x, box_y, box_size, box_size, fill, 3.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "checkbox_paint: box");
    if (d->checked) {
        float cx = r.min.x + box_size * 0.5f;
        float cy = box_y + box_size * 0.5f;
        float s = box_size * 0.25f;
        rr = paint_box(ctx, cx - s, cy - 1.5f, s * 2.0f, 3.0f, COLOR_CHECK, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "checkbox_paint: check");
    }
    if (d->label && d->label[0]) {
        float font_size = 14.0f;
        float tx = r.min.x + box_size + 8.0f;
        float ty = r.min.y + (h + font_size) * 0.5f - 2.0f;
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)d->label, .capacity = strlen(d->label), .size = strlen(d->label)};
        rr = yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, tx, ty, &tb, font_size,
                                                COLOR_TEXT, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "checkbox_paint: label");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_checkbox_set_label(struct yetty_yclass_object *obj,
                                                             const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "checkbox_set_label: NULL obj");
    }
    struct yetty_ygui_checkbox_ptr_result d_dr = yetty_ygui_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_checkbox_set_label: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    free(d->label);
    if (!label) {
        d->label = NULL;
    } else {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) {
            return YETTY_ERR(yetty_ycore_void, "checkbox_set_label: malloc");
        }
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_checkbox_set_checked(struct yetty_yclass_object *obj,
                                                               int checked)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "checkbox_set_checked: NULL obj");
    }
    struct yetty_ygui_checkbox_ptr_result d_dr = yetty_ygui_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_checkbox_set_checked: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    d->checked = checked ? 1 : 0;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_checkbox_get_checked(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_checkbox_get_checked: invalid args");
    }
    struct yetty_ygui_checkbox_ptr_result d_dr =
        yetty_ygui_checkbox_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yetty_ygui_checkbox_get_checked: data_get");
    struct yetty_ygui_checkbox *d = d_dr.value;
    return YETTY_OK(yetty_ycore_int, d->checked);
}

#include "checkbox.gen.c"
