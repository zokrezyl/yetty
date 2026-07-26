/* GENERATED — do not edit. */
/* Object API for regular class(es) `yvideo` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_YVIDEO_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_YVIDEO_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_yvideo;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YVIDEO_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YVIDEO_PTR_RESULT
struct yetty_ygui_yvideo_ptr_result {
    int ok;
    union {
        struct yetty_ygui_yvideo *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_yvideo_ptr_result yetty_ygui_yvideo_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_yvideo_to(struct yetty_ygui_yvideo *data);

struct yetty_yclass_object_ptr_result yetty_ygui_yvideo_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_ygui_yvideo_set_bytes(struct yetty_yclass_object *obj, const uint8_t *bytes, size_t len);

#ifdef __cplusplus
}
#endif

#endif
