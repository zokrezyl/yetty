/* ygui-choicebox.h — multi-selectable list. Each click toggles a row;
 * VALUE_CHANGED fires with i0 = toggled index, i1 = new selected count. */
#ifndef YETTY_YGUI_WIDGETS_CHOICEBOX_H
#define YETTY_YGUI_WIDGETS_CHOICEBOX_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_choicebox_class_get(void);
struct yetty_ycore_void_result yetty_ygui_choicebox_add(struct yetty_ygui_object *obj,
                                                        const char *label);
int yetty_ygui_choicebox_is_selected(const struct yetty_ygui_object *obj, int index);
#ifdef __cplusplus
}
#endif
#endif
