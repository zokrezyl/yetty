/* GENERATED — do not edit. */
/* Object API for regular class(es) `platform` (implementation module: yplatform).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YPLATFORM_YPLATFORM_PLATFORM_H
#define YETTY_YCLASSGEN_API_YPLATFORM_YPLATFORM_PLATFORM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yplatform_gpu_context;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCONFIG_CONFIG_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCONFIG_CONFIG_PTR_RESULT
struct yetty_yconfig_config_ptr_result {
    int ok;
    union {
        struct yetty_yconfig_config *value;
        struct yetty_ycore_error error;
    };
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCORE_XTHREAD_EVENT_PIPE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCORE_XTHREAD_EVENT_PIPE_PTR_RESULT
struct yetty_ycore_xthread_event_pipe_ptr_result {
    int ok;
    union {
        struct yetty_ycore_xthread_event_pipe *value;
        struct yetty_ycore_error error;
    };
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GPU_CONTEXT_CONST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_GPU_CONTEXT_CONST_PTR_RESULT
struct yetty_yplatform_gpu_context_const_ptr_result {
    int ok;
    union {
        const struct yetty_yplatform_gpu_context *value;
        struct yetty_ycore_error error;
    };
};
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_platform;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_PLATFORM_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_PLATFORM_PTR_RESULT
struct yetty_yplatform_platform_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_platform *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yplatform_platform_ptr_result yetty_yplatform_platform_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_to(
    struct yetty_yplatform_platform *data);

struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object *obj,
                                                             struct yetty_yclass_object *app,
                                                             int argc, char **argv);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

struct yetty_yclass_object_ptr_result yetty_yplatform_platform_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_platform_set_gpu_context(
    struct yetty_yclass_object *obj, const struct yetty_yplatform_gpu_context *gpu);
struct yetty_ycore_void_result yetty_yplatform_platform_set_services(
    struct yetty_yclass_object *obj, struct yetty_yconfig_config *config,
    struct yetty_ycore_xthread_event_pipe *input_pipe, struct yetty_yclass_object *clipboard,
    struct yetty_yclass_object *window_chrome);
struct yetty_yplatform_gpu_context_const_ptr_result yetty_yplatform_platform_gpu_context(
    struct yetty_yclass_object *obj);
struct yetty_yconfig_config_ptr_result yetty_yplatform_platform_config(
    struct yetty_yclass_object *obj);
struct yetty_ycore_xthread_event_pipe_ptr_result yetty_yplatform_platform_input_pipe(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_clipboard(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_window_chrome(
    struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
