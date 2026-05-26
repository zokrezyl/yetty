/* ygui-splitter.h — draggable divider between two flex panes.
 * Simple visual port — the actual two-pane resize lands when widgets
 * gain a per-child grow_factor live update API. */
#ifndef YETTY_YGUI_WIDGETS_SPLITTER_H
#define YETTY_YGUI_WIDGETS_SPLITTER_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_splitter_class_get(void);
#ifdef __cplusplus
}
#endif
#endif
