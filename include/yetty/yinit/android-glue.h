/* Shared Android NDK app glue.
 *
 * The generic native_app_glue plumbing — window/surface lifecycle, input
 * translation (keys, chars, mouse, pinch-zoom), the soft-IME bridge, the
 * stdout/stderr -> logcat redirector and the ALooper run loop — is identical
 * for every yetty-family program that runs as a NativeActivity. It lives once
 * in android-glue.c and is compiled into each program's shared library
 * (libyetty.so, libygreeter.so).
 *
 * Each program supplies the two program-specific halves declared below:
 *   yetty_android_program_init  — build the app from the live surface and
 *                                 start its render thread.
 *   yetty_android_program_term  — tear it down.
 * The glue resolves them at link time (exactly one implementation per .so). */

#ifndef YETTY_YINIT_ANDROID_GLUE_H
#define YETTY_YINIT_ANDROID_GLUE_H

#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <pthread.h>
#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>

#define LOG_TAG "yetty"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Forward decls — programs store their own objects through these slots. */
struct yetty_yetty_yetty;
struct yetty_yframework;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yconfig_config;
struct yetty_yplatform_pty_factory;

struct yetty_yplatform_app_state {
    struct android_app *app;
    ANativeWindow *window;
    /* Terminal program: the yetty app. NULL for other programs. */
    struct yetty_yetty_yetty *yetty;
    /* Built by program_init from a synthetic yinit_runtime; owned here,
     * outlives the app object. */
    struct yetty_yframework *yframework;
    struct yetty_ycore_xthread_event_pipe *pipe;
    struct yetty_yconfig_config *config;
    /* Terminal program only. */
    struct yetty_yplatform_pty_factory *pty_factory;
    WGPUInstance instance;
    WGPUSurface surface;
    /* Program-specific state that doesn't fit the slots above
     * (e.g. ygreeter's showcase app object). */
    void *program_state;
    pthread_t render_thread;
    int running;
    int initialized;
    /* Two-finger pinch-to-zoom state. While pinch_active is set, pointer
     * motion drives the visual magnify (Ctrl+wheel equivalent) instead of
     * being forwarded as a mouse drag. pinch_last_dist is the previous
     * finger-to-finger distance in pixels. */
    int pinch_active;
    float pinch_last_dist;
};

/* --- program interface (one implementation per shared library) --------- */

/* Build the program from state->window (already set) and start its render
 * thread. Absorbs its own errors (logs via LOGE) — the NDK command
 * callback that invokes it cannot propagate a Result. */
void yetty_android_program_init(struct yetty_yplatform_app_state *state);

/* Best-effort teardown; safe to call when not initialized. */
struct yetty_ycore_void_result yetty_android_program_term(struct yetty_yplatform_app_state *state);

/* --- glue helpers usable by program implementations -------------------- */

/* Recursive mkdir (mode 0755), ignores existing dirs. */
void yetty_yinit_android_mkdir_p(const char *path);

/* Pop / dismiss the soft IME via the JNI InputMethodManager bridge. */
void yetty_yinit_android_show_keyboard(struct android_app *app);
void yetty_yinit_android_hide_keyboard(struct android_app *app);

/* Defined in yplatform/webgpu-surface/android.c. */
WGPUSurface yetty_yplatform_create_surface_from_window(WGPUInstance instance,
                                                       ANativeWindow *window);

#endif /* YETTY_YINIT_ANDROID_GLUE_H */
