/*
 * yplatform/yplatform/android.c — Android platform subclass.
 *
 *   platform_init — create the config, the android_window, the android_clipboard
 *                   and the cross-thread channels. Called from the NDK glue's
 *                   APP_CMD_INIT_WINDOW handler once the ANativeWindow exists;
 *                   the framebuffer metrics are pushed into the android_window by
 *                   the glue.
 *   platform_run  — the NDK ALooper owns the loop (android_main, in
 *                   ymain/android.c), so run() only hands control back.
 *
 * Plain C — the ANativeWindow + JNI glue live in ymain/android.c. All state
 * private; driven only as init()→run().
 *
 * NOT WIRED IN: foot include (android.gen.c) does not exist yet.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

/* Sibling abstractions (forward-declared; decoupled from generated headers). */
struct yetty_yclass_object_ptr_result yetty_yplatform_android_window_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_android_clipboard_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_android_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe);

/* platform_init slot dispatcher (generated) — run() calls init() through it. */
struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object *obj,
                                                             struct yetty_yclass_object *app,
                                                             int argc, char **argv);

YETTY_YRESULT_DECLARE(yetty_yplatform_android_platform_ptr,
                      struct yetty_yplatform_android_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_android_platform_class_get(void);
struct yetty_yplatform_android_platform_ptr_result yetty_yplatform_android_platform_from(
    struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@yplatform:android_platform") YETTY_ANNOTATE("platform@android")
    YETTY_ANNOTATE("parent@yplatform:platform") yetty_yplatform_android_platform {
    struct yetty_yconfig_config *config;
    struct yetty_yclass_object *window;                 /* yplatform:android_window */
    struct yetty_yclass_object *clipboard;              /* yplatform:android_clipboard */
    struct yetty_ycore_xthread_event_pipe *input_pipe;  /* NDK → worker */
    struct yetty_ycore_xthread_event_pipe *output_pipe; /* worker → NDK */
};

static struct yetty_yplatform_android_platform *android_platform_data(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_android_platform_ptr_result data =
        yetty_yplatform_android_platform_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value;
}

YETTY_ANNOTATE("override@yplatform:android_platform:platform_init")
static struct yetty_ycore_void_result android_platform_init(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv)
{
    (void)app;
    struct yetty_yplatform_android_platform *data = android_platform_data(obj);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "android_platform_init: data_get");
    }

    struct yetty_yconfig_result config_res = yetty_yconfig_create(argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "android_platform_init: config");
    data->config = config_res.value;

    struct yetty_yclass_object_ptr_result window_res = yetty_yplatform_android_window_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "android_platform_init: window create");
    data->window = window_res.value;

    struct yetty_yplatform_input_pipe_result input_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "android_platform_init: input pipe");
    data->input_pipe = input_res.value;
    struct yetty_yplatform_input_pipe_result output_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, output_res, "android_platform_init: output pipe");
    data->output_pipe = output_res.value;

    struct yetty_yclass_object_ptr_result clip_res = yetty_yplatform_android_clipboard_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_res, "android_platform_init: clipboard create");
    data->clipboard = clip_res.value;
    struct yetty_ycore_void_result clip_cfg =
        yetty_yplatform_android_clipboard_configure(data->clipboard, data->input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_cfg, "android_platform_init: clipboard configure");

    ydebug("android_platform: init complete");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:android_platform:platform_run")
static struct yetty_ycore_void_result android_platform_run(struct yetty_yclass_object *obj,
                                                           struct yetty_yclass_object *app,
                                                           int argc, char **argv)
{
    struct yetty_ycore_void_result init_res = yetty_yplatform_platform_init(obj, app, argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, init_res, "android_platform_run: init");
    /* android_main (ymain/android.c) owns the ALooper run loop; nothing to drive
     * here. */
    ydebug("android_platform: run (loop owned by the NDK ALooper)");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/yplatform/yplatform/android.c"
