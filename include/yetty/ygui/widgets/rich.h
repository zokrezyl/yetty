/* GENERATED — do not edit. */
/* Public interface for regular class(es) `rich` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_RICH_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_RICH_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_rich_class_get(void);

struct yetty_ygui_object;
struct rich_data;
YETTY_YRESULT_DECLARE(yetty_ygui_rich_data_ptr, struct rich_data *);
struct yetty_ygui_rich_data_ptr_result yetty_ygui_rich_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_rich_clear(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_rich_add_line(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_rich_add_span(struct yetty_ygui_object *obj, const char *text, float font_size, uint32_t color_rgba);

#endif
