/*
 * ygui-toggle.h — on/off pill switch.
 */
#ifndef YETTY_YGUI_WIDGETS_TOGGLE_H
#define YETTY_YGUI_WIDGETS_TOGGLE_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_toggle_class_get(void);

struct yetty_ycore_void_result yetty_ygui_toggle_set_label(struct yetty_ygui_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_ygui_toggle_set_on(struct yetty_ygui_object *obj, int on);
int yetty_ygui_toggle_get_on(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif
