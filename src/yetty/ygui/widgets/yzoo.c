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

[[clang::annotate("override@ygui:yzoo:widget_emit_container")]]
static struct yetty_ycore_void_result emit_container(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj), YETTY_YFIGURE_KIND_YZOO,
                                        r.min.x, r.min.y, r.max.x, r.max.y, NULL, 0);
}


struct [[clang::annotate("class@ygui:yzoo")]]
       [[clang::annotate("parent@ygui:widget")]] yzoo_data {
    char _empty;
};

#include "yzoo.gen.c"
