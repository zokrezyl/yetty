/*
 * ygui-hbox.c — horizontal flex container.
 *
 * No per-instance data; the only customisation vs the base widget is
 * the constructor's layout struct override (direction=ROW). Children
 * are added via yetty_ygui_add(child_cls, hbox_obj) — the layout pass
 * positions them left-to-right.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * hbox.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_hbox_ptr, struct yetty_ygui_hbox *);
struct yetty_yclass_ptr_result yetty_ygui_hbox_class_get(void);
struct yetty_ygui_hbox_ptr_result yetty_ygui_hbox_from(struct yetty_yclass_object *obj);

#include <yetty/ygui/primitive-widget.h>

[[clang::annotate("override@ygui:hbox:constructor")]]
static struct yetty_ycore_void_result hbox_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
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
yetty_ygui_hbox {
    char _empty;
};

#include "hbox.gen.c"
