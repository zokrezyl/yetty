/*
 * ygui-label.h — static text widget.
 *
 * Chrome widget (figure_kind == 0). Carries a UTF-8 text string and
 * basic typography options (size, color). The paint hook emits a
 * GLYPH record per character into the engine's ygrid body.
 */
#ifndef YETTY_YGUI_WIDGETS_LABEL_H
#define YETTY_YGUI_WIDGETS_LABEL_H

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_label_class_get(void);

struct yetty_ycore_void_result yetty_ygui_label_set_text(struct yetty_ygui_object *obj,
                                                         const char *text);

const char *yetty_ygui_label_get_text(const struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_label_set_font_size(struct yetty_ygui_object *obj,
                                                              float size_px);

struct yetty_ycore_void_result yetty_ygui_label_set_color(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rgba color);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_LABEL_H */
