/* GENERATED — do not edit. */
/* Public interface for regular class(es) `list` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_LIST_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_LIST_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_list_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_list_add(struct yetty_ygui_object *obj,
                                                   const char *label);
struct yetty_ycore_void_result yetty_ygui_list_set_selected(struct yetty_ygui_object *obj,
                                                            int index);
int yetty_ygui_list_get_selected(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
