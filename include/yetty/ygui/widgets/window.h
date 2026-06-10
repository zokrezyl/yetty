/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_window_class_get(void);

struct yetty_ygui_object;
struct window_data;
YETTY_YRESULT_DECLARE(yetty_ygui_window_data_ptr, struct window_data *);
struct yetty_ygui_window_data_ptr_result yetty_ygui_window_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_window_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ygui_object *yetty_ygui_window_body(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_window_set_title(struct yetty_ygui_object *obj,
                                                           const char *title);
struct yetty_ycore_void_result yetty_ygui_window_set_menu(struct yetty_ygui_object *obj,
                                                          struct yetty_ygui_object *menu);
struct yetty_ycore_void_result yetty_ygui_window_set_closable(struct yetty_ygui_object *obj,
                                                              int closable);

#endif
