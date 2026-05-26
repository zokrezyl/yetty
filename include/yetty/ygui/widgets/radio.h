/*
 * ygui-radio.h — single radio button. Apps build mutually-exclusive
 * groups by wiring multiple radios' VALUE_CHANGED to the same handler
 * which clears the others.
 */
#ifndef YETTY_YGUI_WIDGETS_RADIO_H
#define YETTY_YGUI_WIDGETS_RADIO_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_radio_class_get(void);
struct yetty_ycore_void_result yetty_ygui_radio_set_label(struct yetty_ygui_object *obj,
                                                          const char *label);
struct yetty_ycore_void_result yetty_ygui_radio_set_selected(struct yetty_ygui_object *obj,
                                                             int selected);
int yetty_ygui_radio_is_selected(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
