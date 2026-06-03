/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_window_class_get(void);

struct yetty_ygui_object;
struct window_data;
YETTY_YRESULT_DECLARE(yetty_ygui_window_data_ptr, struct window_data *);
struct yetty_ygui_window_data_ptr_result yetty_ygui_window_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ygui_object *yetty_ygui_window_body(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_window_set_title(struct yetty_ygui_object *obj, const char *title);
struct yetty_ycore_void_result yetty_ygui_window_set_menu(struct yetty_ygui_object *obj, struct yetty_ygui_object *menu);
struct yetty_ycore_void_result yetty_ygui_window_set_closable(struct yetty_ygui_object *obj, int closable);

#endif
