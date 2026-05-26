/* ygui-breadcrumbs.h — hbox of separator-divided text rows. */
#ifndef YETTY_YGUI_WIDGETS_BREADCRUMBS_H
#define YETTY_YGUI_WIDGETS_BREADCRUMBS_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_breadcrumbs_class_get(void);
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_add(struct yetty_ygui_object *obj,
                                                          const char *text);
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_clear(struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
