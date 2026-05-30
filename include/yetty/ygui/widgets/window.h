/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_window_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* The auto-allocated body container — add window content here. */
struct yetty_ygui_object *yetty_ygui_window_body(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_window_set_title(struct yetty_ygui_object *obj,
                                                           const char *title);

/* Attach a popup_menu opened by the title bar's hamburger button. */
struct yetty_ycore_void_result yetty_ygui_window_set_menu(struct yetty_ygui_object *obj,
                                                          struct yetty_ygui_object *menu);

/* Show a close "x" in the title bar (ImGui's Begin(&p_open) model). A
 * click on it emits YETTY_YGUI_EVENT_CLOSE on the window; react via
 * yetty_ygui_object_subscribe(window, YETTY_YGUI_EVENT_CLOSE, cb, ud).
 * The framework owns the button; the app decides what "close" means. */
struct yetty_ycore_void_result yetty_ygui_window_set_closable(struct yetty_ygui_object *obj,
                                                              int closable);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
