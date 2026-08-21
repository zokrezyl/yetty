/* GENERATED — do not edit. */
/* Object API for regular class(es) `font` (implementation module: ydrawlist2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YDRAWLIST2_FONT_H
#define YETTY_YCLASSGEN_API_YDRAWLIST2_FONT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ydrawlist2_font_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_font;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_FONT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_FONT_PTR_RESULT
struct yetty_ydrawlist2_font_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_font *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_font_ptr_result yetty_ydrawlist2_font_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_to(struct yetty_ydrawlist2_font *data);
struct yetty_ycore_int_result yetty_ydrawlist2_font_font_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_font_font_id_set(struct yetty_yclass_object *obj,
                                                                 int32_t value);

/* set_name: the installed face name (e.g. "Emmentaler"). */
struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *obj,
                                                         const char *name);

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
