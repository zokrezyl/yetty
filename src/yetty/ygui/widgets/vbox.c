/*
 * ygui-vbox.c — vertical flex container. Identical to hbox except the
 * constructor sets direction=COLUMN.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * vbox.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_vbox_ptr, struct yetty_ygui_vbox *);
struct yetty_yclass_ptr_result yetty_ygui_vbox_class_get(void);
struct yetty_ygui_vbox_ptr_result yetty_ygui_vbox_from(struct yetty_yclass_object *obj);

#include <yetty/ygui/primitive-widget.h>

[[clang::annotate("override@ygui:vbox:constructor")]]
static struct yetty_ycore_void_result vbox_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_vbox_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "vbox_constructor: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.direction = YETTY_YGUI_FLEX_COLUMN;
    /* Conventional flexbox default — children fill the cross axis
     * (here = width). */
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    return yetty_ygui_widget_layout_set(obj, &l);
}

struct [[clang::annotate("class@ygui:vbox")]] [[clang::annotate("parent@ygui:primitive_widget")]]
yetty_ygui_vbox {
    char _empty;
};

#include "vbox.gen.c"
