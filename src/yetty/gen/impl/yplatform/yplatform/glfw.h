/* GENERATED — do not edit. */
/* Public interface for regular class(es) `glfw_platform` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YPLATFORM_GLFW_H
#define YETTY_YCLASSGEN_YPLATFORM_YPLATFORM_GLFW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No per-instance state: the bootstrap below owns the window / surface / pipes /
 * runtime for the duration of run(). */
struct yetty_yclass_ptr_result yetty_yplatform_glfw_platform_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_glfw_platform;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GLFW_PLATFORM_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GLFW_PLATFORM_PTR_RESULT
struct yetty_yplatform_glfw_platform_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_glfw_platform *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yplatform_glfw_platform_ptr_result yetty_yplatform_glfw_platform_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_to(struct yetty_yplatform_glfw_platform *data);

struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#ifdef __cplusplus
}
#endif

#endif
