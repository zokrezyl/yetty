/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydiagram` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void);

struct yetty_ygui_object;
struct ydiagram_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ydiagram_data_ptr, struct ydiagram_data *);
struct yetty_ygui_ydiagram_data_ptr_result yetty_ygui_ydiagram_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_ydiagram_set_source(struct yetty_ygui_object *obj, const char *source);
const char *yetty_ygui_ydiagram_get_source(const struct yetty_ygui_object *obj);

#endif
