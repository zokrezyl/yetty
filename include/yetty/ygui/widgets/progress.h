/* GENERATED — do not edit. */
/* Public interface for regular class(es) `progress` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_PROGRESS_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_PROGRESS_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_progress_class_get(void);

struct yetty_ygui_object;
struct progress_data;
YETTY_YRESULT_DECLARE(yetty_ygui_progress_data_ptr, struct progress_data *);
struct yetty_ygui_progress_data_ptr_result yetty_ygui_progress_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_progress_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_progress_set_value(struct yetty_ygui_object *obj,
                                                             float value);
struct yetty_ycore_float_result yetty_ygui_progress_get_value(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_progress_set_accent(struct yetty_ygui_object *obj,
                                                              uint32_t color);

#endif
