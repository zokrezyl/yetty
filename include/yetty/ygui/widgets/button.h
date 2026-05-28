/* GENERATED — do not edit. */
/* Public interface for regular class(es) `button` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_button_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Set the button label. Caller's buffer is copied. NULL clears it. */
struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_ygui_object *obj,
                                                           const char *label);

const char *yetty_ygui_button_get_label(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
