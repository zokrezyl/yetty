/*
 * primitive-widget.c — chrome-widget base class.
 *
 * Subclass of the base widget. Overrides emit_body so paint() does
 * the heavy lifting — chrome widget subclasses (label, button, panel,
 * hbox, vbox, tabbar, tooltip) only need to override paint to write
 * their SDF / glyph prim records into ctx->ygrid_draw_list.
 *
 * Figure widgets (yimage, yplot, …) do NOT inherit from this class —
 * they extend the base widget directly and override both emit_container
 * and emit_body themselves.
 */

#include "internal.h"

#include <yetty/ygui/primitive-widget.h>

static struct yetty_ycore_void_result primitive_emit_body(struct yetty_ygui_object *obj,
                                                          struct yetty_ygui_emit_ctx *ctx)
{
    return yetty_ygui_widget_paint(obj, ctx);
}

static const struct yetty_ygui_op primitive_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_widget_emit_body, primitive_emit_body),
};

static const struct yetty_ygui_class_descriptor primitive_desc = {
    .name = "yetty_ygui_primitive_widget",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_primitive_widget_class_get, &primitive_desc, primitive_ops,
                       yetty_ygui_widget_class_get(), NULL)
