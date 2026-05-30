/* GENERATED — do not edit. */
/* Public interface for regular class(es) `progress` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_PROGRESS_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_PROGRESS_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_progress_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
#include <stdint.h>
struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_progress_set_value(struct yetty_ygui_object *obj,
                                                             float value);
float yetty_ygui_progress_get_value(const struct yetty_ygui_object *obj);

/* Override the fill colour (packed 0xAABBGGRR). 0 restores the default
 * brand fill. */
struct yetty_ycore_void_result yetty_ygui_progress_set_accent(struct yetty_ygui_object *obj,
                                                              uint32_t color);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
