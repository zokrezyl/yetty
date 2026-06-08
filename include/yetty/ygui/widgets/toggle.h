/* GENERATED — do not edit. */
/* Public interface for regular class(es) `toggle` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TOGGLE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TOGGLE_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_toggle_class_get(void);

struct yetty_ygui_object;
struct toggle_data;
YETTY_YRESULT_DECLARE(yetty_ygui_toggle_data_ptr, struct toggle_data *);
struct yetty_ygui_toggle_data_ptr_result yetty_ygui_toggle_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_toggle_set_label(struct yetty_ygui_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_ygui_toggle_set_on(struct yetty_ygui_object *obj, int on);
struct yetty_ycore_int_result yetty_ygui_toggle_get_on(const struct yetty_ygui_object *obj);

#endif
