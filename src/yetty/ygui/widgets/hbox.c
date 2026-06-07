/*
 * ygui-hbox.c — horizontal flex container.
 *
 * No per-instance data; the only customisation vs the base widget is
 * the constructor's layout struct override (direction=ROW). Children
 * are added via yetty_ygui_add(child_cls, hbox_obj) — the layout pass
 * positions them left-to-right.
 */

#include "../internal.h"

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ygui/widgets/hbox.h>

[[clang::annotate("override@ygui:hbox:constructor")]]
static struct yetty_ycore_void_result hbox_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_hbox_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "hbox_constructor: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.direction = YETTY_YGUI_FLEX_ROW;
    /* Conventional flexbox default — children fill the cross axis
     * (here = height) unless the app overrides. Without this every
     * widget inside an hbox collapses to height 0 and paints nothing. */
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    return yetty_ygui_widget_layout_set(obj, &l);
}

struct [[clang::annotate("class@ygui:hbox")]] [[clang::annotate("parent@ygui:primitive_widget")]]
hbox_data {
    char _empty;
};

#include "hbox.gen.c"
