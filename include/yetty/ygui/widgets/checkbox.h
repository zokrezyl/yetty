/* GENERATED — do not edit. */
/* Public interface for regular class(es) `checkbox` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_CHECKBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_CHECKBOX_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_checkbox_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_checkbox_set_label(struct yetty_ygui_object *obj,
                                                             const char *label);
struct yetty_ycore_void_result yetty_ygui_checkbox_set_checked(struct yetty_ygui_object *obj,
                                                               int checked);
int yetty_ygui_checkbox_get_checked(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
