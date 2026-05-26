/*
 * ygui-popup_menu.h — floating menu with item rows + separators.
 *
 * Apps build a popup_menu once at startup, add items + separators, then
 * call open_at(x, y) to position and show. Clicking an item fires its
 * registered callback then auto-closes the menu. Clicking the same
 * trigger again toggles.
 *
 * Implementation note: popup_menus must be added as children of the
 * root widget with `layout.absolute = 1` so the layout pass leaves
 * their position alone. The widget's open_at helper sets pos_x/pos_y
 * + open flag for you; closed menus paint and hit-test as nothing.
 */
#ifndef YETTY_YGUI_WIDGETS_POPUP_MENU_H
#define YETTY_YGUI_WIDGETS_POPUP_MENU_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_ycore_void_result (*yetty_ygui_menu_item_cb)(struct yetty_ygui_object *menu,
                                                                  int item_index, void *userdata);

const struct yetty_ygui_class *yetty_ygui_popup_menu_class_get(void);

/* Add a clickable item. `label` is copied. Returns the item index. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_item(struct yetty_ygui_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata);

/* Add a separator row (1px hairline). */
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_separator(struct yetty_ygui_object *obj);

/* Open the menu at viewport coords (x, y). Resizes the menu to fit
 * the current item set + sets layout absolute pos. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_open_at(struct yetty_ygui_object *obj, float x,
                                                             float y);

struct yetty_ycore_void_result yetty_ygui_popup_menu_close(struct yetty_ygui_object *obj);

int yetty_ygui_popup_menu_is_open(const struct yetty_ygui_object *obj);

/* Convenience: returns 1 if the menu is currently open. Caller passes
 * the toggle source's (x, y). Switches open ↔ close. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_toggle_at(struct yetty_ygui_object *obj,
                                                               float x, float y);

#ifdef __cplusplus
}
#endif

#endif
