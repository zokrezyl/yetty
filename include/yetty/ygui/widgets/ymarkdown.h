/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ymarkdown` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YMARKDOWN_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YMARKDOWN_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ymarkdown_class_get(void);

struct yetty_ygui_object;
struct ymarkdown_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ymarkdown_data_ptr, struct ymarkdown_data *);
struct yetty_ygui_ymarkdown_data_ptr_result yetty_ygui_ymarkdown_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_source(struct yetty_ygui_object *obj, const char *src, size_t len);
struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_file(struct yetty_ygui_object *obj, const char *path);

#endif
