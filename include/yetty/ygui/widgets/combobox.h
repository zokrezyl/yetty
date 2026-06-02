/* GENERATED — do not edit. */
/* Public interface for regular class(es) `combobox` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_COMBOBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_COMBOBOX_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_combobox_class_get(void);

struct yetty_ygui_object;
struct combo_data;
YETTY_YRESULT_DECLARE(yetty_ygui_combobox_data_ptr, struct combo_data *);
struct yetty_ygui_combobox_data_ptr_result yetty_ygui_combobox_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_combobox_set_text(struct yetty_ygui_object *obj, const char *t);
struct yetty_ycore_void_result yetty_ygui_combobox_add_suggestion(struct yetty_ygui_object *obj, const char *t);
struct yetty_ycore_void_result yetty_ygui_combobox_set_menu(struct yetty_ygui_object *obj, struct yetty_ygui_object *menu);
const char *yetty_ygui_combobox_get_text(const struct yetty_ygui_object *obj);

#endif
