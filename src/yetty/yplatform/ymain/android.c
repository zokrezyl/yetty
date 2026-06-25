/*
 * yplatform/ymain/android.c — Android entry (program hooks for the NDK glue).
 *
 * The generic NDK plumbing (android_main + the ALooper loop + window/input/IME)
 * lives in android-glue.c and resolves these two program hooks at link time:
 *
 *   yetty_android_program_init — the ANativeWindow exists (APP_CMD_INIT_WINDOW);
 *                                assemble the runtime from the live surface,
 *                                create the concrete app through the
 *                                yetty_yapp_create_app() injection point, and
 *                                drive it (yapp:app init/run) on a render thread.
 *   yetty_android_program_term — stop the render thread and tear down.
 *
 * Android has no OS event loop we own here — the glue's ALooper IS the OS loop
 * and the app's run() blocks the render thread until shutdown. This mirrors the
 * desktop glfw_platform worker, but the runtime is stamped by hand from the
 * NDK app_state (Android never goes through a config-parsing main()).
 *
 * NOTE: requires the NDK/SDK toolchain to build; not compiled on desktop/web.
 */

#include <pthread.h>
#include <stdlib.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/android-glue.h> /* struct yetty_yplatform_app_state + hook sigs */
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/ytrace/ytrace.h>

/* Path helpers (yplatform/paths/android.c). */
const char *yetty_yplatform_get_cache_dir(void);
const char *yetty_yplatform_get_runtime_dir(void);
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_config_dir(void);

/* WebGPU surface from the live ANativeWindow (webgpu-surface/android.c). */
WGPUSurface yetty_yplatform_create_surface_from_window(WGPUInstance instance,
                                                       ANativeWindow *window);

/* App injection + lifecycle (yapp module; the concrete app is linked per .so). */
struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object *app,
                                               struct yetty_yclass_object *platform);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object *app,
                                              struct yetty_yclass_object *platform);

/* Platform class registration + base create (generated, yplatform module). The
 * platform object is just the bring-up-state carrier here — Android's NDK
 * ALooper is the OS loop, so we never call platform_run. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_platform_create(struct yetty_yclass_ctx *ctx);

/* Render-thread payload: the app object + the platform object it reads during
 * setup. Both must outlive program_init's stack — the app's run() services the
 * loop to completion on this thread — so the thread owns this (heap) and the
 * pointer is parked on state->program_state for program_term. */
struct yetty_android_render_args {
    struct yetty_yclass_object *app;
    struct yetty_yclass_object *platform;
    int *running;
};

YETTY_EXTERNAL_CALLBACK
static void *yetty_android_render_thread(void *arg)
{
    struct yetty_android_render_args *args = arg;
    /* The app's run() builds the framework + terminal and blocks on the render
     * loop until a shutdown event arrives on the input pipe. */
    struct yetty_ycore_void_result res = yetty_yapp_init(args->app, args->platform);
    if (YETTY_IS_OK(res)) {
        res = yetty_yapp_run(args->app, args->platform);
    }
    if (YETTY_IS_ERR(res)) {
        LOGE("android: app worker fatal: %s", res.error.msg ? res.error.msg : "(no message)");
        yetty_ycore_error_destroy(res.error);
    }
    *(args->running) = 0;
    return NULL;
}

YETTY_EXTERNAL_CALLBACK
void yetty_android_program_init(struct yetty_yplatform_app_state *state)
{
    if (state->initialized || !state->window) {
        return;
    }
    LOGI("Initializing yetty (android)...");

    yetty_yplatform_android_mkdir_p(yetty_yplatform_get_cache_dir());
    yetty_yplatform_android_mkdir_p(yetty_yplatform_get_runtime_dir());
    yetty_yplatform_android_mkdir_p(yetty_yplatform_get_data_dir());
    yetty_yplatform_android_mkdir_p(yetty_yplatform_get_config_dir());

    /* No shell command line on Android — synthesize one. Default to --qemu
     * (external qemu-system-riscv64 from nativeLibraryDir), same as before. */
    char *fake_argv[] = {(char *)"yetty", (char *)"--qemu", NULL};
    struct yetty_yconfig_result config_result = yetty_yconfig_create(2, fake_argv);
    if (!YETTY_IS_OK(config_result)) {
        LOGE("android: yconfig_create failed");
        yetty_ycore_error_destroy(config_result.error);
        return;
    }
    state->config = config_result.value;

    struct yetty_yplatform_input_pipe_result pipe_result = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_result)) {
        LOGE("android: input pipe create failed");
        yetty_ycore_error_destroy(pipe_result.error);
        return;
    }
    state->pipe = pipe_result.value;

    WGPUInstanceDescriptor instance_desc = {0};
    state->instance = wgpuCreateInstance(&instance_desc);
    if (!state->instance) {
        LOGE("android: WebGPU instance create failed");
        return;
    }
    state->surface = yetty_yplatform_create_surface_from_window(state->instance, state->window);
    if (!state->surface) {
        LOGE("android: WebGPU surface create failed");
        return;
    }

    int32_t width = ANativeWindow_getWidth(state->window);
    int32_t height = ANativeWindow_getHeight(state->window);

    struct yetty_android_render_args *args = calloc(1, sizeof(*args));
    if (!args) {
        LOGE("android: out of memory");
        return;
    }

    /* Register the platform classes and create a platform object to carry the
     * bring-up state by hand (no argv / output_pipe / window_chrome on Android).
     * The NDK ALooper is the OS loop, so we never call platform_run — the object
     * is just the handoff the app + yframework read back. */
    struct yetty_ycore_void_result reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(reg)) {
        LOGE("android: platform register failed: %s",
             reg.error.msg ? reg.error.msg : "(no message)");
        yetty_ycore_error_destroy(reg.error);
        free(args);
        return;
    }
    struct yetty_yclass_object_ptr_result platform_result = yetty_yplatform_platform_create(NULL);
    if (!YETTY_IS_OK(platform_result)) {
        LOGE("android: platform create failed: %s",
             platform_result.error.msg ? platform_result.error.msg : "(no message)");
        yetty_ycore_error_destroy(platform_result.error);
        free(args);
        return;
    }
    args->platform = platform_result.value;

    struct yetty_yplatform_gpu_context gpu = {
        .instance = state->instance,
        .surface = state->surface,
        .surface_width = (uint32_t)width,
        .surface_height = (uint32_t)height,
        .content_scale = yetty_yplatform_android_content_scale(state->app),
    };
    struct yetty_ycore_void_result populate =
        yetty_yplatform_platform_set_gpu_context(args->platform, &gpu);
    if (YETTY_IS_OK(populate)) {
        populate = yetty_yplatform_platform_set_services(args->platform, state->config, state->pipe,
                                                         NULL, NULL);
    }
    if (YETTY_IS_ERR(populate)) {
        LOGE("android: platform populate failed: %s",
             populate.error.msg ? populate.error.msg : "(no message)");
        yetty_ycore_error_destroy(populate.error);
        (void)yetty_yclass_object_free(args->platform);
        free(args);
        return;
    }
    args->running = &state->running;

    /* The concrete program is injected by the linked app module (yetty:app for
     * libyetty.so). */
    struct yetty_yclass_object_ptr_result app_result = yetty_yapp_create_app(NULL);
    if (!YETTY_IS_OK(app_result)) {
        LOGE("android: app create failed: %s",
             app_result.error.msg ? app_result.error.msg : "(no message)");
        yetty_ycore_error_destroy(app_result.error);
        (void)yetty_yclass_object_free(args->platform);
        free(args);
        return;
    }
    args->app = app_result.value;
    state->program_state = args; /* so program_term can free it after join */

    state->initialized = 1;
    state->running = 1;
    pthread_create(&state->render_thread, NULL, yetty_android_render_thread, args);

    /* Initial resize so the container gets the real surface size. */
    struct yetty_yui_event ev = {.type = YETTY_YCORE_RESIZE,
                                 .resize = {.width = (float)width, .height = (float)height}};
    state->pipe->ops->write(state->pipe, &ev, sizeof(ev));

    yetty_yplatform_android_show_keyboard(state->app);
    ydebug("android: render thread started (ALooper drives the OS loop)");
}

struct yetty_ycore_void_result yetty_android_program_term(struct yetty_yplatform_app_state *state)
{
    if (!state->initialized) {
        return YETTY_OK_VOID();
    }
    LOGI("Terminating yetty (android)...");

    /* Ask the app's render loop to exit, then join. The app's run() owns the
     * framework/terminal teardown; we only free our own glue allocations. */
    state->running = 0;
    if (state->pipe) {
        struct yetty_yui_event ev = {.type = YETTY_YCORE_SHUTDOWN};
        state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
    }
    if (state->render_thread) {
        pthread_join(state->render_thread, NULL);
        state->render_thread = 0;
    }

    if (state->program_state) {
        struct yetty_android_render_args *args = state->program_state;
        if (args->platform) {
            (void)yetty_yclass_object_free(args->platform);
        }
        free(state->program_state);
        state->program_state = NULL;
    }
    if (state->surface) {
        wgpuSurfaceRelease(state->surface);
        state->surface = NULL;
    }
    if (state->instance) {
        wgpuInstanceRelease(state->instance);
        state->instance = NULL;
    }
    if (state->pipe) {
        state->pipe->ops->destroy(state->pipe);
        state->pipe = NULL;
    }
    if (state->config) {
        state->config->ops->destroy(state->config);
        state->config = NULL;
    }

    state->initialized = 0;
    return YETTY_OK_VOID();
}
