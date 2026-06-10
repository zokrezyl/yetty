/* GENERATED — do not edit. */
/* Public interface for regular class(es) `colorpicker` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_COLORPICKER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_COLORPICKER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_colorpicker_class_get(void);

struct yetty_ygui_object;
struct cp_data;
YETTY_YRESULT_DECLARE(yetty_ygui_colorpicker_data_ptr, struct cp_data *);
struct yetty_ygui_colorpicker_data_ptr_result yetty_ygui_colorpicker_data(
    struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_colorpicker_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_colorpicker_set_color(struct yetty_ygui_object *obj,
                                                                uint32_t c);
struct yetty_ycore_uint32_result yetty_ygui_colorpicker_get_color(
    const struct yetty_ygui_object *obj);

#endif
