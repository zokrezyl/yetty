/* GENERATED — do not edit. */
/* Public interface for regular class(es) `shape` (module: ydrawlist2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YDRAWLIST2_SHAPE_H
#define YETTY_YCLASSGEN_YDRAWLIST2_SHAPE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `id` follows the wire rule: 0 = anonymous record, nonzero = addressable
 * (the type word gains the HAS_ID flag and the id word follows it). Colors
 * are 0xAARRGGBB words. */
struct yetty_yclass_ptr_result yetty_ydrawlist2_shape_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_shape;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_SHAPE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_SHAPE_PTR_RESULT
struct yetty_ydrawlist2_shape_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_shape *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_shape_ptr_result yetty_ydrawlist2_shape_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_to(
    struct yetty_ydrawlist2_shape *data);
struct uint32_result yetty_ydrawlist2_shape_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_id_set(struct yetty_yclass_object *obj,
                                                             uint32_t value);
struct uint32_result yetty_ydrawlist2_shape_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_z_set(struct yetty_yclass_object *obj,
                                                            uint32_t value);
struct uint32_result yetty_ydrawlist2_shape_fill_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_fill_set(struct yetty_yclass_object *obj,
                                                               uint32_t value);
struct uint32_result yetty_ydrawlist2_shape_stroke_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_set(struct yetty_yclass_object *obj,
                                                                 uint32_t value);
struct float_result yetty_ydrawlist2_shape_stroke_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_width_set(
    struct yetty_yclass_object *obj, float value);

/* set_fill: fill color as "#RRGGBB" / "#RRGGBBAA". */
struct yetty_ycore_void_result yetty_ydrawlist2_set_fill(struct yetty_yclass_object *obj,
                                                         const char *color);
/* set_stroke: stroke color as "#RRGGBB" / "#RRGGBBAA". */
struct yetty_ycore_void_result yetty_ydrawlist2_set_stroke(struct yetty_yclass_object *obj,
                                                           const char *color);

typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_fill_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_stroke_fn)(
    struct yetty_yclass_object *, const char *);

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ydrawlist2_register(void);

#ifdef __cplusplus
}
#endif

#endif
