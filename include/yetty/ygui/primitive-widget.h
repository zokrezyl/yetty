/* GENERATED — do not edit. */
/* Public interface for regular class(es) `primitive_widget` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_PRIMITIVE_WIDGET_H
#define YETTY_YCLASSGEN_YGUI_PRIMITIVE_WIDGET_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_primitive_widget_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_primitive_widget;
YETTY_YRESULT_DECLARE(yetty_ygui_primitive_widget_data_ptr, struct yetty_ygui_primitive_widget *);
struct yetty_ygui_primitive_widget_data_ptr_result yetty_ygui_primitive_widget_data(
    struct yetty_ygui_object *obj);

struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;

struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj,
                                                           struct yetty_ygui_emit_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_ygui_primitive_widget_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

#endif
