/* GENERATED — do not edit. */
/* Public interface for regular class(es) `dialog` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DIALOG_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DIALOG_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_dialog_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_dialog_set_title(struct yetty_ygui_object *obj,
                                                           const char *title);

struct yetty_ycore_void_result yetty_ygui_dialog_open_at(struct yetty_ygui_object *obj, float x,
                                                         float y, float width, float height);
struct yetty_ycore_void_result yetty_ygui_dialog_close(struct yetty_ygui_object *obj);
int yetty_ygui_dialog_is_open(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
