/* GENERATED — do not edit. */
/* Public interface for regular class(es) `button` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_button_class_get(void);

struct yetty_ygui_object;
struct button_data;
YETTY_YRESULT_DECLARE(yetty_ygui_button_data_ptr, struct button_data *);
struct yetty_ygui_button_data_ptr_result yetty_ygui_button_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_button_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_ygui_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_ygui_button_set_chrome_icon(struct yetty_ygui_object *obj,
                                                                 int kind);
struct yetty_ycore_const_char_ptr_result yetty_ygui_button_get_label(
    const struct yetty_ygui_object *obj);

#endif
