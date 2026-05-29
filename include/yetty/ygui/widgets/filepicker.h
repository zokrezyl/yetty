/* GENERATED — do not edit. */
/* Public interface for regular class(es) `filepicker` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_FILEPICKER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_FILEPICKER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_filepicker_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Set the browsed directory and reload its entries. */
struct yetty_ycore_void_result yetty_ygui_filepicker_set_dir(struct yetty_ygui_object *obj,
                                                             const char *path);

const char *yetty_ygui_filepicker_get_dir(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
