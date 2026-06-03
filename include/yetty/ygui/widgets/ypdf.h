/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ypdf` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YPDF_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YPDF_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ypdf_class_get(void);

struct yetty_ygui_object;
struct ypdf_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ypdf_data_ptr, struct ypdf_data *);
struct yetty_ygui_ypdf_data_ptr_result yetty_ygui_ypdf_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ypdf_set_file(struct yetty_ygui_object *obj, const char *path);

#endif
