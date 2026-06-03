/* GENERATED — do not edit. */
/* Public interface for regular class(es) `label` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_LABEL_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_LABEL_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_label_class_get(void);

struct yetty_ygui_object;
struct label_data;
YETTY_YRESULT_DECLARE(yetty_ygui_label_data_ptr, struct label_data *);
struct yetty_ygui_label_data_ptr_result yetty_ygui_label_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_label_set_text(struct yetty_ygui_object *obj, const char *text);
const char *yetty_ygui_label_get_text(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_label_set_font_size(struct yetty_ygui_object *obj, float size_px);
struct yetty_ycore_void_result yetty_ygui_label_set_color(struct yetty_ygui_object *obj, struct yetty_ycore_rgba color);

#endif
