/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydiagram` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void);

struct yetty_ygui_object;
struct ydiagram_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ydiagram_data_ptr, struct ydiagram_data *);
struct yetty_ygui_ydiagram_data_ptr_result yetty_ygui_ydiagram_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_ydiagram_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ydiagram_set_source(struct yetty_ygui_object *obj,
                                                              const char *source);
struct yetty_ycore_const_char_ptr_result yetty_ygui_ydiagram_get_source(
    const struct yetty_ygui_object *obj);

#endif
