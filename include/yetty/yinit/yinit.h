/*
 * yinit/yinit.h - platform bootstrap framework.
 *
 * Owns everything that gets a process from "main()" to "worker thread
 * running with a window, WGPU surface, config and event pipes wired
 * up". The application supplies a single worker function; yinit
 * handles paths, asset extraction, yconfig parsing, glfwInit + window
 * + surface, the OS event loop, and teardown.
 *
 * The yetty terminal is one consumer (src/yetty/ymain). Standalone
 * apps (analyzers, diagnostics, visualisers) can be additional
 * consumers — they reuse the bootstrap and only provide their own
 * worker body.
 */

#ifndef YETTY_YINIT_YINIT_H
#define YETTY_YINIT_YINIT_H

#include <yetty/ycore/result.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque platform window handle.
 *   - GLFW desktop platforms (Linux/macOS/Windows):  GLFWwindow *
 *   - WebAssembly:                                    NULL (canvas is implicit)
 *   - Android:                                        ANativeWindow * (via android_app)
 *   - iOS / tvOS:                                     CAMetalLayer-backed UIView *
 */
struct yetty_yinit_runtime {
    /* CLI passthrough (NULL on platforms without argv, e.g. android). */
    int    argc;
    char **argv;

    /* Loaded yconfig (always present — yinit reads it before calling
     * the worker). Ownership stays with yinit; the worker must not
     * destroy it. */
    struct yetty_yconfig_config *config;

    /* WebGPU instance + surface. `surface` is NULL in headless mode
     * (config key vnc/headless=true) — the worker may still run, just
     * without a presentable surface. */
    void    *instance;             /* WGPUInstance */
    void    *surface;              /* WGPUSurface, NULL if headless */
    uint32_t surface_width;
    uint32_t surface_height;
    float    content_scale;

    /* X11-tile renderer co-handles (Linux/X11 only; NULL/0 elsewhere). */
    void          *x11_display;
    unsigned long  x11_window;

    /* Opaque native window handle (see comment above struct). */
    void *window;

    /* Event pipes.
     *   platform_input_pipe — main thread (OS events) → worker thread
     *   output_pipe         — worker thread → main thread (clipboard,
     *                         window manager); NULL if not requested */
    struct yetty_ycore_xthread_event_pipe   *platform_input_pipe;
    struct yetty_ycore_xthread_event_pipe   *output_pipe;
    struct yetty_platform_clipboard_manager *clipboard_manager;
    struct yetty_yplatform_window_manager   *window_manager;
};

/* Worker function — runs on a dedicated thread (or on the main thread
 * on platforms with a JS-driven loop like webasm). The runtime struct
 * is owned by yinit and stays valid until the worker returns.
 *
 * A non-OK result is printed to stderr by yinit before teardown. */
typedef struct yetty_ycore_void_result
(*yetty_yinit_worker_fn)(struct yetty_yinit_runtime *rt, void *user);

/* Per-app bootstrap knobs.
 *
 *   extract_assets_fn — optional callback invoked early in the
 *     bootstrap (after paths are set up, before yconfig). yetty passes
 *     yetty_platform_extract_assets here, which unpacks the brotli-
 *     compressed shaders/fonts/configs bundled in its binary. A
 *     standalone app with no bundled assets leaves this NULL — yinit
 *     then doesn't reference the symbol, so brotli/yncbin don't need
 *     to be linked.
 */
struct yetty_yinit_app_config {
    struct yetty_ycore_void_result (*extract_assets_fn)(void);
};

/* The standard entry point for platforms with an int main(argc,argv):
 *   glfw desktop (linux/macos/windows), webasm.
 *
 * `app_cfg` may be NULL — defaults are: extract_yetty_assets=false.
 *
 * Returns the process exit code. */
int yetty_yinit_run(int argc, char **argv,
                    const struct yetty_yinit_app_config *app_cfg,
                    yetty_yinit_worker_fn worker, void *user);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YINIT_YINIT_H */
