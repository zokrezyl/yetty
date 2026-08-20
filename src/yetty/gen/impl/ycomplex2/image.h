/* GENERATED — do not edit. */
/* Public interface for regular class(es) `image` (module: ycomplex2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YCOMPLEX2_IMAGE_H
#define YETTY_YCLASSGEN_YCOMPLEX2_IMAGE_H

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

/* set_path: the image file (PNG/JPG/… — whatever stb decodes). */
struct yetty_ycore_void_result yetty_ycomplex2_set_path(struct yetty_yclass_object *obj,
                                                        const char *path);

typedef struct yetty_ycore_void_result (*yetty_ycomplex2_set_path_fn)(struct yetty_yclass_object *,
                                                                      const char *);

struct yetty_yclass_object_ptr_result yetty_ycomplex2_image_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ycomplex2_register(void);

#ifdef __cplusplus
}
#endif

#endif
