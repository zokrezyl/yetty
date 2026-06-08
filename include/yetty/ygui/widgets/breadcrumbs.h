/* GENERATED — do not edit. */
/* Public interface for regular class(es) `breadcrumbs` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_BREADCRUMBS_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_BREADCRUMBS_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_class_get(void);

struct yetty_ygui_object;
struct bc_data;
YETTY_YRESULT_DECLARE(yetty_ygui_breadcrumbs_data_ptr, struct bc_data *);
struct yetty_ygui_breadcrumbs_data_ptr_result yetty_ygui_breadcrumbs_data(
    struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_breadcrumbs_add(struct yetty_ygui_object *obj,
                                                          const char *text);
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_clear(struct yetty_ygui_object *obj);

#endif
