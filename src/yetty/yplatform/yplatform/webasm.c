/*
 * yplatform/yplatform/webasm.c — WebAssembly platform subclass.
 *
 *   platform_init — create the config, the webasm_window (the canvas already
 *                   exists), the webasm_clipboard and the cross-thread channels.
 *   platform_run  — the browser owns the loop (emscripten drives it via the JS
 *                   asset preload + emscripten_set_main_loop), so run() only
 *                   hands control back; the loop registration lives in
 *                   ymain/webasm.c.
 *
 * All state private; driven only as init()→run() from ymain/webasm.c.
 *
 * NOT WIRED IN: foot include (webasm.gen.c) does not exist yet.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

/* Sibling abstractions (forward-declared; decoupled from generated headers). */
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_window_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_clipboard_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_webasm_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe);

/* platform_init slot dispatcher (generated) — run() calls init() through it. */
struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object *obj,
                                                             int argc, char **argv);

YETTY_YRESULT_DECLARE(yetty_yplatform_webasm_platform_ptr,
                      struct yetty_yplatform_webasm_platform *);
struct yetty_yclass_ptr_result yetty_yplatform_webasm_platform_class_get(void);
struct yetty_yplatform_webasm_platform_ptr_result yetty_yplatform_webasm_platform_from(
    struct yetty_yclass_object *obj);

struct [[clang::annotate("class@yplatform:webasm_platform")]] [[clang::annotate(
    "platform@webasm")]] [[clang::annotate("parent@yplatform:platform")]]
yetty_yplatform_webasm_platform {
    struct yetty_yconfig_config *config;
    struct yetty_yclass_object *window;                 /* yplatform:webasm_window */
    struct yetty_yclass_object *clipboard;              /* yplatform:webasm_clipboard */
    struct yetty_ycore_xthread_event_pipe *input_pipe;  /* canvas → worker */
    struct yetty_ycore_xthread_event_pipe *output_pipe; /* worker → canvas */
};

static struct yetty_yplatform_webasm_platform *webasm_platform_data(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_webasm_platform_ptr_result data =
        yetty_yplatform_webasm_platform_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value;
}

[[clang::annotate("override@yplatform:webasm_platform:platform_init")]]
static struct yetty_ycore_void_result webasm_platform_init(struct yetty_yclass_object *obj,
                                                           int argc, char **argv)
{
    struct yetty_yplatform_webasm_platform *data = webasm_platform_data(obj);
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "webasm_platform_init: data_get");
    }

    struct yetty_yconfig_result config_res = yetty_yconfig_create(argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "webasm_platform_init: config");
    data->config = config_res.value;

    /* The canvas already exists (page/preload); just bind a window object. */
    struct yetty_yclass_object_ptr_result window_res = yetty_yplatform_webasm_window_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "webasm_platform_init: window create");
    data->window = window_res.value;

    struct yetty_yplatform_input_pipe_result input_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "webasm_platform_init: input pipe");
    data->input_pipe = input_res.value;
    struct yetty_yplatform_input_pipe_result output_res = yetty_platform_input_pipe_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, output_res, "webasm_platform_init: output pipe");
    data->output_pipe = output_res.value;

    struct yetty_yclass_object_ptr_result clip_res = yetty_yplatform_webasm_clipboard_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_res, "webasm_platform_init: clipboard create");
    data->clipboard = clip_res.value;
    struct yetty_ycore_void_result clip_cfg =
        yetty_yplatform_webasm_clipboard_configure(data->clipboard, data->input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clip_cfg, "webasm_platform_init: clipboard configure");

    ydebug("webasm_platform: init complete");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_platform:platform_run")]]
static struct yetty_ycore_void_result webasm_platform_run(struct yetty_yclass_object *obj, int argc,
                                                          char **argv)
{
    struct yetty_ycore_void_result init_res = yetty_yplatform_platform_init(obj, argc, argv);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, init_res, "webasm_platform_run: init");
    /* The browser event loop is driven by emscripten (emscripten_set_main_loop
     * registered from ymain/webasm.c); there is no blocking loop to run here. */
    ydebug("webasm_platform: run (loop owned by the browser/emscripten)");
    return YETTY_OK_VOID();
}

#include "webasm.gen.c"
