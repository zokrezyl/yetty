/*
 * ygui-button.h — push button.
 *
 * Chrome widget (figure_kind == 0). Inherits the base widget class
 * and lists the clickable mixin — press/release/on_click come from
 * the mixin, free of charge.
 */
#ifndef YETTY_YGUI_WIDGETS_BUTTON_H
#define YETTY_YGUI_WIDGETS_BUTTON_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_button_class_get(void);

/* Set the button label. Caller's buffer is copied. NULL clears it. */
struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_ygui_object *obj,
                                                           const char *label);

const char *yetty_ygui_button_get_label(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_BUTTON_H */
