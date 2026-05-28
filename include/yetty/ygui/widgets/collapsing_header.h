/* GENERATED — do not edit. */
/* Public interface for regular class(es) `collapsing_header` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_COLLAPSING_HEADER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_COLLAPSING_HEADER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_collapsing_header_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_title(struct yetty_ygui_object *obj,
                                                                      const char *title);
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_open(struct yetty_ygui_object *obj,
                                                                     int open);
int yetty_ygui_collapsing_header_is_open(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
