/* GENERATED — do not edit. */
/* Public interface for regular class(es) `list` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_LIST_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_LIST_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_list_class_get(void);

struct yetty_ygui_object;
struct list_data;
YETTY_YRESULT_DECLARE(yetty_ygui_list_data_ptr, struct list_data *);
struct yetty_ygui_list_data_ptr_result yetty_ygui_list_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_list_add(struct yetty_ygui_object *obj, const char *label);
struct yetty_ycore_void_result yetty_ygui_list_set_selected(struct yetty_ygui_object *obj, int i);
struct yetty_ycore_int_result yetty_ygui_list_get_selected(const struct yetty_ygui_object *obj);

#endif
