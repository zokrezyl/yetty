/*
 * yplatform/yplatform/webasm.c — WebAssembly platform subclass.
 *
 *   platform_init — create the config, the webasm_window (the canvas already
 *                   exists), the webasm_clipboard and the cross-thread channels.
 *   platform_run  — the browser owns the loop (emscripten drives it via the JS
 *                   asset preload + emscripten_set_main_loop), so run() only
 *                   hands control back; the loop registration lives in
 *                   ymain/webasm.c.
 *
 * All state private; driven only as init()→run() from ymain/webasm.c.
 *
 * NOT WIRED IN: foot include (webasm.gen.c) does not exist yet.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/ytrace/ytrace.h>

#include <webgpu/webgpu.h>

/* Sibling abstractions (forward-declared; decoupled from generated headers). */
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_window_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_clipboard_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_webasm_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe);

/* Canvas / GPU surface / framebuffer helpers (window/webasm.c, webgpu-surface/
 * webasm.c) and the HTML5 input callbacks (ymain/webasm-input.c). */
int yetty_yplatform_webasm_create_window(struct yetty_yconfig_config *config);
void yetty_yplatform_webasm_get_framebuffer_size(int *width, int *height);
WGPUSurface yetty_yplatform_webasm_create_surface(WGPUInstance instance);
void yetty_yplatform_webasm_setup_input_callbacks(struct yetty_ycore_xthread_event_pipe *pipe);

/* platform_init slot dispatcher (generated) — run() calls init() through it. */
struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object *obj,
                                                             struct yetty_yclass_object *app,
                                                             int argc, char **argv);

/* App lifecycle dispatchers (generated, yapp module) — run() drives the app. */
struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object *app,
                                               struct yetty_yclass_object *platform);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object *app,
                                              struct yetty_yclass_object *platform);

YETTY_YRESULT_DECLARE(yetty_yplatform_webasm_platform_ptr,
                      struct yetty_yplatform_webasm_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_webasm_platform_class_get(void);
struct yetty_yplatform_webasm_platform_ptr_result yetty_yplatform_webasm_platform_from(
    struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@yplatform:webasm_platform") YETTY_ANNOTATE("platform@webasm")
    YETTY_ANNOTATE("parent@yplatform:platform") yetty_yplatform_webasm_platform {
    struct yetty_yconfig_config *config;
    struct yetty_yclass_object *window;                 /* yplatform:webasm_window */
    struct yetty_yclass_object *clipboard;              /* yplatform:webasm_clipboard */
    struct yetty_ycore_xthread_event_pipe *input_pipe;  /* canvas → worker */
    struct yetty_ycore_xthread_event_pipe *output_pipe; /* worker → canvas */
    void *instance;                                     /* WGPUInstance */
    void *surface;                                      /* WGPUSurface */
};

static struct yetty_yplatform_webasm_platform *webasm_platform_data(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_webasm_platform_ptr_result data =
        yetty_yplatform_webasm_platform_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value;
}

YETTY_ANNOTATE("override@yplatform:webasm_platform:platform_init")
static struct yetty_ycore_void_result webasm_platform_init(struct yetty_yclass_object *obj,
                                                           struct yetty_yclass_object *app,
                                                           int argc, char **argv)
{
    (void)app; /* the app is driven from run(); init only brings up the platform */
    struct yetty_yplatform_webasm_platform *data = webasm_platform_data(obj);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_init: data_get");
    }

    /* MEMFS path layout the config / asset loader expect (mirrors the desktop
     * paths exported by yconfig). */
    setenv("YETTY_SHADERS_DIR", "/data/shaders", 1);
    setenv("YETTY_FONTS_DIR", "/data/fonts", 1);
    setenv("YETTY_RUNTIME_DIR", "/tmp", 1);
    setenv("YETTY_DATA_DIR", "/data", 1);
    setenv("YETTY_CONFIG_DIR", "/config", 1);

    struct yetty_yconfig_result config_res = yetty_yconfig_create(argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "webasm_platform_init: config");
    data->config = config_res.value;

    /* Bring up the canvas (#canvas element) before the WebGPU surface reads it. */
    if (!yetty_yplatform_webasm_create_window(data->config)) {
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_init: create window failed");
    }
    /* Bind the window abstraction object on top of the canvas. */
    struct yetty_yclass_object_ptr_result window_res = yetty_yplatform_webasm_window_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "webasm_platform_init: window create");
    data->window = window_res.value;

    struct yetty_yplatform_input_pipe_result input_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "webasm_platform_init: input pipe");
    data->input_pipe = input_res.value;
    struct yetty_yplatform_input_pipe_result output_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, output_res, "webasm_platform_init: output pipe");
    data->output_pipe = output_res.value;

    /* HTML5 keyboard/mouse/wheel/resize callbacks → input pipe. */
    yetty_yplatform_webasm_setup_input_callbacks(data->input_pipe);

    /* No clipboard object on the web build: the webasm_clipboard subclass's
     * navigator.clipboard EM_JS glue is not yet wired, and the previous webasm
     * bootstrap also ran with clipboard == NULL. Leave data->clipboard unset. */

    /* WebGPU instance + surface (the canvas exists now). TimedWaitAny
     * lets synchronous-looking code block on a WGPUFuture through the
     * port's Asyncify machinery — yetty_ywebgpu_error_scope_pop uses it
     * so a validation error surfaces inline as a Result instead of
     * being demoted to an async log line. */
    static const WGPUInstanceFeatureName instance_features[] = {
        WGPUInstanceFeatureName_TimedWaitAny,
    };
    WGPUInstanceDescriptor instance_desc = {0};
    instance_desc.requiredFeatureCount = sizeof(instance_features) / sizeof(instance_features[0]);
    instance_desc.requiredFeatures = instance_features;
    WGPUInstance instance = wgpuCreateInstance(&instance_desc);
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_init: WebGPU instance create failed");
    }
    WGPUSurface surface = yetty_yplatform_webasm_create_surface(instance);
    if (!surface) {
        wgpuInstanceRelease(instance);
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_init: WebGPU surface create failed");
    }
    data->instance = instance;
    data->surface = surface;

    ydebug("webasm_platform: init complete");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:webasm_platform:platform_run")
static struct yetty_ycore_void_result webasm_platform_run(struct yetty_yclass_object *obj,
                                                          struct yetty_yclass_object *app, int argc,
                                                          char **argv)
{
    if (!app) {
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_run: app object is required");
    }
    struct yetty_ycore_void_result init_res = yetty_yplatform_platform_init(obj, app, argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, init_res, "webasm_platform_run: init");

    struct yetty_yplatform_webasm_platform *data = webasm_platform_data(obj);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_run: data_get");
    }

    /* Mirror the bring-up state onto the base platform object, then drive the
     * app on the main thread. Single-threaded on the web: the app's run()
     * registers the emscripten main loop and hands back; the browser drives
     * frames thereafter. yframework + the app read the state back through the
     * platform accessors and the HTML5 callbacks own the pipe. */
    int fb_width = 0, fb_height = 0;
    yetty_yplatform_webasm_get_framebuffer_size(&fb_width, &fb_height);

    struct yetty_yplatform_gpu_context gpu = {
        .instance = data->instance,
        .surface = data->surface,
        .surface_width = (uint32_t)fb_width,
        .surface_height = (uint32_t)fb_height,
        .content_scale = 1.0f,
    };
    struct yetty_ycore_void_result set_gpu = yetty_yplatform_platform_set_gpu_context(obj, &gpu);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_gpu, "webasm_platform_run: set gpu context");
    struct yetty_ycore_void_result set_svc = yetty_yplatform_platform_set_services(
        obj, data->config, data->input_pipe, data->clipboard, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_svc, "webasm_platform_run: set services");

    struct yetty_ycore_void_result app_init = yetty_yapp_init(app, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_init, "webasm_platform_run: app init");
    struct yetty_ycore_void_result app_run = yetty_yapp_run(app, obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_run, "webasm_platform_run: app run");

    ydebug("webasm_platform: run (loop owned by the browser/emscripten)");
    return YETTY_OK_VOID();
}

/* Platform-agnostic factory: returns the platform object appropriate to this
 * build. Cross-platform Class-2 tools (ygreeter, yhello) that own their main()
 * call this instead of naming a per-OS create, so the same source picks
 * glfw/webasm/android/ios by which platform.c is compiled in. */
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_default_platform_create(
    struct yetty_yclass_ctx *ctx)
{
    return yetty_yplatform_webasm_platform_create(ctx);
}

#include "yetty/gen/impl/yplatform/yplatform/webasm.c"
