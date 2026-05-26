/*
 * ygui-menubar.c — hbox of trigger buttons that open popup_menus.
 *
 * Each menubar entry is a button; its on_click toggles the bound menu
 * just below the button's rect.
 */

#include "../internal.h"

#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/hbox.h>
#include <yetty/ygui/widgets/menubar.h>
#include <yetty/ygui/widgets/popup_menu.h>

#include <stdlib.h>

static struct yetty_ycore_void_result menubar_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_menubar_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "menubar_constructor: super");
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(obj);
    l.height = 30.0f;
    l.gap = 2.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

static struct yetty_ycore_void_result on_trigger_click(struct yetty_ygui_object *obj, void *userdata)
{
    struct yetty_ygui_object *menu = (struct yetty_ygui_object *)userdata;
    if (!menu) return YETTY_OK_VOID();
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    return yetty_ygui_popup_menu_toggle_at(menu, r.min.x, r.max.y + 2.0f);
}

struct yetty_ycore_void_result yetty_ygui_menubar_add(struct yetty_ygui_object *bar,
                                                      const char *label,
                                                      struct yetty_ygui_object *menu)
{
    if (!bar || !label) return YETTY_ERR(yetty_ycore_void, "menubar_add: NULL arg");
    struct yetty_ygui_object_ptr_result br = yetty_ygui_add(yetty_ygui_button_class_get(), bar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "menubar_add: button");
    struct yetty_ygui_object *btn = br.value;
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


static const struct yetty_ygui_op menubar_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, menubar_constructor),
};

static const struct yetty_ygui_class_descriptor menubar_desc = {
    .name = "yetty_ygui_menubar",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_menubar_class_get, &menubar_desc, menubar_ops, yetty_ygui_hbox_class_get(), NULL)
