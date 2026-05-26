/*
 * primitive-widget.h — base class for widgets whose visual content
 * lives as SDF / glyph prims in the framework's shared ygrid.
 *
 * The chrome widget set (label, button, hbox / vbox, panel, tabbar,
 * tooltip, …) inherits from this class. Subclasses override the
 * `paint` virtual to append ydraw prim records into the per-emit
 * draw_list carried by the emit context — primitive_widget's
 * emit_body wraps the call.
 *
 * Figure widgets (yimage, yplot, …) do NOT inherit from this class;
 * they inherit from the base widget directly and override both
 * emit_container (to mint their own receiver-side figure via
 * CREATE_CHILD) and emit_body (to ship their figure-specific bytes).
 */
#ifndef YETTY_YGUI_PRIMITIVE_WIDGET_H
#define YETTY_YGUI_PRIMITIVE_WIDGET_H

#include <yetty/ygui/class.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_primitive_widget_class_get(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_PRIMITIVE_WIDGET_H */
