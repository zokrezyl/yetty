/* GENERATED — do not edit. */
/* Public interface for regular class(es) `datepicker` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DATEPICKER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DATEPICKER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_datepicker_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* month_0_based: 0 = January … 11 = December. */
struct yetty_ycore_void_result yetty_ygui_datepicker_set_date(struct yetty_ygui_object *obj,
                                                              int year, int month_0_based, int day);

void yetty_ygui_datepicker_get_date(const struct yetty_ygui_object *obj, int *year,
                                    int *month_0_based, int *day);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
