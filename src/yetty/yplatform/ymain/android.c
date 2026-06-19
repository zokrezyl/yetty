/*
 * yplatform/ymain/android.c — Android entry.
 *
 * The generic NDK glue (yinit/android-glue.c) owns android_main + the ALooper
 * run loop and resolves these program hooks at link time. The native window
 * arrives asynchronously: on APP_CMD_INIT_WINDOW the glue calls
 * yetty_android_program_init, which is where the android_platform is created and
 * platform_init runs (window now exists). The ALooper loop IS the "run", so
 * android_platform's platform_run is a no-op.
 *
 * NOT WIRED IN: forward-declared platform symbols, not in any CMake target; the
 * existing yinit/android.c stays until this replaces it.
 *
 * SEAMS:
 *   - the ANativeWindow + framebuffer metrics must be pushed into the platform's
 *     android_window (yetty_yplatform_android_window_set_metrics) here — needs a
 *     platform accessor or a create-time hand-in, still to be decided.
 *   - app worker: how the yetty app body runs on top is the platform→app seam.
 *   - state ownership: the app_state struct needs a field to hold the platform
 *     object across to yetty_android_program_term.
 */

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yinit/android-glue.h> /* struct yetty_yplatform_app_state + hook sigs */
#include <yetty/ytrace/ytrace.h>

/* Generated platform symbols. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_android_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            int argc, char **argv);

YETTY_EXTERNAL_CALLBACK
void yetty_android_program_init(struct yetty_yplatform_app_state *state)
{
    if (state->initialized || !state->window) {
        return;
    }

    struct yetty_ycore_void_result reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(reg)) {
        yerror("android: platform register failed: %s", reg.error.msg);
        yetty_ycore_error_destroy(reg.error);
        return;
    }

    struct yetty_yclass_object_ptr_result platform_res =
        yetty_yplatform_android_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yerror("android: platform create failed: %s", platform_res.error.msg);
        yetty_ycore_error_destroy(platform_res.error);
        return;
    }
    /* TODO(seam): persist platform_res.value on `state` so program_term can
     * tear it down, and push the ANativeWindow + metrics into its
     * android_window before init. */

    /* run() calls init() itself; android has no argv. The ALooper loop (this
     * glue's android_main) is the actual run loop, so run() returns after init. */
    struct yetty_ycore_void_result run_res =
        yetty_yplatform_platform_run(platform_res.value, 0, NULL);
    if (YETTY_IS_ERR(run_res)) {
        yerror("android: platform run failed: %s", run_res.error.msg);
        yetty_ycore_error_destroy(run_res.error);
        return;
    }

    state->initialized = 1;
    state->running = 1;
    ydebug("android: platform run invoked (ALooper loop drives the event loop)");
}

struct yetty_ycore_void_result yetty_android_program_term(struct yetty_yplatform_app_state *state)
{
    if (!state->initialized) {
        return YETTY_OK_VOID();
    }
    /* TODO(seam): destroy the stored platform object (and its window/clipboard/
     * pipes) once `state` holds it. */
    state->initialized = 0;
    state->running = 0;
    return YETTY_OK_VOID();
}
