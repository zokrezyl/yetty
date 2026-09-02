/* GENERATED — do not edit. */
/* Object API for regular class(es) `image` (implementation module: ycomplex2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YCOMPLEX2_IMAGE_H
#define YETTY_YCLASSGEN_API_YCOMPLEX2_IMAGE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ycomplex2_image_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ycomplex2_image;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_IMAGE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_IMAGE_PTR_RESULT
struct yetty_ycomplex2_image_ptr_result {
    int ok;
    union {
        struct yetty_ycomplex2_image *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ycomplex2_image_ptr_result yetty_ycomplex2_image_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_image_to(struct yetty_ycomplex2_image *data);
struct float_result yetty_ycomplex2_image_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_image_x_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ycomplex2_image_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_image_y_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ycomplex2_image_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_image_width_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ycomplex2_image_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_image_height_set(struct yetty_yclass_object *obj,
                                                                float value);
struct yetty_ycore_int_result yetty_ycomplex2_image_layer_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_image_layer_set(struct yetty_yclass_object *obj,
                                                               int32_t value);
struct uint32_result yetty_ycomplex2_image_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_image_id_set(struct yetty_yclass_object *obj,
                                                            uint32_t value);

/* set_path: the image file (PNG/JPG/… — whatever stb decodes). */
struct yetty_ycore_void_result yetty_ycomplex2_set_path(struct yetty_yclass_object *obj,
                                                        const char *path);

struct yetty_yclass_object_ptr_result yetty_ycomplex2_image_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
