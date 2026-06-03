/* GENERATED — do not edit. */
/* Public interface for regular class(es) `dialog` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DIALOG_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DIALOG_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_dialog_class_get(void);

struct yetty_ygui_object;
struct dialog_data;
YETTY_YRESULT_DECLARE(yetty_ygui_dialog_data_ptr, struct dialog_data *);
struct yetty_ygui_dialog_data_ptr_result yetty_ygui_dialog_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_dialog_set_title(struct yetty_ygui_object *obj, const char *title);
struct yetty_ycore_void_result yetty_ygui_dialog_open_at(struct yetty_ygui_object *obj, float x, float y, float width, float height);
struct yetty_ycore_void_result yetty_ygui_dialog_close(struct yetty_ygui_object *obj);
int yetty_ygui_dialog_is_open(const struct yetty_ygui_object *obj);

#endif
