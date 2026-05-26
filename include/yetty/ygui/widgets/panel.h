/*
 * ygui-panel.h — filled / outlined rectangle container.
 *
 * Chrome widget (figure_kind == 0). Mostly identical to the base
 * widget plus a background + border color. Subclasses or apps stack
 * widgets inside it via yetty_ygui_add(child_cls, panel_obj).
 */
#ifndef YETTY_YGUI_WIDGETS_PANEL_H
#define YETTY_YGUI_WIDGETS_PANEL_H

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_panel_class_get(void);

struct yetty_ycore_void_result yetty_ygui_panel_set_bg(struct yetty_ygui_object *obj,
                                                       struct yetty_ycore_rgba color);

struct yetty_ycore_void_result yetty_ygui_panel_set_border(struct yetty_ygui_object *obj,
                                                           struct yetty_ycore_rgba color,
                                                           float width_px);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_PANEL_H */
