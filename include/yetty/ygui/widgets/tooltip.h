/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tooltip` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TOOLTIP_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TOOLTIP_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_tooltip_class_get(void);

struct yetty_ygui_object;
struct tooltip_data;
YETTY_YRESULT_DECLARE(yetty_ygui_tooltip_data_ptr, struct tooltip_data *);
struct yetty_ygui_tooltip_data_ptr_result yetty_ygui_tooltip_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_tooltip_set_text(struct yetty_ygui_object *obj,
                                                           const char *text);
struct yetty_ycore_const_char_ptr_result yetty_ygui_tooltip_get_text(
    const struct yetty_ygui_object *obj);

#endif
