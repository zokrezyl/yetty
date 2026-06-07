/*
 * yinit/glfw.c - platform bootstrap for Linux/macOS/Windows (GLFW).
 *
 * Implements yetty_yinit_run(): from main() to a running worker
 * thread with config + window + WGPU surface + event pipes. The
 * worker — passed by the application — runs on a spawned thread; the
 * OS event loop owns the main thread.
 *
 * This module knows nothing about the yetty terminal. The yetty main
 * (src/yetty/ymain/glfw.c) is one consumer; standalone analyzer apps
 * can be others.
 */

#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <yetty/yplatform/compat.h>
#include <yetty/yplatform/thread.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native X11 handles — only on Linux when yrender was built with X11-tile
 * support (which also pulls in X11 headers via the yetty_yrender link). The
 * native accessors are only safe to call when the runtime platform actually
 * is X11 — under Wayland (or NULL platform) we must not touch them, so we
 * gate on glfwGetPlatform() and return NULL/0 otherwise. The worker reads
 * NULL/0 as "X11-tile path unavailable". */
#if defined(__linux__) && defined(YETTY_HAS_X11_TILE)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
static void platform_get_x11_handles(GLFWwindow *win, void **disp, unsigned long *xwin)
{
    *disp = NULL;
    *xwin = 0UL;
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) {
        return;
    }
    *disp = (void *)glfwGetX11Display();
    *xwin = win ? (unsigned long)glfwGetX11Window(win) : 0UL;
}
#else
static void platform_get_x11_handles(GLFWwindow *win, void **disp, unsigned long *xwin)
{
    (void)win;
    *disp = NULL;
    *xwin = 0UL;
}
#endif

#include <yetty/yinit/yinit.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/clipboard-manager.h>
#include <yetty/yplatform/methods.h> /* window_manager configure/destroy slots */
#include <yetty/yplatform/rpc.h>     /* yetty_yplatform_window_manager_create / _register */
#include <yetty/yplatform/extract-assets.h>
#include <yetty/ytrace/ytrace.h>

/* Forward declarations - implemented in other platform files */
const char *yetty_yplatform_get_cache_dir(void);
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_config_dir(void);
const char *yetty_yplatform_get_runtime_dir(void);

GLFWwindow *yetty_yplatform_create_window(int width, int height, const char *title);
void yetty_yplatform_destroy_window(GLFWwindow *window);
void yetty_yplatform_get_framebuffer_size(GLFWwindow *window, int *width, int *height);
void yetty_yplatform_get_window_size(GLFWwindow *window, int *width, int *height);
WGPUSurface yetty_yplatform_create_surface(WGPUInstance instance, GLFWwindow *window);

/* OS event loop — owns its per-window state internally and reads/writes the
 * input pipe through the GLFW user pointer. Three calls from main: attach
 * (create state, register callbacks), run, teardown. */
void yetty_yplatform_setup_window_callbacks(GLFWwindow *window,
                                            struct yetty_ycore_xthread_event_pipe *pipe);
void yetty_yplatform_run_os_event_loop(GLFWwindow *window, int *running,
                                       struct yetty_platform_clipboard_manager *cm);
void yetty_yplatform_teardown_window_callbacks(GLFWwindow *window);

/* Trampoline carries the application worker + runtime across the
 * thread boundary, then pokes the GLFW event loop awake when the
 * worker exits so the main thread doesn't sleep past process shutdown. */
struct yinit_worker_args {
    yetty_yinit_worker_fn worker;
    void *user;
    struct yetty_yinit_runtime *rt;
    int *running;
    GLFWwindow *window;
    int result;
};

YETTY_EXTERNAL_CALLBACK
static int yinit_worker_trampoline(void *arg)
{
    struct yinit_worker_args *wa = arg;
    struct yetty_ycore_void_result res = wa->worker(wa->rt, wa->user);

    if (YETTY_IS_ERR(res)) {
        /* End-of-chain — yinit is the consumer; print the whole chain
         * to stderr (visible regardless of ytrace config) and free. */
        yetty_ycore_error_print(stderr, "yinit: worker fatal error", res.error);
        yetty_ycore_error_destroy(res.error);
        wa->result = 1;
    } else {
        wa->result = 0;
    }
    *(wa->running) = 0;

    if (wa->window) {
        glfwPostEmptyEvent();
    }
    return 0;
}

struct yetty_ycore_int_result yetty_yinit_run(int argc, char **argv,
                                              const struct yetty_yinit_app_config *app_cfg,
                                              yetty_yinit_worker_fn worker, void *user)
{
    struct yetty_yinit_app_config defaults = {0};
    if (!app_cfg) {
        app_cfg = &defaults;
    }
    /* Platform paths */
    //TODO adapt path reading using the new api that reads a struct
    const char *cache_dir = yetty_yplatform_get_cache_dir();
    const char *data_dir = yetty_yplatform_get_data_dir();
    const char *runtime_dir = yetty_yplatform_get_runtime_dir();
    const char *config_dir = yetty_yplatform_get_config_dir();

    char shaders_dir[512];
    char fonts_dir[512];
    snprintf(shaders_dir, sizeof(shaders_dir), "%s/shaders", data_dir);
    snprintf(fonts_dir, sizeof(fonts_dir), "%s/fonts", data_dir);

    yetty_yplatform_mkdir_p(cache_dir);
    yetty_yplatform_mkdir_p(data_dir);
    yetty_yplatform_mkdir_p(runtime_dir);
    yetty_yplatform_mkdir_p(fonts_dir);
    yetty_yplatform_mkdir_p(config_dir);

    //TODO: adapt path reading directly to yetty_yplatform_paths!
    struct yetty_yconfig_paths paths = {.shaders_dir = shaders_dir,
                                        .fonts_dir = fonts_dir,
                                        .runtime_dir = runtime_dir,
                                        .bin_dir = NULL,
                                        .config_dir = config_dir};

    /* Export platform paths as YETTY_* env vars so config files
     * (e.g. tinyemu .cfg) can reference them via $YETTY_RUNTIME_DIR etc. */
    setenv("YETTY_SHADERS_DIR", shaders_dir, 1);
    setenv("YETTY_FONTS_DIR", fonts_dir, 1);
    setenv("YETTY_RUNTIME_DIR", runtime_dir, 1);
    setenv("YETTY_DATA_DIR", data_dir, 1);
    setenv("YETTY_CONFIG_DIR", config_dir, 1);
    if (paths.bin_dir) {
        setenv("YETTY_BIN_DIR", paths.bin_dir, 1);
    }

    /* Extract bundled assets — app-specific. yetty supplies a callback
     * that unpacks its brotli/incbin'd shaders, fonts, configs and
     * kernels. Standalone apps with no bundled content pass NULL and
     * the brotli/yncbin link is never created. */
    if (app_cfg->extract_assets_fn) {
        struct yetty_ycore_void_result extract_result = app_cfg->extract_assets_fn();
        if (!YETTY_IS_OK(extract_result)) {
            return YETTY_ERR(yetty_ycore_int, "yinit: failed to extract assets", extract_result);
        }
        ydebug("yinit: assets extracted");
    } else {
        ydebug("yinit: no asset extraction callback supplied");
    }

    /* Config — parses argv. -h/--help and unknown-flag errors exit() from
     * inside yetty_yconfig_create, so anything that needs a display server
     * (glfwInit, window, surface) MUST come after this point. Otherwise
     * `yetty --help` over SSH without DISPLAY would die in glfwInit before
     * ever printing usage. */
    struct yetty_yconfig_result config_result = yetty_yconfig_create(argc, argv, &paths);
    if (!YETTY_IS_OK(config_result)) {
        return YETTY_ERR(yetty_ycore_int, "yinit: failed to create config", config_result);
    }
    struct yetty_yconfig_config *config = config_result.value;

    /* Check for headless mode — gates glfwInit. Headless is the
     * --yvnc-headless path: VNC server runs without a window, so there is
     * no display server to talk to and glfwInit must not be called (it
     * would fail on a TTY-only host like a remote SSH session). */
    const char *vnc_headless = config->ops->get_string(config, "vnc/headless", NULL);
    int headless = vnc_headless && strcmp(vnc_headless, "true") == 0;

    if (!headless) {
        /* No platform hint on linux — let GLFW 3.4 auto-pick. With
         * GLFW_ANY_PLATFORM (the default), GLFW probes in order Wayland →
         * X11 → NULL, so we get native Wayland when WAYLAND_DISPLAY is set
         * and X11 (or XWayland on a Wayland session without a Wayland-
         * capable GLFW build) as the fallback.
         *
         * The dawn-exotic Linux prebuilts (see build-tools/yetty/Dawn.cmake)
         * are built with the Wayland backend enabled, so the Wayland path
         * works end-to-end. The X11-tile renderer stays as the fallback —
         * it's still the right path for remote sessions (VNC, ssh -X)
         * where partial-tile blits beat full-frame surface presents. */
#if defined(__linux__) && !defined(__ANDROID__)
        /* Diagnostic override: YETTY_FORCE_X11=1 forces the X11/XWayland
         * path so the X11-tile renderer is exercised on a Wayland session
         * (used when bisecting "Wayland surface present vs. instanced draw"
         * bugs). Remove once the Wayland-surface path is fully stable. */
        if (getenv("YETTY_FORCE_X11")) {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        }
#endif

        if (!glfwInit()) {
            config->ops->destroy(config);
            return YETTY_ERR(yetty_ycore_int, "yinit: failed to initialize GLFW");
        }
    } else {
        ydebug("yinit: headless mode, skipping glfwInit");
    }

    /* Window dimensions */
    int width = config->ops->get_int(config, "window/width", 1280);
    int height = config->ops->get_int(config, "window/height", 720);

    /* Window (skip for headless mode) */
    GLFWwindow *window = NULL;
    if (!headless) {
        ydebug("yinit: creating window %dx%d", width, height);
        window = yetty_yplatform_create_window(width, height, "yetty");
        if (!window) {
            config->ops->destroy(config);
            glfwTerminate();
            return YETTY_ERR(yetty_ycore_int, "yinit: failed to create window");
        }
        ydebug("yinit: window created");
    } else {
        ydebug("yinit: headless mode, skipping window creation");
    }

    /* Platform input pipe */
    ydebug("yinit: creating platform input pipe");
    fflush(stderr);

    //TODO: fix inconsistency between the result type and the type itself, see below
    struct yetty_yplatform_input_pipe_result pipe_result = yetty_platform_input_pipe_create();
    ydebug("yinit: platform input pipe created, ok=%d", pipe_result.ok);
    if (!YETTY_IS_OK(pipe_result)) {
        if (window) {
            yetty_yplatform_destroy_window(window);
        }
        config->ops->destroy(config);
        if (!headless) {
            glfwTerminate();
        }
        return YETTY_ERR(yetty_ycore_int, "yinit: failed to create platform input pipe",
                         pipe_result);
    }
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe = pipe_result.value;
    if (window) {
        /* os-event-loop owns its per-window state internally and stores
         * the pipe (plus its own bookkeeping — double-click timestamps,
         * etc.) on the GLFW user pointer. main doesn't see it. */
        yetty_yplatform_setup_window_callbacks(window, platform_input_pipe);
        ydebug("yinit: window callbacks set up");
    }

    /* WebGPU instance.
     * TimedWaitAny is required by yplatform/webgpu/default.c: GPU futures
     * are waited on with non-zero timeoutNS from worker threads, which
     * Dawn rejects unless this feature is declared up front.
     *
     * Dawn debug toggles (opt-in via YETTY_DAWN_DEBUG=1) — enables aggressive
     * validation and keeps user-defined labels/symbols intact so backend
     * errors point back to our objects. Required to chase the
     * Wayland-direct-surface + multi-instance freeze. */
    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = {0};
    instance_desc.requiredFeatureCount = 1;
    instance_desc.requiredFeatures = instance_features;

    static const char *const dawn_enabled_toggles[] = {
        "use_user_defined_labels_in_backend",
        "disable_symbol_renaming",
        "enable_immediate_error_handling",
        "record_detailed_timing_in_trace_events",
    };
    WGPUDawnTogglesDescriptor dawn_toggles = {0};
    if (getenv("YETTY_DAWN_DEBUG")) {
        dawn_toggles.chain.sType = WGPUSType_DawnTogglesDescriptor;
        dawn_toggles.enabledToggleCount =
            sizeof(dawn_enabled_toggles) / sizeof(dawn_enabled_toggles[0]);
        dawn_toggles.enabledToggles = dawn_enabled_toggles;
        instance_desc.nextInChain = &dawn_toggles.chain;
        yinfo("dawn: debug toggles enabled (%zu): use_user_defined_labels_in_backend, "
              "disable_symbol_renaming, enable_immediate_error_handling, "
              "record_detailed_timing_in_trace_events",
              dawn_toggles.enabledToggleCount);
    } else {
        ydebug("dawn: debug toggles OFF (set YETTY_DAWN_DEBUG=1 to enable)");
    }

    ydebug("yinit: calling wgpuCreateInstance(features=%u, nextInChain=%p)",
           (unsigned)instance_desc.requiredFeatureCount, (void *)instance_desc.nextInChain);
    WGPUInstance instance = wgpuCreateInstance(&instance_desc);
    ydebug("yinit: wgpuCreateInstance returned instance=%p", (void *)instance);
    if (!instance) {
        platform_input_pipe->ops->destroy(platform_input_pipe);
        if (window) {
            yetty_yplatform_destroy_window(window);
        }
        config->ops->destroy(config);
        if (!headless) {
            glfwTerminate();
        }
        return YETTY_ERR(yetty_ycore_int, "yinit: failed to create WebGPU instance");
    }

    /* Surface (NULL for headless mode) */
    WGPUSurface surface = NULL;
    if (window) {
        surface = yetty_yplatform_create_surface(instance, window);
        if (!surface) {
            wgpuInstanceRelease(instance);
            platform_input_pipe->ops->destroy(platform_input_pipe);
            yetty_yplatform_destroy_window(window);
            config->ops->destroy(config);
            glfwTerminate();
            return YETTY_ERR(yetty_ycore_int, "yinit: failed to create WebGPU surface");
        }
    }

    /* Framebuffer dimensions for the runtime struct */
    int fb_width, fb_height;
    if (window) {
        yetty_yplatform_get_framebuffer_size(window, &fb_width, &fb_height);
    } else {
        /* Headless mode: use configured dimensions */
        fb_width = width;
        fb_height = height;
    }

    /* HiDPI scale = framebuffer/window. Captured here (after the window
     * exists, before the runtime is handed to the worker) so anywhere
     * downstream that needs to scale physical-unit config knobs (font
     * size, padding) can read content_scale instead of re-deriving it
     * from a pair of GLFW calls. */
    float content_scale = 1.0f;
    if (window) {
        int win_w = 0, win_h = 0;
        yetty_yplatform_get_window_size(window, &win_w, &win_h);
        if (win_w > 0 && fb_width > 0) {
            content_scale = (float)fb_width / (float)win_w;
        }
    }

    void *x11_display = NULL;
    unsigned long x11_window = 0UL;
    platform_get_x11_handles(window, &x11_display, &x11_window);

    /* Render→main "output pipe" + clipboard manager that uses it.
     *
     * The output pipe is a shared bus for anything the render thread
     * needs the main thread to do. Today only the clipboard manager
     * writes here (Set/GetClipboardString must run on the GLFW main
     * thread); future producers like window minimize/maximize/setTitle
     * will share the same pipe. The main thread drains it between
     * glfwWaitEvents and dispatches each event by type.
     *
     * Paste responses come back through the existing platform_input_pipe
     * as YETTY_YCORE_PASTE events with the text in event->payload.
     *
     * NULL is OK in headless mode: copy/paste silently no-ops. */
    struct yetty_ycore_xthread_event_pipe *output_pipe = NULL;
    struct yetty_platform_clipboard_manager *clipboard_manager = NULL;
    struct yetty_yclass_object *window_manager = NULL;
    if (!headless) {
        struct yetty_yplatform_input_pipe_result op_res = yetty_platform_input_pipe_create();
        if (YETTY_IS_OK(op_res)) {
            output_pipe = op_res.value;

            /* Window manager first so we can hand it to the clipboard
             * manager's drain: the drain reads every event off the pipe
             * and dispatches by type, sending WINDOW_* through here. */
            struct yetty_ycore_void_result wm_reg = yetty_yplatform_register();
            if (YETTY_IS_ERR(wm_reg)) {
                ywarn("yinit: window manager register failed: %s", wm_reg.error.msg);
                yetty_ycore_error_destroy(wm_reg.error);
            }
            struct yetty_yclass_object_ptr_result wm_res =
                yetty_yplatform_window_manager_create(NULL);
            if (YETTY_IS_OK(wm_res)) {
                window_manager = wm_res.value;
                struct yetty_ycore_void_result wm_cfg = yetty_yplatform_window_manager_configure(
                    NULL, window_manager, window, output_pipe, platform_input_pipe);
                if (YETTY_IS_ERR(wm_cfg)) {
                    ywarn("yinit: window manager configure failed: %s", wm_cfg.error.msg);
                    yetty_ycore_error_destroy(wm_cfg.error);
                    struct yetty_ycore_void_result wm_destroy =
                        yetty_yplatform_window_manager_destroy(NULL, window_manager);
                    if (YETTY_IS_ERR(wm_destroy)) {
                        yetty_ycore_error_destroy(wm_destroy.error);
                    }
                    window_manager = NULL;
                }
            } else {
                ywarn("yinit: window manager create failed: %s", wm_res.error.msg);
                yetty_ycore_error_destroy(wm_res.error);
            }

            struct yetty_yplatform_clipboard_manager_result clip_res =
                yetty_platform_clipboard_manager_create(output_pipe, platform_input_pipe,
                                                        window_manager);
            if (YETTY_IS_OK(clip_res)) {
                clipboard_manager = clip_res.value;
            } else {
                ywarn("yinit: clipboard manager create failed: %s", clip_res.error.msg);
                yetty_ycore_error_destroy(clip_res.error);
                if (window_manager) {
                    struct yetty_ycore_void_result wm_destroy =
                        yetty_yplatform_window_manager_destroy(NULL, window_manager);
                    if (YETTY_IS_ERR(wm_destroy)) {
                        yetty_ycore_error_destroy(wm_destroy.error);
                    }
                    window_manager = NULL;
                }
                output_pipe->ops->destroy(output_pipe);
                output_pipe = NULL;
            }
        } else {
            ywarn("yinit: output pipe create failed: %s", op_res.error.msg);
            yetty_ycore_error_destroy(op_res.error);
        }
    }

    /* Hand everything to the worker through the runtime struct. */
    struct yetty_yinit_runtime rt = {
        .argc = argc,
        .argv = argv,
        .config = config,
        .instance = instance,
        .surface = surface,
        .surface_width = (uint32_t)fb_width,
        .surface_height = (uint32_t)fb_height,
        .content_scale = content_scale,
        .x11_display = x11_display,
        .x11_window = x11_window,
        .window = window,
        .platform_input_pipe = platform_input_pipe,
        .output_pipe = output_pipe,
        .clipboard_manager = clipboard_manager,
        .window_manager = window_manager,
    };

    /* Worker thread */
    int running = 1;
    struct yinit_worker_args wa = {
        .worker = worker,
        .user = user,
        .rt = &rt,
        .running = &running,
        .window = window,
        .result = 0,
    };

    struct yetty_yplatform_ythread *render_thread =
        yetty_yplatform_ythread_create(yinit_worker_trampoline, &wa);

    /* OS event loop (headless uses a simple wait loop) */
    if (window) {
        yetty_yplatform_run_os_event_loop(window, &running, clipboard_manager);
    } else {
        /* Headless mode: just wait for the worker to finish */
        while (running) {
            yetty_yplatform_ytime_sleep_ms(100);
        }
    }
    yetty_yplatform_ythread_join(render_thread);

    /* Cleanup. The worker is responsible for tearing down whatever it
     * created (e.g. yetty itself releases its surface on destroy);
     * yinit tears down only the bootstrap layer it built. */
    ydebug("yinit: cleanup starting");
    wgpuInstanceRelease(instance);
    ydebug("yinit: instance released");
    if (window) {
        yetty_yplatform_teardown_window_callbacks(window);
    }
    platform_input_pipe->ops->destroy(platform_input_pipe);
    ydebug("yinit: platform_input_pipe destroyed");
    if (clipboard_manager) {
        struct yetty_ycore_void_result cd = clipboard_manager->ops->destroy(clipboard_manager);
        if (YETTY_IS_ERR(cd)) {
            yerror("yinit: clipboard_manager destroy failed: %s", cd.error.msg);
            yetty_ycore_error_destroy(cd.error);
        }
    }
    if (window_manager) {
        struct yetty_ycore_void_result wm_destroy =
            yetty_yplatform_window_manager_destroy(NULL, window_manager);
        if (YETTY_IS_ERR(wm_destroy)) {
            yetty_ycore_error_destroy(wm_destroy.error);
        }
    }
    if (output_pipe) {
        output_pipe->ops->destroy(output_pipe);
    }
    config->ops->destroy(config);
    ydebug("yinit: config destroyed");
    if (window) {
        ydebug("yinit: destroying window");
        yetty_yplatform_destroy_window(window);
        ydebug("yinit: window destroyed");
    }
    if (!headless) {
        ydebug("yinit: calling glfwTerminate");
        glfwTerminate();
    }
    ydebug("yinit: cleanup complete");

    /* wa.result is the worker's process exit code. The worker's own Result
     * error (if any) was already printed and freed at the thread-trampoline
     * boundary, so only the numeric code remains to carry up to main(). */
    return YETTY_OK(yetty_ycore_int, wa.result);
}
