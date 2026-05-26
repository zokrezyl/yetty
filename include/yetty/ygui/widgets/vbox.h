/*
 * ygui-vbox.h — vertical flex container.
 *
 * Chrome widget (figure_kind == 0). Subclass of the base widget class
 * whose constructor overrides the layout struct to direction=COLUMN.
 */
#ifndef YETTY_YGUI_WIDGETS_VBOX_H
#define YETTY_YGUI_WIDGETS_VBOX_H

#include <yetty/ygui/class.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_vbox_class_get(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_VBOX_H */
