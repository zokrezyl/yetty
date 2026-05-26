/*
 * ygui-vbox.c — vertical flex container. Identical to hbox except the
 * constructor sets direction=COLUMN.
 */

#include "../internal.h"

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ygui/widgets/vbox.h>

static struct yetty_ycore_void_result vbox_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_vbox_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "vbox_constructor: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.direction = YETTY_YGUI_FLEX_COLUMN;
    /* Conventional flexbox default — children fill the cross axis
     * (here = width). */
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    return yetty_ygui_widget_layout_set(obj, &l);
}

const struct yetty_ygui_class *yetty_ygui_vbox_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (cls) return cls;
    static const struct yetty_ygui_op ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, vbox_constructor),
    };
    static const struct yetty_ygui_class_descriptor desc = {.name = "yetty_ygui_vbox",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,};
    struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), yetty_ygui_primitive_widget_class_get(), NULL, 0);
    if (YETTY_IS_ERR(r)) return NULL;
    cls = r.value;
    return cls;
}
