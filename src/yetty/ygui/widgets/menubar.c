/*
 * ygui-menubar.c — hbox of trigger buttons that open popup_menus.
 *
 * Each menubar entry is a button; its on_click toggles the bound menu
 * just below the button's rect.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * menubar.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_menubar_ptr, struct yetty_ygui_menubar *);
struct yetty_yclass_ptr_result yetty_ygui_menubar_class_get(void);
struct yetty_ygui_menubar_ptr_result yetty_ygui_menubar_from(struct yetty_yclass_object *obj);

#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/hbox.h>
#include <yetty/ygui/widgets/popup_menu.h>

#include <stdlib.h>

YETTY_ANNOTATE("override@ygui:menubar:constructor")
static struct yetty_ycore_void_result menubar_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_menubar_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "menubar_constructor: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.height = 30.0f;
    l.gap = 2.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

static struct yetty_ycore_void_result on_trigger_click(struct yetty_yclass_object *yclass_obj,
                                                       void *userdata)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_object *menu = (struct yetty_yclass_object *)userdata;
    if (!menu) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_popup_menu_toggle_at(menu, r.min.x, r.max.y + 2.0f);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_menubar_add(struct yetty_yclass_object *bar,
                                                      const char *label,
                                                      struct yetty_yclass_object *menu)
{
    if (!bar || !label) {
        return YETTY_ERR(yetty_ycore_void, "menubar_add: NULL arg");
    }
    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(bar, yetty_ygui_button_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "menubar_add: button");
    struct yetty_yclass_object *btn = br.value;
    struct yetty_ycore_void_result lr = yetty_ygui_button_set_label(btn, label);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "menubar_add: set_label");
    {
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(btn);
        l.width = 80.0f;
        struct yetty_ycore_void_result wr = yetty_ygui_widget_layout_set(btn, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "menubar_add: btn layout");
    }
    return yetty_ygui_clickable_on_click_set(btn, on_trigger_click, menu);
}

struct YETTY_ANNOTATE("class@ygui:menubar") YETTY_ANNOTATE("parent@ygui:hbox") yetty_ygui_menubar {
    char _empty;
};

#include "menubar.gen.c"
