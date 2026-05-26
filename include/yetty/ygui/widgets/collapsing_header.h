/*
 * ygui-collapsing_header.h — vbox with a togglable header.
 *
 * The header strip shows the title + an open/close chevron. Click on
 * the strip toggles the open flag; when closed the body children get
 * zero height so they don't render. App adds body children with
 * yetty_ygui_add(child_cls, header_widget).
 */
#ifndef YETTY_YGUI_WIDGETS_COLLAPSING_HEADER_H
#define YETTY_YGUI_WIDGETS_COLLAPSING_HEADER_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_collapsing_header_class_get(void);
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_title(struct yetty_ygui_object *obj,
                                                                      const char *title);
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_open(struct yetty_ygui_object *obj,
                                                                     int open);
int yetty_ygui_collapsing_header_is_open(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
