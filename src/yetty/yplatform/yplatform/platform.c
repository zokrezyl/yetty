/*
 * yplatform/yplatform/platform.c — platform lifecycle abstraction, base class.
 *
 * A `platform` owns the host integration for one yetty process. It has exactly
 * two virtual methods, both implemented by each platform subclass
 * (glfw_platform, webasm_platform, ios_platform, android_platform):
 *
 *   platform_init(argc, argv) — create the config (passing argc/argv through to
 *                   yconfig_create), the window object, the clipboard object and
 *                   the cross-thread channels.
 *   platform_run(argc, argv)  — call platform_init(argc, argv), then start /
 *                   drive the OS event loop.
 *
 * The per-platform main (ymain/<platform>) drives the whole startup with a
 * single call: platform_run(argc, argv). run() calls init() itself, so main
 * never has to sequence the two. It has no accessors and is never reached into
 * by other code; everything each platform needs is held privately in its own
 * subclass data slice.
 *
 * Both slots are local@ — a platform is a single process-local object, never
 * proxied over RPC.
 *
 * NOT WIRED IN: no CMake entry and codegen has not been run; the existing
 * yinit/ymain bootstrap stays until this replaces it. The platform.gen.c
 * included at the foot does not exist yet.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/gpu-context.h>

/* Held by pointer, so a forward declaration is all this TU needs. */
struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;

/* The platform's bring-up results live on the base so every subclass shares one
 * shape. The data block is opaque outside this TU: the per-OS subclass producer
 * sets these during platform_run through the setters below, and yframework plus
 * the app read them back through the getters. */
struct YETTY_ANNOTATE("class@yplatform:platform") yetty_yplatform_platform {
    struct yetty_yplatform_gpu_context gpu;            /* WebGPU instance/surface + geometry */
    struct yetty_yconfig_config *config;               /* owned for run()'s duration */
    struct yetty_ycore_xthread_event_pipe *input_pipe; /* main → worker events */
    struct yetty_yclass_object *clipboard;             /* borrowed (NULL headless) */
    struct yetty_yclass_object *window_chrome;         /* borrowed (NULL headless) */
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yplatform_platform_ptr, struct yetty_yplatform_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_platform_class_get(void);
struct yetty_yplatform_platform_ptr_result yetty_yplatform_platform_from(
    struct yetty_yclass_object *obj);

/*===========================================================================
 * The two virtual slots. Each platform subclass overrides both; the base
 * defaults make a missing override a loud error.
 *=========================================================================*/

YETTY_ANNOTATE("virtual@yplatform:platform:platform_init")
YETTY_ANNOTATE("local@yplatform:platform_init")
static struct yetty_ycore_void_result platform_default_init(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv)
{
    (void)obj;
    (void)app;
    (void)argc;
    (void)argv;
    return YETTY_ERR(yetty_ycore_void, "platform_init: not implemented by base platform class");
}

YETTY_ANNOTATE("virtual@yplatform:platform:platform_run")
YETTY_ANNOTATE("local@yplatform:platform_run")
static struct yetty_ycore_void_result platform_default_run(struct yetty_yclass_object *obj,
                                                           struct yetty_yclass_object *app,
                                                           int argc, char **argv)
{
    (void)obj;
    (void)app;
    (void)argc;
    (void)argv;
    return YETTY_ERR(yetty_ycore_void, "platform_run: not implemented by base platform class");
}

/*===========================================================================
 * State accessors. The bring-up results live on the opaque base data block;
 * subclass producers (glfw/webasm/…) set them, yframework_create + the app read
 * them back. Pointer signatures throughout, so the generated header only
 * forward-declares the handle types (no struct reproduction / redefinition).
 *=========================================================================*/

/* Recover the base data slice from any platform (sub)class object. Propagates the
 * downcast error in a Result; the getters below absorb it (returning their
 * raw-pointer NULL) because their public signatures are fixed by the generated
 * API contract and cannot return a Result. */
static struct yetty_yplatform_platform_ptr_result platform_data(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_platform_ptr_result data = yetty_yplatform_platform_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yplatform_platform_ptr, data, "platform_data: data_get");
    return YETTY_OK(yetty_yplatform_platform_ptr, data.value);
}

/* Result wrappers for the pointer-returning state getters below, so the
 * obj→data downcast error propagates instead of being absorbed. Codegen
 * reproduces these into the generated header for consumers. */
YETTY_YRESULT_DECLARE(yetty_yplatform_gpu_context_const_ptr,
                      const struct yetty_yplatform_gpu_context *);
YETTY_YRESULT_DECLARE(yetty_yconfig_config_ptr, struct yetty_yconfig_config *);
YETTY_YRESULT_DECLARE(yetty_ycore_xthread_event_pipe_ptr, struct yetty_ycore_xthread_event_pipe *);

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yplatform_platform_set_gpu_context(
    struct yetty_yclass_object *obj, const struct yetty_yplatform_gpu_context *gpu)
{
    struct yetty_yplatform_platform_ptr_result data = yetty_yplatform_platform_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "platform_set_gpu_context: data_get");
    if (gpu) {
        data.value->gpu = *gpu;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yplatform_platform_set_services(
    struct yetty_yclass_object *obj, struct yetty_yconfig_config *config,
    struct yetty_ycore_xthread_event_pipe *input_pipe, struct yetty_yclass_object *clipboard,
    struct yetty_yclass_object *window_chrome)
{
    struct yetty_yplatform_platform_ptr_result data = yetty_yplatform_platform_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "platform_set_services: data_get");
    data.value->config = config;
    data.value->input_pipe = input_pipe;
    data.value->clipboard = clipboard;
    data.value->window_chrome = window_chrome;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_yplatform_gpu_context_const_ptr_result yetty_yplatform_platform_gpu_context(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_platform_ptr_result data = platform_data(obj);
    YETTY_RETURN_IF_ERR(yetty_yplatform_gpu_context_const_ptr, data, "platform_gpu_context: data");
    return YETTY_OK(yetty_yplatform_gpu_context_const_ptr, &data.value->gpu);
}

YETTY_ANNOTATE("expose")
struct yetty_yconfig_config_ptr_result yetty_yplatform_platform_config(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_platform_ptr_result data = platform_data(obj);
    YETTY_RETURN_IF_ERR(yetty_yconfig_config_ptr, data, "platform_config: data");
    return YETTY_OK(yetty_yconfig_config_ptr, data.value->config);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_xthread_event_pipe_ptr_result yetty_yplatform_platform_input_pipe(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_platform_ptr_result data = platform_data(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_xthread_event_pipe_ptr, data, "platform_input_pipe: data");
    return YETTY_OK(yetty_ycore_xthread_event_pipe_ptr, data.value->input_pipe);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_clipboard(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_platform_ptr_result data = platform_data(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data, "platform_clipboard: data");
    return YETTY_OK(yetty_yclass_object_ptr, data.value->clipboard);
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_window_chrome(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_platform_ptr_result data = platform_data(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data, "platform_window_chrome: data");
    return YETTY_OK(yetty_yclass_object_ptr, data.value->window_chrome);
}

#include "yetty/gen/impl/yplatform/yplatform/platform.c"
