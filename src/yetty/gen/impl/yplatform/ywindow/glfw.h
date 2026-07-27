/* GENERATED — do not edit. */
/* Public interface for regular class(es) `glfw_window` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YWINDOW_GLFW_H
#define YETTY_YCLASSGEN_YPLATFORM_YWINDOW_GLFW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private subclass state: the native GLFW window, owned here. */
struct yetty_yclass_ptr_result yetty_yplatform_glfw_window_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_glfw_window;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GLFW_WINDOW_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GLFW_WINDOW_PTR_RESULT
struct yetty_yplatform_glfw_window_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_glfw_window *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yplatform_glfw_window_ptr_result yetty_yplatform_glfw_window_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_to(
    struct yetty_yplatform_glfw_window *data);

struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#ifdef __cplusplus
}
#endif

#endif
