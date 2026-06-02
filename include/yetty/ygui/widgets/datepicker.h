/* GENERATED — do not edit. */
/* Public interface for regular class(es) `datepicker` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DATEPICKER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DATEPICKER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_datepicker_class_get(void);

struct yetty_ygui_object;
struct dp_data;
YETTY_YRESULT_DECLARE(yetty_ygui_datepicker_data_ptr, struct dp_data *);
struct yetty_ygui_datepicker_data_ptr_result yetty_ygui_datepicker_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_datepicker_set_date(struct yetty_ygui_object *obj, int year, int month_0_based, int day);
void yetty_ygui_datepicker_get_date(const struct yetty_ygui_object *obj, int *year, int *month_0_based, int *day);

#endif
