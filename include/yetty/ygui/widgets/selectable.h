/* ygui-selectable.h — clickable highlightable row with text. */
#ifndef YETTY_YGUI_WIDGETS_SELECTABLE_H
#define YETTY_YGUI_WIDGETS_SELECTABLE_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_selectable_class_get(void);
struct yetty_ycore_void_result yetty_ygui_selectable_set_text(struct yetty_ygui_object *obj,
                                                              const char *text);
struct yetty_ycore_void_result yetty_ygui_selectable_set_selected(struct yetty_ygui_object *obj,
                                                                  int selected);
int yetty_ygui_selectable_is_selected(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
