/* ygui-ymarkdown.h — markdown viewer. Inherits ydraw_embed: setting
 * the source rebuilds an internal draw_list via yetty_ymarkdown_render
 * which the embed base paints. */
#ifndef YETTY_YGUI_WIDGETS_YMARKDOWN_H
#define YETTY_YGUI_WIDGETS_YMARKDOWN_H
#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_ymarkdown_class_get(void);
struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_source(struct yetty_ygui_object *obj,
                                                               const char *src, size_t len);
struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_file(struct yetty_ygui_object *obj,
                                                             const char *path);
#ifdef __cplusplus
}
#endif
#endif
