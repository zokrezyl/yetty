/* GENERATED — do not edit. */
/* Public interface for regular class(es) `choicebox` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_CHOICEBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_CHOICEBOX_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_choicebox_class_get(void);

struct yetty_ygui_object;
struct cb_data;
YETTY_YRESULT_DECLARE(yetty_ygui_choicebox_data_ptr, struct cb_data *);
struct yetty_ygui_choicebox_data_ptr_result yetty_ygui_choicebox_data(
    struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_choicebox_add(struct yetty_ygui_object *obj,
                                                        const char *label);
struct yetty_ycore_int_result yetty_ygui_choicebox_is_selected(const struct yetty_ygui_object *obj,
                                                               int idx);

#endif
