/*
 * yplatform/yplatform/ios-tvos.c — iOS / tvOS platform subclass.
 *
 *   platform_init — create the config, the ios_window, the ios_clipboard and
 *                   the cross-thread channels. Called from the UIKit view
 *                   controller (viewDidLoad) once the CAMetalLayer-backed view
 *                   exists; the layer + framebuffer metrics are pushed into the
 *                   ios_window by the Obj-C bootstrap.
 *   platform_run  — UIKit owns the loop (UIApplicationMain, in ymain/ios-tvos),
 *                   so run() only hands control back.
 *
 * Plain C (no Obj-C) so the yclass generator parses it; the UIKit glue lives in
 * ymain/ios-tvos.m. All state private; driven only as init()→run().
 *
 * NOT WIRED IN: foot include (ios-tvos.gen.c) does not exist yet.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

/* Sibling abstractions (forward-declared; decoupled from generated headers). */
struct yetty_yclass_object_ptr_result yetty_yplatform_ios_window_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_ios_clipboard_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_ios_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe);

/* platform_init slot dispatcher (generated) — run() calls init() through it. */
struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object *obj,
                                                             int argc, char **argv);

YETTY_YRESULT_DECLARE(yetty_yplatform_ios_platform_ptr, struct yetty_yplatform_ios_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_ios_platform_class_get(void);
struct yetty_yplatform_ios_platform_ptr_result yetty_yplatform_ios_platform_from(
    struct yetty_yclass_object *obj);

struct [[clang::annotate("class@yplatform:ios_platform")]] [[clang::annotate(
    "platform@ios")]] [[clang::annotate("parent@yplatform:platform")]]
yetty_yplatform_ios_platform {
    struct yetty_yconfig_config *config;
    struct yetty_yclass_object *window;                 /* yplatform:ios_window */
    struct yetty_yclass_object *clipboard;              /* yplatform:ios_clipboard */
    struct yetty_ycore_xthread_event_pipe *input_pipe;  /* UIKit → worker */
    struct yetty_ycore_xthread_event_pipe *output_pipe; /* worker → UIKit */
};

static struct yetty_yplatform_ios_platform *ios_platform_data(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_ios_platform_ptr_result data = yetty_yplatform_ios_platform_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value;
}

[[clang::annotate("override@yplatform:ios_platform:platform_init")]]
static struct yetty_ycore_void_result ios_platform_init(struct yetty_yclass_object *obj, int argc,
                                                        char **argv)
{
    struct yetty_yplatform_ios_platform *data = ios_platform_data(obj);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "ios_platform_init: data_get");
    }

    struct yetty_yconfig_result config_res = yetty_yconfig_create(argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "ios_platform_init: config");
    data->config = config_res.value;

    /* UIKit owns the view/layer; the ios_window just caches metrics the Obj-C
     * bootstrap pushes in via yetty_yplatform_ios_window_set_metrics. */
    struct yetty_yclass_object_ptr_result window_res = yetty_yplatform_ios_window_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "ios_platform_init: window create");
    data->window = window_res.value;

    struct yetty_yplatform_input_pipe_result input_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "ios_platform_init: input pipe");
    data->input_pipe = input_res.value;
    struct yetty_yplatform_input_pipe_result output_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, output_res, "ios_platform_init: output pipe");
    data->output_pipe = output_res.value;

    struct yetty_yclass_object_ptr_result clip_res = yetty_yplatform_ios_clipboard_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_res, "ios_platform_init: clipboard create");
    data->clipboard = clip_res.value;
    struct yetty_ycore_void_result clip_cfg =
        yetty_yplatform_ios_clipboard_configure(data->clipboard, data->input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_cfg, "ios_platform_init: clipboard configure");

    ydebug("ios_platform: init complete");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:ios_platform:platform_run")]]
static struct yetty_ycore_void_result ios_platform_run(struct yetty_yclass_object *obj, int argc,
                                                       char **argv)
{
    struct yetty_ycore_void_result init_res = yetty_yplatform_platform_init(obj, argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, init_res, "ios_platform_run: init");
    /* UIApplicationMain (ymain/ios-tvos.m) owns the run loop; nothing to drive
     * here. */
    ydebug("ios_platform: run (loop owned by UIKit/UIApplicationMain)");
    return YETTY_OK_VOID();
}

#include "ios-tvos.gen.c"
