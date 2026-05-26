/*
 * ygui-tooltip.h — pilot widget for the new toolkit.
 *
 * Chrome widget (figure_kind == 0). Inherits from the base widget
 * class; carries a UTF-8 label and a fixed background color. Used by
 * the framework's first end-to-end test of class registration,
 * instance alloc, data slice access, dispatch, and emit pass 2.
 */
#ifndef YETTY_YGUI_WIDGETS_TOOLTIP_H
#define YETTY_YGUI_WIDGETS_TOOLTIP_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tooltip class accessor. */
const struct yetty_ygui_class *yetty_ygui_tooltip_class_get(void);

/* Set the tooltip text. Caller's buffer is copied — the tooltip owns
 * its internal copy. Passing NULL clears the label. */
struct yetty_ycore_void_result yetty_ygui_tooltip_set_text(struct yetty_ygui_object *obj,
                                                           const char *text);

const char *yetty_ygui_tooltip_get_text(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_TOOLTIP_H */
