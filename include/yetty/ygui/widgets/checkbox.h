/*
 * ygui-checkbox.h — boolean input.
 *
 * Clickable. State change fires VALUE_CHANGED with i0 = 0/1.
 */
#ifndef YETTY_YGUI_WIDGETS_CHECKBOX_H
#define YETTY_YGUI_WIDGETS_CHECKBOX_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_checkbox_class_get(void);

struct yetty_ycore_void_result yetty_ygui_checkbox_set_label(struct yetty_ygui_object *obj,
                                                             const char *label);
struct yetty_ycore_void_result yetty_ygui_checkbox_set_checked(struct yetty_ygui_object *obj,
                                                               int checked);
int yetty_ygui_checkbox_get_checked(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif
