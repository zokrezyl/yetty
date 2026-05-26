/*
 * ygui-dropdown.h — fixed-list selection.
 *
 * Click toggles a popup_menu with the registered options. Picking an
 * option fires VALUE_CHANGED with i0 = selected index.
 */
#ifndef YETTY_YGUI_WIDGETS_DROPDOWN_H
#define YETTY_YGUI_WIDGETS_DROPDOWN_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_dropdown_class_get(void);

/* Append an option. `label` is copied. Returns the index. */
struct yetty_ycore_void_result yetty_ygui_dropdown_add_option(struct yetty_ygui_object *obj,
                                                              const char *label);

struct yetty_ycore_void_result yetty_ygui_dropdown_set_selected(struct yetty_ygui_object *obj,
                                                                int index);
int yetty_ygui_dropdown_get_selected(const struct yetty_ygui_object *obj);

/* Bind the dropdown to its overlay menu. The menu must already be a
 * popup_menu under the root (so it paints on top). After binding, call
 * dropdown_add_option — each option is mirrored into the menu so the
 * user gets a real popup when clicking the dropdown trigger. */
struct yetty_ycore_void_result yetty_ygui_dropdown_set_menu(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_object *menu);

#ifdef __cplusplus
}
#endif

#endif
