/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yvideo` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YVIDEO_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YVIDEO_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yvideo_class_get(void);

struct yetty_ygui_object;
struct yvideo_data;
YETTY_YRESULT_DECLARE(yetty_ygui_yvideo_data_ptr, struct yvideo_data *);
struct yetty_ygui_yvideo_data_ptr_result yetty_ygui_yvideo_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_yvideo_set_bytes(struct yetty_ygui_object *obj, const uint8_t *bytes, size_t len);

#endif
