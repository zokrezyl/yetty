/* Android program: the yetty terminal.
 *
 * The generic NDK app glue (window/input/IME/looper/android_main) lives in
 * android-glue.c. This file provides the terminal-specific halves the glue
 * resolves at link time: yetty_android_program_init builds the terminal
 * from the live surface and starts its render thread; yetty_android_program_term
 * tears it down. Compiled into libyetty.so. */

#include <webgpu/webgpu.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yinit/android-glue.h>

#include <yetty/yetty/yetty.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>

/* Forward declarations (defined in yplatform/paths/android.c). */
const char *yetty_yplatform_get_cache_dir(void);
const char *yetty_yplatform_get_runtime_dir(void);
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_config_dir(void);

/* Render thread args */
struct yetty_yplatform_render_thread_args {
    struct yetty_yetty_yetty *yetty;
    int *running;
};

YETTY_EXTERNAL_CALLBACK
static void *render_thread_func(void *arg)
{
    struct yetty_yplatform_render_thread_args *args = arg;
    yetty_run(args->yetty);
    *(args->running) = 0;
    free(args);
    return NULL;
}

YETTY_EXTERNAL_CALLBACK
void yetty_android_program_init(struct yetty_yplatform_app_state *state)
{
    const char *cache_dir;
    const char *runtime_dir;
    struct yetty_yconfig_paths paths;
    struct yetty_yconfig_result config_result;
    struct yetty_yplatform_input_pipe_result pipe_result;
    struct yetty_yplatform_pty_factory_ptr_result pty_result;
    struct yetty_yinit_runtime yinit_rt;
    struct yetty_yetty_yetty_result yetty_result;
    struct yetty_yplatform_render_thread_args *args;
    int32_t width, height;

    if (state->initialized || !state->window) {
        return;
    }

    LOGI("Initializing yetty...");

    cache_dir = yetty_yplatform_get_cache_dir();
    runtime_dir = yetty_yplatform_get_runtime_dir();

    yetty_yinit_android_mkdir_p(cache_dir);
    yetty_yinit_android_mkdir_p(runtime_dir);
    /* extract-assets writes to data_dir/{shaders,fonts,yemu,...} and
     * config_dir/temu — make sure both roots exist. Default Android
     * only creates /data/data/<pkg>/files, not /files/data. */
    {
        const char *data_dir = yetty_yplatform_get_data_dir();
        const char *config_dir = yetty_yplatform_get_config_dir();
        yetty_yinit_android_mkdir_p(data_dir);
        yetty_yinit_android_mkdir_p(config_dir);

        /* Match glfw-main.c: shaders/fonts live under <data_dir>/{shaders,fonts}
         * which is exactly where extract-assets puts them. */
        static char shaders_dir[512];
        static char fonts_dir[512];
        snprintf(shaders_dir, sizeof(shaders_dir), "%s/shaders", data_dir);
        snprintf(fonts_dir, sizeof(fonts_dir), "%s/fonts", data_dir);

        paths.shaders_dir = shaders_dir;
        paths.fonts_dir = fonts_dir;
        paths.config_dir = config_dir;
    }
    paths.runtime_dir = runtime_dir;
    paths.bin_dir = NULL;

    /* Extract embedded assets (kernel, opensbi, alpine rootfs, qemu binary,
     * cdb fonts, config.yaml...) onto disk where tinyemu / qemu / fontloader
     * / config-loader can read them. Must run BEFORE yetty_yconfig_create
     * so the bundled config.yaml is on disk on first launch. Without this,
     * tinyemu_pty_create() also fails to open kernel-riscv64.bin and the
     * process silently exits because ytrace logs go to stderr, which
     * Android's NativeActivity routes to /dev/null. */
    {
        struct yetty_ycore_void_result extract_result = yetty_platform_extract_assets();
        if (!YETTY_IS_OK(extract_result)) {
            LOGE("Failed to extract assets: %s",
                 extract_result.error.msg ? extract_result.error.msg : "(no message)");
            /* Fatal — without assets nothing will work. */
            return;
        }
        LOGI("Assets extracted to runtime dir");
    }

    /* Config — default to --qemu on Android (spawns external qemu loaded
     * from nativeLibraryDir/libqemu-system-riscv64.so, then telnets to
     * 127.0.0.1:QEMU_TELNET_PORT, same as desktop --qemu). There's no
     * shell command line on Android, so synthesize one. */
    {
        char *fake_argv[] = {(char *)"yetty", (char *)"--qemu", NULL};
        int fake_argc = 2;
        config_result = yetty_yconfig_create(fake_argc, fake_argv, &paths);
    }
    if (!YETTY_IS_OK(config_result)) {
        LOGE("Failed to create config");
        return;
    }
    state->config = config_result.value;

    /* Platform input pipe */
    pipe_result = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_result)) {
        LOGE("Failed to create input pipe");
        return;
    }
    state->pipe = pipe_result.value;

    /* PTY factory */
    pty_result = yetty_yplatform_pty_factory_create(state->config, NULL);
    if (!YETTY_IS_OK(pty_result)) {
        LOGE("Failed to create PTY factory");
        return;
    }
    state->pty_factory = pty_result.value;

    /* WebGPU instance.
     * TimedWaitAny is required by yplatform/webgpu/default.c — GPU futures
     * are waited on with non-zero timeoutNS from worker threads. */
    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = {0};
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;
    state->instance = wgpuCreateInstance(&instance_desc);
    if (!state->instance) {
        LOGE("Failed to create WebGPU instance");
        return;
    }

    /* Surface */
    state->surface = yetty_yplatform_create_surface_from_window(state->instance, state->window);
    if (!state->surface) {
        LOGE("Failed to create surface");
        return;
    }

    /* Get window size */
    width = ANativeWindow_getWidth(state->window);
    height = ANativeWindow_getHeight(state->window);

    /* Assemble a yinit_runtime in-place so yframework_create can take the
     * same code path the desktop worker uses. Android doesn't go through
     * yetty_yinit_run — the NDK drives the OS loop and we bootstrap here
     * inline — so the struct gets stamped by hand. No argv/output_pipe/
     * clipboard/window_manager on Android. */
    memset(&yinit_rt, 0, sizeof(yinit_rt));
    yinit_rt.config = state->config;
    yinit_rt.instance = state->instance;
    yinit_rt.surface = state->surface;
    yinit_rt.surface_width = (uint32_t)width;
    yinit_rt.surface_height = (uint32_t)height;
    yinit_rt.content_scale = yetty_yinit_android_content_scale(state->app);
    yinit_rt.platform_input_pipe = state->pipe;

    struct yetty_yframework_ptr_result yrt_res = yetty_yframework_create(&yinit_rt);
    if (!YETTY_IS_OK(yrt_res)) {
        LOGE("Failed to create yframework: %s",
             yrt_res.error.msg ? yrt_res.error.msg : "(no message)");
        yetty_ycore_error_destroy(yrt_res.error);
        return;
    }
    state->yframework = yrt_res.value;

    yetty_result = yetty_create(state->yframework, state->pty_factory);
    if (!YETTY_IS_OK(yetty_result)) {
        LOGE("Failed to create Yetty: %s",
             yetty_result.error.msg ? yetty_result.error.msg : "(no message)");
        yetty_yframework_destroy(state->yframework);
        state->yframework = NULL;
        return;
    }
    state->yetty = yetty_result.value;
    state->initialized = 1;
    state->running = 1;

    /* Start render thread */
    args = malloc(sizeof(struct yetty_yplatform_render_thread_args));
    args->yetty = state->yetty;
    args->running = &state->running;

    //TODO: use the platform abstraction yetty_yplatform_pthread_create() so that we can try to unify the main functions
    pthread_create(&state->render_thread, NULL, render_thread_func, args);

    /* Initial RESIZE event so the terminal/kernel get the real surface
     * size at startup (otherwise they stick at the libvterm default 80x24
     * until the user manually rotates the screen). Mirrors glfw-main.c. */
    {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_RESIZE;
        ev.resize.width = (float)width;
        ev.resize.height = (float)height;
        state->pipe->ops->write(state->pipe, &ev, sizeof(ev));
    }

    /* Pop the soft IME up at launch — there's no other input method on
     * a phone. We re-show on tap (handle_input below) so the user can
     * dismiss with Back and recover with a tap. */
    yetty_yinit_android_show_keyboard(state->app);

    LOGI("Yetty initialized successfully");
}

struct yetty_ycore_void_result yetty_android_program_term(struct yetty_yplatform_app_state *state)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (!state->initialized) {
        return YETTY_OK_VOID();
    }

    LOGI("Terminating yetty...");

    state->running = 0;
    if (state->render_thread) {
        pthread_join(state->render_thread, NULL);
        state->render_thread = 0;
    }

    if (state->yetty) {
        struct yetty_ycore_void_result r = yetty_destroy(state->yetty);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
        state->yetty = NULL;
    }
    if (state->yframework) {
        struct yetty_ycore_void_result r = yetty_yframework_destroy(state->yframework);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
        state->yframework = NULL;
        /* yframework_destroy already unconfigured + released the surface +
         * instance. Null them here so the legacy fallbacks below skip. */
        state->surface = NULL;
        state->instance = NULL;
    }
    if (state->surface) {
        wgpuSurfaceRelease(state->surface);
        state->surface = NULL;
    }
    if (state->instance) {
        wgpuInstanceRelease(state->instance);
        state->instance = NULL;
    }
    if (state->pty_factory) {
        state->pty_factory->ops->destroy(state->pty_factory);
        state->pty_factory = NULL;
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

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "term_yetty: yetty destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}
