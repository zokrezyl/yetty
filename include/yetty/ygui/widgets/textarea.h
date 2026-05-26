/* ygui-textarea.h — multi-line text. Append-only API for now. */
#ifndef YETTY_YGUI_WIDGETS_TEXTAREA_H
#define YETTY_YGUI_WIDGETS_TEXTAREA_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_textarea_class_get(void);
struct yetty_ycore_void_result yetty_ygui_textarea_set_text(struct yetty_ygui_object *obj,
                                                            const char *text);
const char *yetty_ygui_textarea_get_text(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
