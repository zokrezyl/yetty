/* ygui-combobox.h — textinput + dropdown chevron. Click chevron to
 * open a bound popup_menu with suggestions; typing filters them. */
#ifndef YETTY_YGUI_WIDGETS_COMBOBOX_H
#define YETTY_YGUI_WIDGETS_COMBOBOX_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_combobox_class_get(void);
struct yetty_ycore_void_result yetty_ygui_combobox_set_text(struct yetty_ygui_object *obj,
                                                            const char *text);
struct yetty_ycore_void_result yetty_ygui_combobox_add_suggestion(struct yetty_ygui_object *obj,
                                                                  const char *text);
struct yetty_ycore_void_result yetty_ygui_combobox_set_menu(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_object *menu);
const char *yetty_ygui_combobox_get_text(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
