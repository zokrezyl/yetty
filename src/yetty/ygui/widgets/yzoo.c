/* ygui-yzoo.c — figure widget shell (kind = YZOO).
 * The yzoo C API (yetty_yzoo_render) produces draw_list prims that the
 * receiver-side ygrid consumes. For the figure-kind path we ship the
 * rendered bytes; the standalone ygreeter would need a yzoo factory
 * to render them — same wiring as yplot/yimage but yzoo doesn't ship
 * a _factory_create() entry point. This shell is the widget-side
 * scaffolding; the receiver factory is a separate follow-up. */
#include "../internal.h"
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yzoo.h>

static struct yetty_ycore_void_result emit_container(struct yetty_ygui_object *obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YZOO,
                                        r.min.x, r.min.y, r.max.x, r.max.y, NULL, 0);
}


static const struct yetty_ygui_op yzoo_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_widget_emit_container, emit_container),
};

static const struct yetty_ygui_class_descriptor yzoo_desc = {
    .name = "yetty_ygui_yzoo",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_yzoo_class_get, &yzoo_desc, yzoo_ops, yetty_ygui_widget_class_get(), NULL)
