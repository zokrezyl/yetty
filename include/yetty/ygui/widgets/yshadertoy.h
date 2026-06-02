/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yshadertoy` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YSHADERTOY_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YSHADERTOY_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yshadertoy_class_get(void);

struct yetty_ygui_object;
struct yshadertoy_data;
YETTY_YRESULT_DECLARE(yetty_ygui_yshadertoy_data_ptr, struct yshadertoy_data *);
struct yetty_ygui_yshadertoy_data_ptr_result yetty_ygui_yshadertoy_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_yshadertoy_set_source(struct yetty_ygui_object *obj, const char *src, size_t len);

#endif
