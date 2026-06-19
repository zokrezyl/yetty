/* webasm.c — yetty's WebAssembly entry point.
 *
 * The generic bootstrap (paths, assets, config, window, GPU surface,
 * input pipe + HTML5 callbacks, runtime assembly) lives in
 * yetty_yinit_run (yinit/webasm-run.c), shared with the ygui-tool
 * wasms. This file is just yetty's worker: the PTY factory, the
 * optional in-browser netstack, yframework, the terminal app, and the
 * run loop — mirroring the desktop worker in ymain/glfw.c.
 *
 * Single-threaded model: the worker runs on the main thread and
 * returns once yetty_run() registers the emscripten main loop; HTML5
 * callbacks then write events to the platform input pipe and the
 * browser drives frames.
 */

#include <yetty/yetty/yetty.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ynet/netstack.h>
#include <yetty/ytrace/ytrace.h>

/* Implemented in yplatform/window/webasm.c. */
void yetty_yplatform_webasm_get_framebuffer_size(int *width, int *height);

/* yetty's worker — runs after yetty_yinit_run has brought up the
 * window / GPU surface / config / input pipe. */
static struct yetty_ycore_void_result yetty_webasm_worker(struct yetty_yinit_runtime *rt,
                                                          void *user)
{
    (void)user;
    struct yetty_yconfig_config *config = rt->config;

    /* Optional in-browser TCP/IP stack (lwIP over an L2 relay WebSocket),
     * brought up when --net-relay / net/relay is set. Independent of the
     * PTY session mode and self-driving via emscripten websocket +
     * interval callbacks; the program-lifetime pointer is intentionally
     * not stored (the runtime outlives the worker and the callbacks own
     * it). */
    const char *net_relay = config->ops->get_string(config, YETTY_YCONFIG_KEY_NET_RELAY, "");
    if (net_relay && net_relay[0]) {
        struct yetty_ynet_netstack_ptr_result netstack_result =
            yetty_ynet_netstack_create(net_relay);
        if (!YETTY_IS_OK(netstack_result)) {
            ywarn("yetty_webasm_worker: netstack create failed: %s", netstack_result.error.msg);
            yetty_ycore_error_destroy(netstack_result.error);
        }
    }

    /* PTY factory — yetty-specific. */
    struct yetty_yplatform_pty_factory_ptr_result pty_factory_result =
        yetty_yplatform_pty_factory_create(config, NULL);
    if (!YETTY_IS_OK(pty_factory_result)) {
        return YETTY_ERR(yetty_ycore_void, "webasm: pty_factory_create failed", pty_factory_result);
    }
    struct yetty_yplatform_pty_factory *pty_factory = pty_factory_result.value;
    ydebug("webasm worker: PtyFactory created");

    /* Generic GPU / event-loop / render-target bring-up. */
    struct yetty_yframework_ptr_result yframework_result = yetty_yframework_create(rt);
    if (!YETTY_IS_OK(yframework_result)) {
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "webasm: yframework_create failed", yframework_result);
    }
    struct yetty_yframework *yframework = yframework_result.value;

    /* The terminal app. */
    struct yetty_yetty_yetty_result yetty_result = yetty_create(yframework, pty_factory);
    if (!YETTY_IS_OK(yetty_result)) {
        (void)yetty_yframework_destroy(yframework);
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "webasm: yetty_create failed", yetty_result);
    }
    struct yetty_yetty_yetty *yetty = yetty_result.value;
    ydebug("webasm worker: Yetty created");

    /* Initial resize so the first frame uses the live framebuffer size. */
    int fb_width, fb_height;
    yetty_yplatform_webasm_get_framebuffer_size(&fb_width, &fb_height);
    struct yetty_yui_event event = {
        .type = YETTY_YCORE_RESIZE,
        .resize = {.width = (float)fb_width, .height = (float)fb_height}};
    rt->platform_input_pipe->ops->write(rt->platform_input_pipe, &event, sizeof(event));
    ydebug("webasm worker: posted initial resize %dx%d", fb_width, fb_height);

    /* Render runs on the main thread (rAF). yetty_run returns
     * immediately; the JS event loop drives subsequent ticks. The
     * terminal + yframework intentionally leak for program lifetime —
     * the browser keeps running the loop after the worker returns. */
    ydebug("webasm worker: starting Yetty");
    struct yetty_ycore_void_result run_result = yetty_run(yetty);
    if (!YETTY_IS_OK(run_result)) {
        return YETTY_ERR(yetty_ycore_void, "webasm: yetty_run failed", run_result);
    }

    ydebug("webasm worker: returning (event loop continues asynchronously)");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    /* Force-enable all ytrace points by default on webasm so the
     * headless Chrome test and browser debugging see the full trace
     * stream without env wiring on the JS side. */
    //setenv("YTRACE_DEFAULT_ON", "yes", 1);

    struct yetty_ycore_int_result run_result =
        yetty_yinit_run(argc, argv, yetty_webasm_worker, NULL);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yetty (webasm) fatal", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
