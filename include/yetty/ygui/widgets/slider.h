/*
 * ygui-slider.h — horizontal slider.
 *
 * Clickable; press at an x-coord maps to value = lerp(min, max, x/w).
 * Each press emits VALUE_CHANGED with f0 = new value.
 */
#ifndef YETTY_YGUI_WIDGETS_SLIDER_H
#define YETTY_YGUI_WIDGETS_SLIDER_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_slider_class_get(void);

struct yetty_ycore_void_result yetty_ygui_slider_set_range(struct yetty_ygui_object *obj, float min,
                                                           float max);
struct yetty_ycore_void_result yetty_ygui_slider_set_value(struct yetty_ygui_object *obj,
                                                           float value);
float yetty_ygui_slider_get_value(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif
