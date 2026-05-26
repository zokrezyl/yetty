/*
 * ygui-separator.c — hairline divider. Paints a flat box at its rect.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/separator.h>
#include <yetty/ysdf/funcs.gen.h>

#define COLOR_BORDER 0xFF474A36u

static struct yetty_ycore_void_result separator_paint(struct yetty_ygui_object *obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "separator_paint: NULL ctx");
    }
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
    return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0u, 0u, COLOR_BORDER, 0u,
                                                 0.0f, &geom);
}

static const struct yetty_ygui_op separator_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_widget_paint, separator_paint),
};

static const struct yetty_ygui_class_descriptor separator_desc = {
    .name = "yetty_ygui_separator",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_separator_class_get, &separator_desc, separator_ops,
                        yetty_ygui_primitive_widget_class_get(), NULL)
