/* GENERATED — do not edit. */
/* Public interface for regular class(es) `dropdown` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DROPDOWN_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DROPDOWN_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_dropdown_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Append an option. `label` is copied. Returns the index. */
struct yetty_ycore_void_result yetty_ygui_dropdown_add_option(struct yetty_ygui_object *obj,
                                                              const char *label);

struct yetty_ycore_void_result yetty_ygui_dropdown_set_selected(struct yetty_ygui_object *obj,
                                                                int index);
int yetty_ygui_dropdown_get_selected(const struct yetty_ygui_object *obj);

/* Bind the dropdown to its overlay menu. The menu must already be a
 * popup_menu under the root (so it paints on top). After binding, call
 * dropdown_add_option — each option is mirrored into the menu so the
 * user gets a real popup when clicking the dropdown trigger. */
struct yetty_ycore_void_result yetty_ygui_dropdown_set_menu(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_object *menu);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
