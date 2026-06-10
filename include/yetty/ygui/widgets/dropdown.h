/* GENERATED — do not edit. */
/* Public interface for regular class(es) `dropdown` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_DROPDOWN_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_DROPDOWN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_dropdown_class_get(void);

struct yetty_ygui_object;
struct dropdown_data;
YETTY_YRESULT_DECLARE(yetty_ygui_dropdown_data_ptr, struct dropdown_data *);
struct yetty_ygui_dropdown_data_ptr_result yetty_ygui_dropdown_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_dropdown_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_dropdown_add_option(struct yetty_ygui_object *obj,
                                                              const char *label);
struct yetty_ycore_void_result yetty_ygui_dropdown_set_selected(struct yetty_ygui_object *obj,
                                                                int index);
struct yetty_ycore_int_result yetty_ygui_dropdown_get_selected(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_dropdown_set_menu(struct yetty_ygui_object *obj,
                                                            struct yetty_ygui_object *menu);

#endif
