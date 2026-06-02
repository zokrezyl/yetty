/* GENERATED — do not edit. */
/* Public interface for regular class(es) `stepper` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_STEPPER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_STEPPER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_stepper_class_get(void);

struct yetty_ygui_object;
struct stepper_data;
YETTY_YRESULT_DECLARE(yetty_ygui_stepper_data_ptr, struct stepper_data *);
struct yetty_ygui_stepper_data_ptr_result yetty_ygui_stepper_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_stepper_add_step(struct yetty_ygui_object *obj, const char *label);
struct yetty_ycore_void_result yetty_ygui_stepper_set_current(struct yetty_ygui_object *obj, int i);

#endif
