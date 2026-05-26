/* ygui-ybrowser.h — HTML viewer via ylexbor. */
#ifndef YETTY_YGUI_WIDGETS_YBROWSER_H
#define YETTY_YGUI_WIDGETS_YBROWSER_H
#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_ybrowser_class_get(void);
struct yetty_ycore_void_result yetty_ygui_ybrowser_set_html(struct yetty_ygui_object *obj,
                                                            const char *html, size_t len);
#ifdef __cplusplus
}
#endif
#endif
