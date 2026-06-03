/* GENERATED — do not edit. */
/* Public interface for regular class(es) `statusbar` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_STATUSBAR_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_STATUSBAR_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_statusbar_class_get(void);

struct yetty_ygui_object;
struct statusbar_data;
YETTY_YRESULT_DECLARE(yetty_ygui_statusbar_data_ptr, struct statusbar_data *);
struct yetty_ygui_statusbar_data_ptr_result yetty_ygui_statusbar_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_statusbar_set_left(struct yetty_ygui_object *obj, const char *text);
struct yetty_ycore_void_result yetty_ygui_statusbar_set_right(struct yetty_ygui_object *obj, const char *text);

#endif
