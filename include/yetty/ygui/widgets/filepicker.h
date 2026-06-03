/* GENERATED — do not edit. */
/* Public interface for regular class(es) `filepicker` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_FILEPICKER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_FILEPICKER_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_filepicker_class_get(void);

struct yetty_ygui_object;
struct fp_data;
YETTY_YRESULT_DECLARE(yetty_ygui_filepicker_data_ptr, struct fp_data *);
struct yetty_ygui_filepicker_data_ptr_result yetty_ygui_filepicker_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_filepicker_set_dir(struct yetty_ygui_object *obj, const char *path);
const char *yetty_ygui_filepicker_get_dir(const struct yetty_ygui_object *obj);

#endif
