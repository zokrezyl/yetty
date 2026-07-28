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
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "menubar_constructor: layout_get");
    struct yetty_ygui_layout l = *layout_res.value;
    l.height = 30.0f;
    l.gap = 2.0f;
    return yetty_ygui_widget_layout_set(obj, &l);
}

/* Topmost widget of the tree `node` belongs to. */
static struct yetty_yclass_object *menubar_widget_root(struct yetty_yclass_object *node)
{
    for (;;) {
        struct yetty_yclass_object_ptr_result parent_res = yetty_ygui_widget_parent(node);
        if (YETTY_IS_ERR(parent_res)) {
            yetty_ycore_error_destroy(parent_res.error);
            return node;
        }
        if (!parent_res.value) {
            return node;
        }
        node = parent_res.value;
    }
}

/* Exact class identity — `_from`/object_data is a downcast that succeeds on any
 * widget (garbage slice), so it can't test type; the minted class pointer can. */
static int menubar_object_is(struct yetty_yclass_object *obj, struct yetty_yclass_object *class_obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_yclass_object_class(obj);
    if (YETTY_IS_ERR(class_res)) {
        yetty_ycore_error_destroy(class_res.error);
        return 0;
    }
    return (struct yetty_yclass_object *)class_res.value == class_obj;
}

/* Close every open popup_menu in `node`'s subtree except `keep`. The menubar's
 * bound menus live as siblings of the bar (under the shared root), so this walks
 * the whole tree. Makes the menus mutually exclusive. */
static struct yetty_ycore_void_result menubar_close_menus_except(struct yetty_yclass_object *node,
                                                                 struct yetty_yclass_object *keep)
{
    if (!node) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_ptr_result menu_class = yetty_ygui_popup_menu_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, menu_class, "menubar_close_menus_except: menu class");
    if (node != keep && menubar_object_is(node, (struct yetty_yclass_object *)menu_class.value)) {
        struct yetty_ycore_int_result open_res = yetty_ygui_popup_menu_is_open(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "menubar_close_menus_except: is_open");
        if (open_res.value) {
            struct yetty_ycore_void_result close_res = yetty_ygui_popup_menu_close(node);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "menubar_close_menus_except: close");
        }
    }
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "menubar_close_menus_except: first_child");
    for (struct yetty_yclass_object *child = child_res.value; child;) {
        struct yetty_ycore_void_result walk_res = menubar_close_menus_except(child, keep);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_res, "menubar_close_menus_except: child");
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(child);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "menubar_close_menus_except: next");
        child = next_res.value;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_trigger_click(struct yetty_yclass_object *yclass_obj,
                                                       void *userdata)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_yclass_object *menu = (struct yetty_yclass_object *)userdata;
    if (!menu) {
        return YETTY_OK_VOID();
    }
    /* Menus are mutually exclusive. Close every open menu first; if THIS menu was
     * the open one, that closes it (a toggle). Otherwise open it fresh. */
    struct yetty_ycore_int_result was_open_res = yetty_ygui_popup_menu_is_open(menu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, was_open_res, "on_trigger_click: is_open");
    struct yetty_ycore_void_result close_res =
        menubar_close_menus_except(menubar_widget_root(obj), NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "on_trigger_click: close others");
    if (was_open_res.value) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "on_trigger_click: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    return yetty_ygui_popup_menu_open_at(menu, r.min.x, r.max.y + 2.0f);
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
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(btn);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "menubar_add: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.width = 80.0f;
        struct yetty_ycore_void_result wr = yetty_ygui_widget_layout_set(btn, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "menubar_add: btn layout");
    }
    return yetty_ygui_clickable_on_click_set(btn, on_trigger_click, menu);
}

struct YETTY_ANNOTATE("class@ygui:menubar") YETTY_ANNOTATE("parent@ygui:hbox") yetty_ygui_menubar {
    char _empty;
};

#include "yetty/gen/impl/ygui/widgets/menubar.c"
