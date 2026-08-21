/* GENERATED — do not edit. */
/* Object API for regular class(es) `text` (implementation module: ydrawlist2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YDRAWLIST2_TEXT_H
#define YETTY_YCLASSGEN_API_YDRAWLIST2_TEXT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ydrawlist2_text_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_text;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_TEXT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_TEXT_PTR_RESULT
struct yetty_ydrawlist2_text_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_text *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_text_ptr_result yetty_ydrawlist2_text_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_to(struct yetty_ydrawlist2_text *data);
struct float_result yetty_ydrawlist2_text_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_x_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ydrawlist2_text_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_y_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ydrawlist2_text_font_size_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_font_size_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct uint32_result yetty_ydrawlist2_text_color_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_color_set(struct yetty_yclass_object *obj,
                                                               uint32_t value);
struct uint32_result yetty_ydrawlist2_text_layer_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_layer_set(struct yetty_yclass_object *obj,
                                                               uint32_t value);
struct yetty_ycore_int_result yetty_ydrawlist2_text_font_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_font_id_set(struct yetty_yclass_object *obj,
                                                                 int32_t value);
struct float_result yetty_ydrawlist2_text_rotation_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_text_rotation_set(struct yetty_yclass_object *obj,
                                                                  float value);

/* set_body: the UTF-8 text of the run. Named `body` so the binding
 * generators treat it as the class's primary content (positional in the
 * generated constructors, like api_yplot's Function). */
struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *obj,
                                                         const char *body);

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
