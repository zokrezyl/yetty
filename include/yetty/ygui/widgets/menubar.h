/*
 * ygui-menubar.h — top-of-window menu bar.
 *
 * Hbox of trigger buttons; each opens its bound popup_menu when
 * clicked. The popup_menu must already exist as a sibling under the
 * root, since it positions itself absolutely.
 */
#ifndef YETTY_YGUI_WIDGETS_MENUBAR_H
#define YETTY_YGUI_WIDGETS_MENUBAR_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_menubar_class_get(void);

/* Append a trigger button labelled `label` that toggles `menu` when
 * clicked. The menu is borrowed (caller owns lifetime). */
struct yetty_ycore_void_result yetty_ygui_menubar_add(struct yetty_ygui_object *bar,
                                                      const char *label,
                                                      struct yetty_ygui_object *menu);

#ifdef __cplusplus
}
#endif

#endif
