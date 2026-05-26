/* ygui-scrollarea.h — vbox container with scrollbar visual.
 * Real wheel-driven scrolling lands when the input layer grows wheel
 * events; the widget currently just shows a stylised bg + scrollbar
 * track so apps can place it where the eventual scrollable region
 * will live. App adds body children with yetty_ygui_add(child, area). */
#ifndef YETTY_YGUI_WIDGETS_SCROLLAREA_H
#define YETTY_YGUI_WIDGETS_SCROLLAREA_H
#include <yetty/ygui/class.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_scrollarea_class_get(void);
#ifdef __cplusplus
}
#endif
#endif
