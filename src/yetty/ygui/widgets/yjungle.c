/* ygui-yjungle.c — figure widget shell (kind = YJUNGLE). */
#include "../internal.h"
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yjungle.h>

static struct yetty_ycore_void_result emit_container(struct yetty_ygui_object *obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj),
                                        YETTY_YFIGURE_KIND_YJUNGLE, r.min.x, r.min.y, r.max.x,
                                        r.max.y, NULL, 0);
}


static const struct yetty_ygui_op yjungle_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_widget_emit_container, emit_container),
};

static const struct yetty_ygui_class_descriptor yjungle_desc = {
    .name = "yetty_ygui_yjungle",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_yjungle_class_get, &yjungle_desc, yjungle_ops, yetty_ygui_widget_class_get(), NULL)
