/* ygui-list.h — vertical list of selectable rows. Clicking sets the
 * selected index and emits VALUE_CHANGED with i0 = index. */
#ifndef YETTY_YGUI_WIDGETS_LIST_H
#define YETTY_YGUI_WIDGETS_LIST_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_list_class_get(void);
struct yetty_ycore_void_result yetty_ygui_list_add(struct yetty_ygui_object *obj,
                                                   const char *label);
struct yetty_ycore_void_result yetty_ygui_list_set_selected(struct yetty_ygui_object *obj,
                                                            int index);
int yetty_ygui_list_get_selected(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
