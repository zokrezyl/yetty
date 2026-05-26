/* ygui-colorpicker.c — color swatch + hex label. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/colorpicker.h>
#include <stdio.h>

#define COLOR_BG 0xFF1F1A14u
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u

struct cp_data {
    uint32_t color;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_colorpicker_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "colorpicker: super");
    struct cp_data *d = yetty_ygui_data_get(obj, yetty_ygui_colorpicker_class_get());
    d->color = 0xFF92A86Bu; /* brand accent default */
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "colorpicker paint: NULL ctx");
    struct cp_data *d = yetty_ygui_data_get(obj, yetty_ygui_colorpicker_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    float sw_w = h;  /* square swatch on the left */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_box(ctx, r.min.x, r.min.y, sw_w, h, d->color, 4),
                        "colorpicker: swatch");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x + sw_w + 4, r.min.y, w - sw_w - 4, h, COLOR_BG, 4),
                        "colorpicker: text_bg");
    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", (unsigned)(d->color & 0xFFu),
             (unsigned)((d->color >> 8) & 0xFFu), (unsigned)((d->color >> 16) & 0xFFu));
    float fs = 13.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    return yguix_text(ctx, hex, r.min.x + sw_w + 12, ty, fs, COLOR_TEXT);
}

struct yetty_ycore_void_result yetty_ygui_colorpicker_set_color(struct yetty_ygui_object *obj,
                                                                uint32_t c)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "cp_set_color: NULL");
    struct cp_data *d = yetty_ygui_data_get(obj, yetty_ygui_colorpicker_class_get());
    d->color = c;
    return yetty_ygui_object_set_dirty(obj);
}

uint32_t yetty_ygui_colorpicker_get_color(const struct yetty_ygui_object *obj)
{
    if (!obj) return 0;
    return ((struct cp_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                  yetty_ygui_colorpicker_class_get()))
        ->color;
}


static const struct yetty_ygui_op colorpicker_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
};

static const struct yetty_ygui_class_descriptor colorpicker_desc = {
    .name = "yetty_ygui_colorpicker",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct cp_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_colorpicker_class_get, &colorpicker_desc, colorpicker_ops, yetty_ygui_primitive_widget_class_get(), NULL)
