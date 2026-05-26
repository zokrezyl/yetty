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

static struct yetty_ycore_void_result hbox_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_hbox_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "hbox_constructor: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.direction = YETTY_YGUI_FLEX_ROW;
    /* Conventional flexbox default — children fill the cross axis
     * (here = height) unless the app overrides. Without this every
     * widget inside an hbox collapses to height 0 and paints nothing. */
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    return yetty_ygui_widget_layout_set(obj, &l);
}


static const struct yetty_ygui_op hbox_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, hbox_constructor),
};

static const struct yetty_ygui_class_descriptor hbox_desc = {
    .name = "yetty_ygui_hbox",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_hbox_class_get, &hbox_desc, hbox_ops, yetty_ygui_primitive_widget_class_get(), NULL)
