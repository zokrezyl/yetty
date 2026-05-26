/*
 * ygui-statusbar.h — bottom-of-window strip with left + right text.
 */
#ifndef YETTY_YGUI_WIDGETS_STATUSBAR_H
#define YETTY_YGUI_WIDGETS_STATUSBAR_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_statusbar_class_get(void);

struct yetty_ycore_void_result yetty_ygui_statusbar_set_left(struct yetty_ygui_object *obj,
                                                             const char *text);
struct yetty_ycore_void_result yetty_ygui_statusbar_set_right(struct yetty_ygui_object *obj,
                                                              const char *text);

#ifdef __cplusplus
}
#endif

#endif
