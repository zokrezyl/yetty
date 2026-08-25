/* GENERATED — do not edit. */
/* Public interface for regular class(es) `shadertoy` (module: ycomplex2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YCOMPLEX2_SHADERTOY_H
#define YETTY_YCLASSGEN_YCOMPLEX2_SHADERTOY_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ycomplex2_shadertoy_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ycomplex2_shadertoy;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_SHADERTOY_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_SHADERTOY_PTR_RESULT
struct yetty_ycomplex2_shadertoy_ptr_result {
    int ok;
    union {
        struct yetty_ycomplex2_shadertoy *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ycomplex2_shadertoy_ptr_result yetty_ycomplex2_shadertoy_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_shadertoy_to(
    struct yetty_ycomplex2_shadertoy *data);
struct float_result yetty_ycomplex2_shadertoy_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_x_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ycomplex2_shadertoy_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_y_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ycomplex2_shadertoy_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_width_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ycomplex2_shadertoy_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_height_set(struct yetty_yclass_object *obj,
                                                                    float value);

/* set_source: the WGSL shader source, inline. Empty/NULL selects the
 * receiver's built-in default shader (animated gradient). */
struct yetty_ycore_void_result yetty_ycomplex2_set_source(struct yetty_yclass_object *obj,
                                                          const char *wgsl);
/* set_wgsl_path: read the shader source from a file — the class's primary
 * content (Shadertoy("plasma.wgsl", …)); set_wgsl takes inline source. */
struct yetty_ycore_void_result yetty_ycomplex2_set_wgsl_path(struct yetty_yclass_object *obj,
                                                             const char *path);

typedef struct yetty_ycore_void_result (*yetty_ycomplex2_set_source_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_ycomplex2_set_wgsl_path_fn)(
    struct yetty_yclass_object *, const char *);

struct yetty_yclass_object_ptr_result yetty_ycomplex2_shadertoy_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ycomplex2_register(void);

#ifdef __cplusplus
}
#endif

#endif
