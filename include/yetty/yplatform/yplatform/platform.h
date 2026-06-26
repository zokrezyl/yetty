/* GENERATED — do not edit. */
/* Public interface for regular class(es) `platform` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YPLATFORM_PLATFORM_H
#define YETTY_YCLASSGEN_YPLATFORM_YPLATFORM_PLATFORM_H

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

struct yetty_yconfig_config_ptr_result {
    int ok;
    union {
        struct yetty_yconfig_config *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ycore_xthread_event_pipe_ptr_result {
    int ok;
    union {
        struct yetty_ycore_xthread_event_pipe *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yplatform_gpu_context_const_ptr_result {
    int ok;
    union {
        const struct yetty_yplatform_gpu_context *value;
        struct yetty_ycore_error error;
    };
};

/* The platform's bring-up results live on the base so every subclass shares one
 * shape. The data block is opaque outside this TU: the per-OS subclass producer
 * sets these during platform_run through the setters below, and yframework plus
 * the app read them back through the getters. */
struct yetty_yclass_ptr_result yetty_yplatform_platform_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_platform;
struct yetty_yplatform_platform_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_platform *value;
        struct yetty_ycore_error error;
    };
};
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

typedef struct yetty_ycore_void_result (*yetty_yplatform_platform_init_fn)(
    struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);
typedef struct yetty_ycore_void_result (*yetty_yplatform_platform_run_fn)(
    struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);

struct yetty_yclass_object_ptr_result yetty_yplatform_platform_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

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
