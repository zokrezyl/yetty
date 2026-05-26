/*
 * ygui-spinner.h — numeric stepper with - / + buttons + value display.
 */
#ifndef YETTY_YGUI_WIDGETS_SPINNER_H
#define YETTY_YGUI_WIDGETS_SPINNER_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_spinner_class_get(void);
struct yetty_ycore_void_result yetty_ygui_spinner_set_value(struct yetty_ygui_object *obj, float v);
struct yetty_ycore_void_result yetty_ygui_spinner_set_range(struct yetty_ygui_object *obj, float min,
                                                            float max, float step);
float yetty_ygui_spinner_get_value(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
