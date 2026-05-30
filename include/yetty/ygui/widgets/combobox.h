/* GENERATED — do not edit. */
/* Public interface for regular class(es) `combobox` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_COMBOBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_COMBOBOX_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_combobox_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_combobox_set_text(struct yetty_ygui_object *obj,
                                                            const char *text);
struct yetty_ycore_void_result yetty_ygui_combobox_add_suggestion(struct yetty_ygui_object *obj,
                                                                  const char *text);
struct yetty_ycore_void_result yetty_ygui_combobox_set_menu(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_object *menu);
const char *yetty_ygui_combobox_get_text(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
