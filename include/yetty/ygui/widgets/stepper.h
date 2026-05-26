/* ygui-stepper.h — numbered circles connected by lines, with one
 * "current" step highlighted. */
#ifndef YETTY_YGUI_WIDGETS_STEPPER_H
#define YETTY_YGUI_WIDGETS_STEPPER_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_stepper_class_get(void);
struct yetty_ycore_void_result yetty_ygui_stepper_add_step(struct yetty_ygui_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_ygui_stepper_set_current(struct yetty_ygui_object *obj,
                                                              int index);
#ifdef __cplusplus
}
#endif
#endif
