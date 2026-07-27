/* GENERATED — do not edit. */
/* Object API for regular class(es) `ymarkdown` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_YMARKDOWN_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_YMARKDOWN_H

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
struct yetty_ygui_ymarkdown;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YMARKDOWN_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YMARKDOWN_PTR_RESULT
struct yetty_ygui_ymarkdown_ptr_result {
    int ok;
    union {
        struct yetty_ygui_ymarkdown *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_ymarkdown_ptr_result yetty_ygui_ymarkdown_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_ymarkdown_to(struct yetty_ygui_ymarkdown *data);

struct yetty_yclass_object_ptr_result yetty_ygui_ymarkdown_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_source(struct yetty_yclass_object *obj,
                                                               const char *src, size_t len);
struct yetty_ycore_void_result yetty_ygui_ymarkdown_set_file(struct yetty_yclass_object *obj,
                                                             const char *path);

#ifdef __cplusplus
}
#endif

#endif
