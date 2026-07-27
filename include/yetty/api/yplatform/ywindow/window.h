/* GENERATED — do not edit. */
/* Object API for regular class(es) `window` (implementation module: yplatform).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YPLATFORM_YWINDOW_WINDOW_H
#define YETTY_YCLASSGEN_API_YPLATFORM_YWINDOW_WINDOW_H

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
struct yetty_yplatform_window;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_WINDOW_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_WINDOW_PTR_RESULT
struct yetty_yplatform_window_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_window *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yplatform_window_ptr_result yetty_yplatform_window_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_to(
    struct yetty_yplatform_window *data);

struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object *obj,
                                                           int width, int height,
                                                           const char *title);
struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object *obj,
                                                               int *width, int *height);
struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(
    struct yetty_yclass_object *obj, int *width, int *height);
struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(
    struct yetty_yclass_object *obj, float *xscale, float *yscale);
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object *obj,
                                                                const char *title);
struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object *obj);
/* Create the WebGPU surface for this window. `instance` is a WGPUInstance and
 * the result value is a WGPUSurface — both passed as void* so this abstraction
 * stays free of <webgpu/webgpu.h>; the subclass casts. The native handle stays
 * private to the subclass. */
struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(
    struct yetty_yclass_object *obj, void *instance);

struct yetty_yclass_object_ptr_result yetty_yplatform_window_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
