/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YWINDOW_WINDOW_H
#define YETTY_YCLASSGEN_YPLATFORM_YWINDOW_WINDOW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base window data slice. The base is abstract — it owns no state (the native
 * handle, if any, is private to each subclass), so this slice is empty. */
struct yetty_yclass_ptr_result yetty_yplatform_window_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_window;
struct yetty_yplatform_window_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_window *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yplatform_window_ptr_result yetty_yplatform_window_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_to(struct yetty_yplatform_window *data);

struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object * obj, int width, int height, const char * title);
struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object * obj, int * width, int * height);
struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(struct yetty_yclass_object * obj, int * width, int * height);
struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(struct yetty_yclass_object * obj, float * xscale, float * yscale);
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object * obj, const char * title);
struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object * obj);
/* Create the WebGPU surface for this window. `instance` is a WGPUInstance and
 * the result value is a WGPUSurface — both passed as void* so this abstraction
 * stays free of <webgpu/webgpu.h>; the subclass casts. The native handle stays
 * private to the subclass. */
struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(struct yetty_yclass_object * obj, void * instance);

typedef struct yetty_ycore_void_result (*yetty_yplatform_window_open_fn)(struct yetty_yclass_object *, int, int, const char *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_size_fn)(struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_framebuffer_size_fn)(struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_content_scale_fn)(struct yetty_yclass_object *, float *, float *);
typedef struct yetty_ycore_int_result (*yetty_yplatform_window_should_close_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_set_title_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_void_ptr_result (*yetty_yplatform_window_create_surface_fn)(struct yetty_yclass_object *, void *);

struct yetty_yclass_object_ptr_result yetty_yplatform_window_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#ifdef __cplusplus
}
#endif

#endif
