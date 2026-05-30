/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tooltip` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TOOLTIP_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TOOLTIP_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_tooltip_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Set the tooltip text. Caller's buffer is copied — the tooltip owns
 * its internal copy. Passing NULL clears the label. */
struct yetty_ycore_void_result yetty_ygui_tooltip_set_text(struct yetty_ygui_object *obj,
                                                           const char *text);

const char *yetty_ygui_tooltip_get_text(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
