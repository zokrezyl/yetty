/* glfw-main.c - Application entry point for Linux/macOS/Windows */

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
 * support (which also pulls in X11 headers via the yetty_yrender link). On
 * Wayland, glfwGetX11Display() / glfwGetX11Window() return NULL/0, which is
 * exactly what yetty_create expects when the X11-tile path isn't usable. */
#if defined(__linux__) && defined(YETTY_HAS_X11_TILE)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
static void platform_get_x11_handles(GLFWwindow *win, void **disp, unsigned long *xwin)
{
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

#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
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
WGPUSurface yetty_yplatform_create_surface(WGPUInstance instance, GLFWwindow *window);
void yetty_yplatform_setup_window_callbacks(GLFWwindow *window);
void yetty_yplatform_run_os_event_loop(GLFWwindow *window, int *running);

/* Render thread args */
struct yetty_yplatform_render_thread_args {
    struct yetty_yetty_yetty *yetty;
    int *running;
    GLFWwindow *window;
    int result;
};

YETTY_EXTERNAL_CALLBACK
static int render_thread_func(void *arg)
{
    struct yetty_yplatform_render_thread_args *args = arg;
    struct yetty_ycore_void_result res = yetty_run(args->yetty);

    args->result = YETTY_IS_OK(res) ? 0 : 1;
    *(args->running) = 0;

    if (args->window) {
        glfwPostEmptyEvent();
    }

    return 0;
}

int main(int argc, char **argv)
{
    /* Advertise ourselves via the de-facto TERM_PROGRAM convention so child
     * processes (PTY shells, tools like ycat) can detect a yetty terminal
     * and adapt their output (e.g. emit ypaint OSC sequences instead of
     * plain ANSI). Done here at the top of main so every fork inherits it. */
    setenv("TERM_PROGRAM", "yetty", 1);

#if defined(__linux__) && !defined(__ANDROID__)
    /* On linux GLFW 3.4 picks Wayland if WAYLAND_DISPLAY is set, else X11.
     * Native Wayland also requires the Dawn prebuilt to accept
     * SurfaceSourceWaylandSurface — Google's official ubuntu-latest Dawn
     * release does NOT (validation aborts with "Unsupported sType"), so on
     * a Wayland session the surface ends up invalid and yetty renders
     * nothing. Default to X11 (XWayland on Wayland sessions); the user can
     * opt into native Wayland with YETTY_GLFW_PLATFORM=wayland once Dawn
     * gains the missing backend support. */
    const char *_yetty_glfw_platform = getenv("YETTY_GLFW_PLATFORM");
    if (!_yetty_glfw_platform || strcmp(_yetty_glfw_platform, "x11") == 0) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    } else if (strcmp(_yetty_glfw_platform, "wayland") == 0) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    }
    /* Any other value: leave GLFW's auto-pick alone. */
#endif

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    /* Platform paths */
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

    struct yetty_yplatform_paths paths = {.shaders_dir = shaders_dir,
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

    /* Config */
    struct yetty_yconfig_result config_result = yetty_yconfig_create(argc, argv, &paths);
    if (!YETTY_IS_OK(config_result)) {
        fprintf(stderr, "Failed to create config\n");
        glfwTerminate();
        return 1;
    }
    struct yetty_yconfig_config *config = config_result.value;

    /* Extract assets */
    yetty_platform_extract_assets(config);
    ydebug("main: assets extracted");

    /* Check for headless mode */
    const char *vnc_headless = config->ops->get_string(config, "vnc/headless", NULL);
    int headless = vnc_headless && strcmp(vnc_headless, "true") == 0;

    /* Window dimensions */
    int width = config->ops->get_int(config, "window/width", 1280);
    int height = config->ops->get_int(config, "window/height", 720);

    /* Window (skip for headless mode) */
    GLFWwindow *window = NULL;
    if (!headless) {
        ydebug("main: creating window %dx%d", width, height);
        window = yetty_yplatform_create_window(width, height, "yetty");
        if (!window) {
            fprintf(stderr, "Failed to create window\n");
            config->ops->destroy(config);
            glfwTerminate();
            return 1;
        }
        ydebug("main: window created");
        yetty_yplatform_setup_window_callbacks(window);
        ydebug("main: window callbacks set up");
    } else {
        ydebug("main: headless mode, skipping window creation");
    }

    /* Platform input pipe */
    ydebug("main: creating platform input pipe");
    fflush(stderr);
    struct yetty_yplatform_input_pipe_result pipe_result = yetty_platform_input_pipe_create();
    ydebug("main: platform input pipe created, ok=%d", pipe_result.ok);
    if (!YETTY_IS_OK(pipe_result)) {
        fprintf(stderr, "Failed to create platform input pipe\n");
        if (window) {
            yetty_yplatform_destroy_window(window);
        }
        config->ops->destroy(config);
        glfwTerminate();
        return 1;
    }
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe = pipe_result.value;
    if (window) {
        glfwSetWindowUserPointer(window, platform_input_pipe);
    }

    /* PTY factory */
    ydebug("main: creating PTY factory");
    struct yetty_yplatform_pty_factory_ptr_result pty_factory_result =
        yetty_yplatform_pty_factory_create(config, NULL);
    if (!YETTY_IS_OK(pty_factory_result)) {
        fprintf(stderr, "Failed to create PTY factory\n");
        platform_input_pipe->ops->destroy(platform_input_pipe);
        if (window) {
            yetty_yplatform_destroy_window(window);
        }
        config->ops->destroy(config);
        glfwTerminate();
        return 1;
    }
    struct yetty_yplatform_pty_factory *pty_factory = pty_factory_result.value;

    /* WebGPU instance */
    WGPUInstance instance = wgpuCreateInstance(NULL);
    if (!instance) {
        fprintf(stderr, "Failed to create WebGPU instance\n");
        pty_factory->ops->destroy(pty_factory);
        platform_input_pipe->ops->destroy(platform_input_pipe);
        if (window) {
            yetty_yplatform_destroy_window(window);
        }
        config->ops->destroy(config);
        glfwTerminate();
        return 1;
    }

    /* Surface (NULL for headless mode) */
    WGPUSurface surface = NULL;
    if (window) {
        surface = yetty_yplatform_create_surface(instance, window);
        if (!surface) {
            fprintf(stderr, "Failed to create WebGPU surface\n");
            wgpuInstanceRelease(instance);
            pty_factory->ops->destroy(pty_factory);
            platform_input_pipe->ops->destroy(platform_input_pipe);
            yetty_yplatform_destroy_window(window);
            config->ops->destroy(config);
            glfwTerminate();
            return 1;
        }
    }

    /* App context */
    int fb_width, fb_height;
    if (window) {
        yetty_yplatform_get_framebuffer_size(window, &fb_width, &fb_height);
    } else {
        /* Headless mode: use configured dimensions */
        fb_width = width;
        fb_height = height;
    }

    void *x11_display = NULL;
    unsigned long x11_window = 0UL;
    // TODO we should detect if we are on wayland. If wayland, then do not trigger any Xwindows related stuff
    platform_get_x11_handles(window, &x11_display, &x11_window);

    struct yetty_yetty_app_context app_context = {
        .app_gpu_context = {.instance = instance,
                            .surface = surface,
                            .surface_width = (uint32_t)fb_width,
                            .surface_height = (uint32_t)fb_height,
                            .x11_display = x11_display,
                            .x11_window = x11_window},
        .config = config,
        .platform_input_pipe = platform_input_pipe,
        .pty_factory = pty_factory};

    /* Yetty */
    struct yetty_yetty_yetty_result yetty_result = yetty_create(&app_context);
    if (!YETTY_IS_OK(yetty_result)) {
        yerror("Failed to create Yetty: %s", yetty_result.error.msg);
        /* Note: yetty_destroy already released surface if it was configured */
        wgpuInstanceRelease(instance);
        pty_factory->ops->destroy(pty_factory);
        platform_input_pipe->ops->destroy(platform_input_pipe);
        if (window) {
            yetty_yplatform_destroy_window(window);
        }
        config->ops->destroy(config);
        glfwTerminate();
        return 1;
    }
    struct yetty_yetty_yetty *yetty = yetty_result.value;

    /* Render thread */
    int running = 1;
    struct yetty_yplatform_render_thread_args thread_args = {
        .yetty = yetty, .running = &running, .window = window, .result = 0};

    struct yetty_yplatform_ythread *render_thread =
        yetty_yplatform_ythread_create(render_thread_func, &thread_args);

    /* Initial resize event */
    if (window) {
        yetty_yplatform_get_framebuffer_size(window, &fb_width, &fb_height);
    }
    struct yetty_yui_event event = {
        .type = YETTY_YCORE_RESIZE,
        .resize = {.width = (float)fb_width, .height = (float)fb_height}};
    platform_input_pipe->ops->write(platform_input_pipe, &event, sizeof(event));

    /* OS event loop (headless uses a simple wait loop) */
    if (window) {
        yetty_yplatform_run_os_event_loop(window, &running);
    } else {
        /* Headless mode: just wait for render thread to finish */
        while (running) {
            yetty_yplatform_ytime_sleep_ms(100);
        }
    }
    yetty_yplatform_ythread_join(render_thread);

    /* Cleanup - surface is released by yetty_destroy (yetty owns it after configure) */
    ydebug("main: cleanup starting");
    yetty_destroy(yetty);
    ydebug("main: yetty destroyed, releasing instance");
    wgpuInstanceRelease(instance);
    ydebug("main: instance released, destroying pty_factory");
    pty_factory->ops->destroy(pty_factory);
    ydebug("main: pty_factory destroyed");
    if (window) {
        glfwSetWindowUserPointer(window, NULL);
    }
    platform_input_pipe->ops->destroy(platform_input_pipe);
    ydebug("main: platform_input_pipe destroyed");
    config->ops->destroy(config);
    ydebug("main: config destroyed");
    if (window) {
        ydebug("main: destroying window");
        yetty_yplatform_destroy_window(window);
        ydebug("main: window destroyed");
    }
    ydebug("main: calling glfwTerminate");
    glfwTerminate();
    ydebug("main: cleanup complete");

    return thread_args.result;
}
