/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yimage` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YIMAGE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YIMAGE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_yimage_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_yimage;
YETTY_YRESULT_DECLARE(yetty_ygui_yimage_data_ptr, struct yetty_ygui_yimage *);
struct yetty_ygui_yimage_data_ptr_result yetty_ygui_yimage_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_yimage_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len);
struct yetty_ycore_const_uint8_ptr_result yetty_ygui_yimage_bytes(
    const struct yetty_ygui_object *obj);
struct yetty_ycore_size_result yetty_ygui_yimage_bytes_len(const struct yetty_ygui_object *obj);

#endif
