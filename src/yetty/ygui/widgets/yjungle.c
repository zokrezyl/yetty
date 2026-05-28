/* ygui-yjungle.c — figure widget shell (kind = YJUNGLE). */
#include "../internal.h"
#include <yetty/yfigure/wire.h>
#include <yetty/ygui/widgets/yjungle.h>

[[clang::annotate("override@ygui:yjungle:widget_emit_container")]]
static struct yetty_ycore_void_result emit_container(struct yetty_yclass_ctx *_yc_ctx, struct yetty_yclass_object *_yc_obj,
                                                     struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_emit_ensure_child(ctx, yetty_ygui_object_id(obj),
                                        YETTY_YFIGURE_KIND_YJUNGLE, r.min.x, r.min.y, r.max.x,
                                        r.max.y, NULL, 0);
}


struct [[clang::annotate("class@ygui:yjungle")]]
       [[clang::annotate("parent@ygui:widget")]] yjungle_data {
    char _empty;
};

#include "yjungle.gen.c"
