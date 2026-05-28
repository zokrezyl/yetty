/* GENERATED — do not edit. */
/* Public interface for regular class(es) `spinner` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SPINNER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SPINNER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_spinner_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_spinner_set_value(struct yetty_ygui_object *obj, float v);
struct yetty_ycore_void_result yetty_ygui_spinner_set_range(struct yetty_ygui_object *obj, float min,
                                                            float max, float step);
float yetty_ygui_spinner_get_value(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
