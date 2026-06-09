/* GENERATED — do not edit. */
/* Public interface for regular class(es) `collapsing_header` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_COLLAPSING_HEADER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_COLLAPSING_HEADER_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_collapsing_header_class_get(void);

struct yetty_ygui_object;
struct ch_data;
YETTY_YRESULT_DECLARE(yetty_ygui_collapsing_header_data_ptr, struct ch_data *);
struct yetty_ygui_collapsing_header_data_ptr_result yetty_ygui_collapsing_header_data(
    struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_title(struct yetty_ygui_object *obj,
                                                                      const char *title);
struct yetty_ycore_void_result yetty_ygui_collapsing_header_set_open(struct yetty_ygui_object *obj,
                                                                     int open);
int yetty_ygui_collapsing_header_is_open(const struct yetty_ygui_object *obj);

#endif
