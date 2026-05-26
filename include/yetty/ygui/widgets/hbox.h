/*
 * ygui-hbox.h — horizontal flex container.
 *
 * Chrome widget (figure_kind == 0). Subclass of the base widget class
 * whose constructor overrides the layout struct to direction=ROW.
 * Apps configure justify / align / gap via yetty_ygui_widget_layout_set
 * the usual way; only the direction is bolted in.
 */
#ifndef YETTY_YGUI_WIDGETS_HBOX_H
#define YETTY_YGUI_WIDGETS_HBOX_H

#include <yetty/ygui/class.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_hbox_class_get(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_HBOX_H */
