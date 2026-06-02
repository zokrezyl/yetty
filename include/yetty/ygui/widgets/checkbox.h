/* GENERATED — do not edit. */
/* Public interface for regular class(es) `checkbox` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_CHECKBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_CHECKBOX_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_checkbox_class_get(void);

struct yetty_ygui_object;
struct checkbox_data;
YETTY_YRESULT_DECLARE(yetty_ygui_checkbox_data_ptr, struct checkbox_data *);
struct yetty_ygui_checkbox_data_ptr_result yetty_ygui_checkbox_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_checkbox_set_label(struct yetty_ygui_object *obj, const char *label);
struct yetty_ycore_void_result yetty_ygui_checkbox_set_checked(struct yetty_ygui_object *obj, int checked);
int yetty_ygui_checkbox_get_checked(const struct yetty_ygui_object *obj);

#endif
