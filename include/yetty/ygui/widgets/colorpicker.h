/* ygui-colorpicker.h — color swatch + RGB hex label.
 * Minimal port: shows the current color; doesn't yet support
 * interactive selection (would require a separate dialog). */
#ifndef YETTY_YGUI_WIDGETS_COLORPICKER_H
#define YETTY_YGUI_WIDGETS_COLORPICKER_H
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_colorpicker_class_get(void);
struct yetty_ycore_void_result yetty_ygui_colorpicker_set_color(struct yetty_ygui_object *obj,
                                                                uint32_t rgba);
uint32_t yetty_ygui_colorpicker_get_color(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
