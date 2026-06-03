/* GENERATED — do not edit. */
/* Public interface for regular class(es) `button` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_button_class_get(void);

struct yetty_ygui_object;
struct button_data;
YETTY_YRESULT_DECLARE(yetty_ygui_button_data_ptr, struct button_data *);
struct yetty_ygui_button_data_ptr_result yetty_ygui_button_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_ygui_object *obj, const char *label);
const char *yetty_ygui_button_get_label(const struct yetty_ygui_object *obj);

#endif
