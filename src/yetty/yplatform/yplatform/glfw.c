/*
 * yplatform/yplatform/glfw.c — GLFW desktop platform subclass
 * (Linux / macOS / Windows).
 *
 *   platform_init(argc, argv) — no-op on glfw: the window/surface/pipes are
 *                   brought up inside platform_run's bootstrap (the GLFW window
 *                   must exist on the same thread that runs the event loop).
 *   platform_run(argc, argv)  — the full desktop bootstrap. Brings up config +
 *                   window (ywindow) + surface + channels + clipboard
 *                   (yclipboard) + window chrome, runs the yetty app on a worker
 *                   thread, and pumps the GLFW event loop on this (main) thread
 *                   until the window closes.
 *
 * This subclass replaces the old yinit/glfw.c bootstrap: the window and
 * clipboard are now the polymorphic yclass abstractions (yplatform:glfw_window /
 * yplatform:glfw_clipboard), not the hand-written window/default.c +
 * clipboard/default.c.
 *
 * Compiled into the yetty exec (not the yplatform static lib) so it can call up
 * into yconfig/yframework/yetty; the class accessor it provides is referenced by
 * the yplatform rpc.gen.c and resolved at exec link.
 */

#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yapp/app.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/thread.h>
#include <yetty/yplatform/time.h>
#include <yetty/yplatform/ywindow/glfw.h>
#include <yetty/yplatform/ywindow/window.h>
#include <yetty/yplatform/yclipboard/glfw.h>
#include <yetty/yplatform/yclipboard/clipboard.h>
#include <yetty/yplatform/ywindow-chrome/window-chrome.h>
#include <yetty/yplatform/ywindow-chrome/glfw.h>
#include <yetty/ytrace/ytrace.h>

/* Native X11 handles — only on Linux when yrender was built with X11-tile
 * support. Safe to call only when the runtime platform actually is X11; under
 * Wayland (or a NULL platform) we must not touch them, so we gate on
 * glfwGetPlatform() and return NULL/0 otherwise (the worker reads NULL/0 as
 * "X11-tile path unavailable"). */
#if defined(__linux__) && defined(YETTY_HAS_X11_TILE)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
static void glfw_platform_x11_handles(GLFWwindow *window, void **display, unsigned long *x11_window)
{
    *display = NULL;
    *x11_window = 0UL;
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) {
        return;
    }
    *display = (void *)glfwGetX11Display();
    *x11_window = window ? (unsigned long)glfwGetX11Window(window) : 0UL;
}
#else
static void glfw_platform_x11_handles(GLFWwindow *window, void **display, unsigned long *x11_window)
{
    (void)window;
    *display = NULL;
    *x11_window = 0UL;
}
#endif

/* OS event-loop callbacks (os-event-loop/default.c) — attach reads/writes the
 * input pipe through the GLFW user pointer; teardown frees that state. */
void yetty_yplatform_setup_window_callbacks(GLFWwindow *window,
                                            struct yetty_ycore_xthread_event_pipe *pipe);
void yetty_yplatform_teardown_window_callbacks(GLFWwindow *window);

/* Internal platform-glue accessor (ywindow/glfw.c): the private GLFWwindow*. Not
 * part of the public window API — only the platform layer that owns the OS loop
 * may reach it (event-loop callbacks, window_chrome, x11 co-handles). */
void *yetty_yplatform_glfw_window_native_handle(struct yetty_yclass_object *obj);

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_glfw_platform_ptr, struct yetty_yplatform_glfw_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_glfw_platform_class_get(void);
struct yetty_yplatform_glfw_platform_ptr_result yetty_yplatform_glfw_platform_from(
    struct yetty_yclass_object *obj);

/* No per-instance state: the bootstrap below owns the window / surface / pipes /
 * runtime for the duration of run(). */
struct [[clang::annotate("class@yplatform:glfw_platform")]] [[clang::annotate("platform@glfw")]]
[[clang::annotate("parent@yplatform:platform")]] yetty_yplatform_glfw_platform {
    char reserved;
};

/* Trampoline carries the app object + runtime across the thread boundary, then
 * pokes the GLFW event loop awake when the app exits so the main thread doesn't
 * sleep past process shutdown. The app body itself is the yapp:app subclass's
 * app_run — the platform is generic and never names the concrete program. */
struct glfw_platform_worker_args {
    struct yetty_yclass_object *app;
    struct yetty_yinit_runtime *rt;
    int *running;
    GLFWwindow *window;
    int result;
};

YETTY_EXTERNAL_CALLBACK
static int glfw_platform_worker_trampoline(void *arg)
{
    struct glfw_platform_worker_args *worker_args = arg;
    struct yetty_ycore_void_result res =
        yetty_yapp_app_run(worker_args->app, worker_args->rt);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_print(stderr, "glfw_platform: worker fatal error", res.error);
        yetty_ycore_error_destroy(res.error);
        worker_args->result = 1;
    } else {
        worker_args->result = 0;
    }
    *(worker_args->running) = 0;
    if (worker_args->window) {
        glfwPostEmptyEvent();
    }
    return 0;
}

/* Main-thread OS event loop. After each event burst, drains the render→main
 * buses: the clipboard's own drain (COPY/PASTE) and the window-chrome bus
 * (WINDOW_* / SET_CURSOR, applied one event at a time). GLFW window-mutating and
 * clipboard calls must run on this thread; the render thread parks requests on
 * the pipes and wakes us with glfwPostEmptyEvent. */
static void glfw_platform_event_loop(GLFWwindow *window, int *running,
                                     struct yetty_yclass_object *clipboard,
                                     struct yetty_ycore_xthread_event_pipe *chrome_output_pipe,
                                     struct yetty_yclass_object *window_chrome)
{
    while (*running && !glfwWindowShouldClose(window)) {
        glfwWaitEvents();

        if (clipboard) {
            struct yetty_ycore_void_result drain_res = yetty_yplatform_clipboard_drain(clipboard);
            if (YETTY_IS_ERR(drain_res)) {
                yerror("glfw_platform: clipboard drain failed: %s", drain_res.error.msg);
                yetty_ycore_error_destroy(drain_res.error);
            }
        }

        if (window_chrome && chrome_output_pipe) {
            for (;;) {
                struct yetty_yui_event event;
                struct yetty_ycore_size_result read_res =
                    chrome_output_pipe->ops->read(chrome_output_pipe, &event, sizeof(event));
                if (YETTY_IS_ERR(read_res)) {
                    yetty_ycore_error_destroy(read_res.error);
                    break;
                }
                if (read_res.value == 0 || read_res.value != sizeof(event)) {
                    break;
                }
                struct yetty_ycore_void_result handle_res =
                    yetty_yplatform_window_chrome_handle_event(window_chrome, &event);
                if (YETTY_IS_ERR(handle_res)) {
                    yetty_ycore_error_destroy(handle_res.error);
                }
            }
        }
    }
}

[[clang::annotate("override@yplatform:glfw_platform:platform_init")]]
static struct yetty_ycore_void_result glfw_platform_init(struct yetty_yclass_object *obj,
                                                         struct yetty_yclass_object *app, int argc,
                                                         char **argv)
{
    /* GLFW requires the window + event loop on one thread, which is run()'s
     * thread, so the bring-up lives there; nothing to do here. */
    (void)obj;
    (void)app;
    (void)argc;
    (void)argv;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_platform:platform_run")]]
static struct yetty_ycore_void_result glfw_platform_run(struct yetty_yclass_object *obj,
                                                        struct yetty_yclass_object *app, int argc,
                                                        char **argv)
{
    (void)obj;
    if (!app) {
        return YETTY_ERR(yetty_ycore_void, "glfw_platform_run: app object is required");
    }

    /* Config resolves the platform directory layout itself, exports the YETTY_*
     * env vars, and parses argv. -h/--help and unknown-flag errors exit() from
     * inside yetty_yconfig_create, so anything that needs a display server
     * (glfwInit, window, surface) MUST come after this point. */
    struct yetty_yconfig_result config_res = yetty_yconfig_create(argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "glfw_platform: failed to create config");
    struct yetty_yconfig_config *config = config_res.value;

    /* Headless (--yvnc-headless): VNC server runs without a window, so there is
     * no display server to talk to and glfwInit must not be called. */
    const char *vnc_headless = config->ops->get_string(config, "vnc/headless", NULL);
    int headless = vnc_headless && strcmp(vnc_headless, "true") == 0;

    if (!headless) {
#if defined(__linux__) && !defined(__ANDROID__)
        /* Diagnostic override: YETTY_FORCE_X11=1 forces the X11/XWayland path so
         * the X11-tile renderer is exercised on a Wayland session. */
        if (getenv("YETTY_FORCE_X11")) {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        }
#endif
        if (!glfwInit()) {
            config->ops->destroy(config);
            return YETTY_ERR(yetty_ycore_void, "glfw_platform: failed to initialize GLFW");
        }
    } else {
        ydebug("glfw_platform: headless mode, skipping glfwInit");
    }

    int width = config->ops->get_int(config, "window/width", 1280);
    int height = config->ops->get_int(config, "window/height", 720);

    /* Window — the polymorphic ywindow glfw subclass owns the native handle. */
    struct yetty_yclass_object *window_obj = NULL;
    GLFWwindow *window = NULL;
    if (!headless) {
        ydebug("glfw_platform: creating window %dx%d", width, height);
        struct yetty_yclass_object_ptr_result window_create_res =
            yetty_yplatform_glfw_window_create(NULL);
        if (!YETTY_IS_OK(window_create_res)) {
            config->ops->destroy(config);
            glfwTerminate();
            return YETTY_ERR(yetty_ycore_void, "glfw_platform: window create failed",
                             window_create_res);
        }
        window_obj = window_create_res.value;
        struct yetty_ycore_void_result open_res =
            yetty_yplatform_window_open(window_obj, width, height, "yetty");
        if (YETTY_IS_ERR(open_res)) {
            (void)yetty_yclass_object_free(window_obj);
            config->ops->destroy(config);
            glfwTerminate();
            return YETTY_ERR(yetty_ycore_void, "glfw_platform: window open failed", open_res);
        }
        window = yetty_yplatform_glfw_window_native_handle(window_obj);
        ydebug("glfw_platform: window created");
    }

    /* Platform input pipe (main → render). */
    struct yetty_yplatform_input_pipe_result pipe_res = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_res)) {
        if (window_obj) {
            (void)yetty_yplatform_window_destroy(window_obj);
            (void)yetty_yclass_object_free(window_obj);
        }
        config->ops->destroy(config);
        if (!headless) {
            glfwTerminate();
        }
        return YETTY_ERR(yetty_ycore_void, "glfw_platform: failed to create platform input pipe",
                         pipe_res);
    }
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe = pipe_res.value;
    if (window) {
        yetty_yplatform_setup_window_callbacks(window, platform_input_pipe);
    }

    /* WebGPU instance. TimedWaitAny is required by yplatform/webgpu/default.c
     * (GPU futures waited with non-zero timeout from worker threads). Dawn debug
     * toggles are opt-in via YETTY_DAWN_DEBUG=1. */
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
    }

    WGPUInstance instance = wgpuCreateInstance(&instance_desc);
    if (!instance) {
        platform_input_pipe->ops->destroy(platform_input_pipe);
        if (window_obj) {
            (void)yetty_yplatform_window_destroy(window_obj);
            (void)yetty_yclass_object_free(window_obj);
        }
        config->ops->destroy(config);
        if (!headless) {
            glfwTerminate();
        }
        return YETTY_ERR(yetty_ycore_void, "glfw_platform: failed to create WebGPU instance");
    }

    /* Surface (NULL for headless). The ywindow subclass creates it from its own
     * private native handle. */
    WGPUSurface surface = NULL;
    if (window_obj) {
        struct yetty_yclass_void_ptr_result surface_res =
            yetty_yplatform_window_create_surface(window_obj, instance);
        if (!YETTY_IS_OK(surface_res)) {
            wgpuInstanceRelease(instance);
            platform_input_pipe->ops->destroy(platform_input_pipe);
            (void)yetty_yplatform_window_destroy(window_obj);
            (void)yetty_yclass_object_free(window_obj);
            config->ops->destroy(config);
            glfwTerminate();
            return YETTY_ERR(yetty_ycore_void, "glfw_platform: failed to create WebGPU surface",
                             surface_res);
        }
        surface = surface_res.value;
    }

    /* Framebuffer dimensions + HiDPI scale (= framebuffer/window). */
    int fb_width = width, fb_height = height;
    float content_scale = 1.0f;
    if (window_obj) {
        (void)yetty_yplatform_window_get_framebuffer_size(window_obj, &fb_width, &fb_height);
        int win_w = 0, win_h = 0;
        (void)yetty_yplatform_window_get_size(window_obj, &win_w, &win_h);
        if (win_w > 0 && fb_width > 0) {
            content_scale = (float)fb_width / (float)win_w;
        }
    }

    void *x11_display = NULL;
    unsigned long x11_window = 0UL;
    glfw_platform_x11_handles(window, &x11_display, &x11_window);

    /* Clipboard (yclipboard) + window chrome — each with its own render→main bus
     * drained on the main thread. NULL is fine in headless: copy/paste and
     * window controls silently no-op. */
    struct yetty_yclass_object *clipboard = NULL;
    struct yetty_ycore_xthread_event_pipe *clipboard_output_pipe = NULL;
    struct yetty_yclass_object *window_chrome = NULL;
    struct yetty_ycore_xthread_event_pipe *chrome_output_pipe = NULL;
    if (!headless) {
        struct yetty_yplatform_input_pipe_result clip_pipe_res = yetty_platform_input_pipe_create();
        if (YETTY_IS_OK(clip_pipe_res)) {
            clipboard_output_pipe = clip_pipe_res.value;
            struct yetty_yclass_object_ptr_result clip_res =
                yetty_yplatform_glfw_clipboard_create(NULL);
            if (YETTY_IS_OK(clip_res)) {
                clipboard = clip_res.value;
                struct yetty_ycore_void_result clip_cfg = yetty_yplatform_glfw_clipboard_configure(
                    clipboard, clipboard_output_pipe, platform_input_pipe);
                if (YETTY_IS_ERR(clip_cfg)) {
                    ywarn("glfw_platform: clipboard configure failed: %s", clip_cfg.error.msg);
                    yetty_ycore_error_destroy(clip_cfg.error);
                    (void)yetty_yclass_object_free(clipboard);
                    clipboard = NULL;
                }
            } else {
                ywarn("glfw_platform: clipboard create failed: %s", clip_res.error.msg);
                yetty_ycore_error_destroy(clip_res.error);
            }
            if (!clipboard) {
                clipboard_output_pipe->ops->destroy(clipboard_output_pipe);
                clipboard_output_pipe = NULL;
            }
        } else {
            ywarn("glfw_platform: clipboard pipe create failed: %s", clip_pipe_res.error.msg);
            yetty_ycore_error_destroy(clip_pipe_res.error);
        }

        struct yetty_yplatform_input_pipe_result chrome_pipe_res =
            yetty_platform_input_pipe_create();
        if (YETTY_IS_OK(chrome_pipe_res)) {
            chrome_output_pipe = chrome_pipe_res.value;
            struct yetty_yclass_object_ptr_result chrome_res =
                yetty_yplatform_glfw_window_chrome_create(NULL);
            if (YETTY_IS_OK(chrome_res)) {
                window_chrome = chrome_res.value;
                /* Base binds the render→main bus; the glfw subclass binds the
                 * native window + response pipe (each writes its own slice). */
                struct yetty_ycore_void_result chrome_cfg =
                    yetty_yplatform_window_chrome_configure(window_chrome, chrome_output_pipe);
                if (YETTY_IS_OK(chrome_cfg)) {
                    chrome_cfg = yetty_yplatform_glfw_window_chrome_attach(window_chrome, window,
                                                                          platform_input_pipe);
                }
                if (YETTY_IS_ERR(chrome_cfg)) {
                    ywarn("glfw_platform: window chrome configure failed: %s", chrome_cfg.error.msg);
                    yetty_ycore_error_destroy(chrome_cfg.error);
                    (void)yetty_yplatform_window_chrome_destroy(window_chrome);
                    window_chrome = NULL;
                }
            } else {
                ywarn("glfw_platform: window chrome create failed: %s", chrome_res.error.msg);
                yetty_ycore_error_destroy(chrome_res.error);
            }
            if (!window_chrome) {
                chrome_output_pipe->ops->destroy(chrome_output_pipe);
                chrome_output_pipe = NULL;
            }
        } else {
            ywarn("glfw_platform: window chrome pipe create failed: %s", chrome_pipe_res.error.msg);
            yetty_ycore_error_destroy(chrome_pipe_res.error);
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
        .output_pipe = NULL,
        .clipboard = clipboard,
        .window_chrome = window_chrome,
    };

    int running = 1;
    struct glfw_platform_worker_args worker_args = {
        .app = app,
        .rt = &rt,
        .running = &running,
        .window = window,
        .result = 0,
    };
    struct yetty_yplatform_ythread *render_thread =
        yetty_yplatform_ythread_create(glfw_platform_worker_trampoline, &worker_args);

    if (render_thread) {
        if (window) {
            glfw_platform_event_loop(window, &running, clipboard, chrome_output_pipe, window_chrome);
        } else {
            while (running) {
                yetty_yplatform_ytime_sleep_ms(100);
            }
        }
        yetty_yplatform_ythread_join(render_thread);
    } else {
        /* No worker thread: don't enter the event loop (it would spin with
         * running==1, and headless would loop forever) and don't join NULL.
         * Fall through to teardown and surface the failure. */
        yerror("glfw_platform: render thread create failed");
        worker_args.result = 1;
    }

    /* Teardown — the worker tore down whatever it created (yetty releases its
     * surface on destroy); we tear down only the bootstrap layer. */
    wgpuInstanceRelease(instance);
    if (window) {
        yetty_yplatform_teardown_window_callbacks(window);
    }
    platform_input_pipe->ops->destroy(platform_input_pipe);
    if (clipboard) {
        (void)yetty_yclass_object_free(clipboard);
    }
    if (clipboard_output_pipe) {
        clipboard_output_pipe->ops->destroy(clipboard_output_pipe);
    }
    if (window_chrome) {
        (void)yetty_yplatform_window_chrome_destroy(window_chrome);
    }
    if (chrome_output_pipe) {
        chrome_output_pipe->ops->destroy(chrome_output_pipe);
    }
    config->ops->destroy(config);
    if (window_obj) {
        (void)yetty_yplatform_window_destroy(window_obj);
        (void)yetty_yclass_object_free(window_obj);
    }
    if (!headless) {
        glfwTerminate();
    }
    ydebug("glfw_platform: cleanup complete");

    if (worker_args.result != 0) {
        return YETTY_ERR(yetty_ycore_void, "glfw_platform: worker exited with error");
    }
    return YETTY_OK_VOID();
}

#include "glfw.gen.c"
