/* GENERATED — do not edit. */
/* Public interface for regular class(es) `glfw_window_chrome` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YWINDOW_CHROME_GLFW_H
#define YETTY_YCLASSGEN_YPLATFORM_YWINDOW_CHROME_GLFW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_xthread_event_pipe;

/* Private subclass state. */
struct yetty_yclass_ptr_result yetty_yplatform_glfw_window_chrome_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_glfw_window_chrome;
struct yetty_yplatform_glfw_window_chrome_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_glfw_window_chrome *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yplatform_glfw_window_chrome_ptr_result yetty_yplatform_glfw_window_chrome_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_to(struct yetty_yplatform_glfw_window_chrome *data);

struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

/* Bind the native window + main→render response pipe. Call once after create()
 * and the base window_chrome_configure(). Both borrowed. */
struct yetty_ycore_void_result yetty_yplatform_glfw_window_chrome_attach(struct yetty_yclass_object *obj, void *os_window, struct yetty_ycore_xthread_event_pipe *input_pipe);

#ifdef __cplusplus
}
#endif

#endif
