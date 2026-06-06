/* GENERATED — do not edit. */
/* Public interface for regular class(es) `radio` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_RADIO_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_RADIO_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_radio_class_get(void);

struct yetty_ygui_object;
struct radio_data;
YETTY_YRESULT_DECLARE(yetty_ygui_radio_data_ptr, struct radio_data *);
struct yetty_ygui_radio_data_ptr_result yetty_ygui_radio_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_radio_set_label(struct yetty_ygui_object *obj, const char *label);
struct yetty_ycore_void_result yetty_ygui_radio_set_selected(struct yetty_ygui_object *obj, int s);
struct yetty_ycore_int_result yetty_ygui_radio_is_selected(const struct yetty_ygui_object *obj);

#endif
