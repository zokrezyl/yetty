/* GENERATED — do not edit. */
/* Public interface for regular class(es) `dropdown` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DROPDOWN_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DROPDOWN_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_dropdown_class_get(void);

struct yetty_ygui_object;
struct dropdown_data;
YETTY_YRESULT_DECLARE(yetty_ygui_dropdown_data_ptr, struct dropdown_data *);
struct yetty_ygui_dropdown_data_ptr_result yetty_ygui_dropdown_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_dropdown_add_option(struct yetty_ygui_object *obj, const char *label);
struct yetty_ycore_void_result yetty_ygui_dropdown_set_selected(struct yetty_ygui_object *obj, int index);
int yetty_ygui_dropdown_get_selected(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_dropdown_set_menu(struct yetty_ygui_object *obj, struct yetty_ygui_object *menu);

#endif
