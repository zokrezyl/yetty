/* ygui-colorpicker.c — color swatch + hex label. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * colorpicker.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_colorpicker_data_ptr, struct yetty_ygui_colorpicker *);
struct yetty_yclass_ptr_result yetty_ygui_colorpicker_class_get(void);
struct yetty_ygui_colorpicker_data_ptr_result yetty_ygui_colorpicker_data(
    struct yetty_ygui_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <stdio.h>

#define COLOR_BG 0xFF1F1A14u
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u

struct [[clang::annotate("class@ygui:colorpicker")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] yetty_ygui_colorpicker {
    uint32_t color;
};

[[clang::annotate("override@ygui:colorpicker:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_colorpicker_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "colorpicker: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_colorpicker_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_colorpicker *d = d_dr.value;
    d->color = 0xFF92A86Bu; /* brand accent default */
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:colorpicker:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "colorpicker paint: NULL ctx");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_colorpicker_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_colorpicker *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    float sw_w = h; /* square swatch on the left */
    struct yetty_ycore_void_result result_55 =
        yguix_box(ctx, r.min.x, r.min.y, sw_w, h, d->color, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_55, "colorpicker: swatch");
    struct yetty_ycore_void_result result_57 =
        yguix_box(ctx, r.min.x + sw_w + 4, r.min.y, w - sw_w - 4, h, COLOR_BG, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_57, "colorpicker: text_bg");
    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", (unsigned)(d->color & 0xFFu),
             (unsigned)((d->color >> 8) & 0xFFu), (unsigned)((d->color >> 16) & 0xFFu));
    float fs = 13.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    return yguix_text(ctx, hex, r.min.x + sw_w + 12, ty, fs, COLOR_TEXT);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_colorpicker_set_color(struct yetty_ygui_object *obj,
                                                                uint32_t c)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "cp_set_color: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_colorpicker_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_colorpicker_set_color: data_get");
    struct yetty_ygui_colorpicker *d = d_dr.value;
    d->color = c;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_uint32_result yetty_ygui_colorpicker_get_color(
    const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_uint32, "yetty_ygui_colorpicker_get_color: NULL obj");
    }
    struct yetty_ygui_void_ptr_result data_result = yetty_ygui_data_get_result(
        (struct yetty_ygui_object *)obj, yetty_ygui_colorpicker_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, data_result,
                        "yetty_ygui_colorpicker_get_color: data_get");
    return YETTY_OK(yetty_ycore_uint32,
                    ((struct yetty_ygui_colorpicker *)data_result.value)->color);
}

#include "colorpicker.gen.c"
