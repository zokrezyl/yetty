/*
 * yetty/app.c — yclass class `yetty:app`: the yetty terminal as a yapp.
 *
 * Subclass of yapp:app. This is the program-specific half of the startup: the
 * platform brings up config / window / clipboard / GPU surface / channels and
 * hands the assembled runtime to run, which builds the PTY factory, the framework
 * and the terminal, runs the terminal loop, and tears down. The same platform
 * bootstrap drives every standalone app — only this app object differs.
 *
 * yclass: the only hand-written file is this annotated .c; app.gen.c is #included
 * at the foot. Both slots are app overrides of the yapp:app virtuals.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yapp/app.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/ytrace/ytrace.h>
#if defined(YETTY_HAS_YMUX)
#include <yetty/ymux/bootstrap.h>
#include <yetty/ymux/client-pty.h>
#endif

struct YETTY_ANNOTATE("class@yetty:app") YETTY_ANNOTATE("parent@yapp:app") yetty_yetty_app {
    char reserved;
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yetty_app_ptr, struct yetty_yetty_app *);
struct yetty_yclass_ptr_result yetty_yetty_app_class_get(void);
struct yetty_yetty_app_ptr_result yetty_yetty_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yetty_app_create(struct yetty_yclass_ctx *ctx);

YETTY_ANNOTATE("override@yapp:app:init")
static struct yetty_ycore_void_result yetty_app_init(struct yetty_yclass_object *obj,
                                                     struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yapp:app:run")
static struct yetty_ycore_void_result yetty_app_run(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *platform)
{
    (void)obj;

    struct yetty_yconfig_config_ptr_result config_res = yetty_yplatform_platform_config(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "yetty:app: platform config");
    struct yetty_yconfig_config *config = config_res.value;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "yetty:app: platform gpu context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "yetty:app: platform input pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;

    if (!config || !gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "yetty:app: platform state not populated");
    }

    struct yetty_yplatform_pty_factory_ptr_result pty_res;
#if defined(YETTY_HAS_YMUX)
    /* Client mode: the terminal's PTY bridges to a remote ymux server pane
     * instead of a local forked shell. Everything else (window, GPU, renderer)
     * is the normal windowed path.
     *
     *   --ymux-attach[=HOST:PORT] → attach to an explicit server (no spawn).
     *   --ymux                    → tmux-style: connect to the local server or
     *                               spawn a detached one, then attach.
     *
     * On any client-setup failure we fall back to a local shell so a plain
     * yetty always comes up. */
    const char *ymux_attach = config->ops->get_string(config, YETTY_YCONFIG_KEY_YMUX_ATTACH, NULL);
    const char *ymux_auto = config->ops->get_string(config, YETTY_YCONFIG_KEY_YMUX_AUTO, NULL);
    char ymux_host[256] = "127.0.0.1";
    int ymux_port = 9998;
    int ymux_client = 0;

    if (ymux_attach) {
        const char *colon = strrchr(ymux_attach, ':');
        if (colon && colon != ymux_attach) {
            size_t host_len = (size_t)(colon - ymux_attach);
            if (host_len >= sizeof(ymux_host)) {
                host_len = sizeof(ymux_host) - 1;
            }
            memcpy(ymux_host, ymux_attach, host_len);
            ymux_host[host_len] = '\0';
            ymux_port = atoi(colon + 1);
            if (ymux_port <= 0) {
                ymux_port = 9998;
            }
        } else if (ymux_attach[0] != '\0') {
            snprintf(ymux_host, sizeof(ymux_host), "%s", ymux_attach);
        }
        yinfo("yetty:app: ymux client mode, attaching to %s:%d", ymux_host, ymux_port);
        ymux_client = 1;
    } else if (ymux_auto) {
        const char *port_str = config->ops->get_string(config, YETTY_YCONFIG_KEY_YMUX_PORT, NULL);
        if (port_str) {
            int parsed = atoi(port_str);
            if (parsed > 0) {
                ymux_port = parsed;
            }
        }
        if (yetty_ymux_ensure_server(ymux_host, ymux_port)) {
            yinfo("yetty:app: ymux auto mode, attaching to %s:%d", ymux_host, ymux_port);
            ymux_client = 1;
        } else {
            ywarn("yetty:app: ymux auto mode could not reach/spawn a server — "
                  "falling back to a local shell");
        }
    }

    if (ymux_client) {
        pty_res = yetty_ymux_client_pty_factory_create(ymux_host, ymux_port, /*pane=*/0);
    } else {
        pty_res = yetty_yplatform_pty_factory_create(config, NULL);
    }
#else
    pty_res = yetty_yplatform_pty_factory_create(config, NULL);
#endif
    if (!YETTY_IS_OK(pty_res)) {
        return YETTY_ERR(yetty_ycore_void, "yetty:app: pty_factory_create failed", pty_res);
    }
    struct yetty_yplatform_pty_factory *pty_factory = pty_res.value;

    struct yetty_yframework_ptr_result framework_res = yetty_yframework_create(platform);
    if (!YETTY_IS_OK(framework_res)) {
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "yetty:app: yframework_create failed", framework_res);
    }
    struct yetty_yframework *framework = framework_res.value;

    struct yetty_yetty_yetty_result yetty_res = yetty_create(framework, pty_factory);
    if (!YETTY_IS_OK(yetty_res)) {
        (void)yetty_yframework_destroy(framework);
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "yetty:app: yetty_create failed", yetty_res);
    }
    struct yetty_yetty_yetty *yetty = yetty_res.value;

    /* First frame at the live framebuffer size, not the config default. */
    struct yetty_yui_event resize = {
        .type = YETTY_YCORE_RESIZE,
        .resize = {.width = (float)gpu->surface_width, .height = (float)gpu->surface_height},
    };
    input_pipe->ops->write(input_pipe, &resize, sizeof(resize));

    struct yetty_ycore_void_result run_res = yetty_run(yetty);

#ifndef __EMSCRIPTEN__
    /* Desktop/native: event_loop->start() (inside yetty_run) blocked until the
     * terminal was shut down, so by the time we get here the app is really
     * exiting — tear everything down.
     *
     * On webasm yetty_run is NON-blocking: event_loop->start() registers the
     * emscripten main loop and returns immediately, and the browser drives
     * frames only AFTER this function (and main()) return. Destroying yetty /
     * yframework / the pty factory here would tear the terminal and the WebGPU
     * device down on the very first tick, leaving a blank canvas. The runtime
     * must outlive this call for program lifetime — the browser owns the loop —
     * so on webasm it is intentionally leaked (mirrors the pre-refactor webasm
     * worker). */
    (void)yetty_destroy(yetty);
    (void)yetty_yframework_destroy(framework);
    pty_factory->ops->destroy(pty_factory);
#endif
    return run_res;
}

/* Install the yetty app's accessor-lookup hook (the yetty app object is proxied
 * over RPC, so it must be resolvable by name) and then build the app. The
 * generic bootstrap calls only this one forwarder, so the registration folds in
 * here rather than living behind a separate entry point. */
struct yetty_ycore_void_result yetty_yetty_register(void);

struct yetty_yclass_object_ptr_result yetty_yapp_create_app(struct yetty_yclass_ctx *ctx)
{
    struct yetty_ycore_void_result reg = yetty_yetty_register();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, reg, "yetty_yapp_create_app: register");
    return yetty_yetty_app_create(ctx);
}

#include "app.gen.c"
