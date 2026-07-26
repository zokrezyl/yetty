/* GENERATED — do not edit. */
/* Object API for regular class(es) `glfw_window_chrome` (implementation module: yplatform).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YPLATFORM_YWINDOW_CHROME_GLFW_H
#define YETTY_YCLASSGEN_API_YPLATFORM_YWINDOW_CHROME_GLFW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_xthread_event_pipe;



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_glfw_window_chrome;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GLFW_WINDOW_CHROME_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GLFW_WINDOW_CHROME_PTR_RESULT
struct yetty_yplatform_glfw_window_chrome_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_glfw_window_chrome *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yplatform_glfw_window_chrome_ptr_result yetty_yplatform_glfw_window_chrome_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_to(struct yetty_yplatform_glfw_window_chrome *data);

struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_create(struct yetty_yclass_ctx *ctx);



/* Bind the native window + main→render response pipe. Call once after create()
 * and the base window_chrome_configure(). Both borrowed. */
struct yetty_ycore_void_result yetty_yplatform_glfw_window_chrome_attach(struct yetty_yclass_object *obj, void *os_window, struct yetty_ycore_xthread_event_pipe *input_pipe);

#ifdef __cplusplus
}
#endif

#endif
