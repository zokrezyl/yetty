/* GENERATED — do not edit. */
/* Public interface for regular class(es) `spinner` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SPINNER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SPINNER_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_spinner_class_get(void);

struct yetty_ygui_object;
struct spinner_data;
YETTY_YRESULT_DECLARE(yetty_ygui_spinner_data_ptr, struct spinner_data *);
struct yetty_ygui_spinner_data_ptr_result yetty_ygui_spinner_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_spinner_set_value(struct yetty_ygui_object *obj, float v);
struct yetty_ycore_void_result yetty_ygui_spinner_set_range(struct yetty_ygui_object *obj, float mn, float mx, float step);
struct yetty_ycore_float_result yetty_ygui_spinner_get_value(const struct yetty_ygui_object *obj);

#endif
