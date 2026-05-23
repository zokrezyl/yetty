/*
 * ymain/glfw.c - yetty's main() on GLFW desktop platforms.
 *
 * Thin wrapper: hands main() over to yinit, supplies a worker that
 * does the yetty-specific bits (PTY factory, yetty_create / _run /
 * _destroy). All bootstrap (paths, config, window, surface, event
 * pipes, OS event loop) lives in yinit.
 */

#include <yetty/yplatform/compat.h>     /* setenv shim on Windows MSVC */
#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>

static struct yetty_ycore_void_result
yetty_worker(struct yetty_yinit_runtime *rt, void *user)
{
    (void)user;

    /* PTY factory — yetty-specific. */
    struct yetty_yplatform_pty_factory_ptr_result pf_res =
        yetty_yplatform_pty_factory_create(rt->config, NULL);
    if (!YETTY_IS_OK(pf_res)) {
        return YETTY_ERR(yetty_ycore_void, "ymain: pty_factory_create failed", pf_res);
    }
    struct yetty_yplatform_pty_factory *pty_factory = pf_res.value;

    /* Generic GPU / event-loop / VNC / RPC / render-target bring-up
     * (adapter, device, queue, allocator, msdf, present mode, surface
     * config, ...). Outlives the yetty terminal instance below. */
    struct yetty_yframework_ptr_result yrt_res = yetty_yframework_create(rt);
    if (!YETTY_IS_OK(yrt_res)) {
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "ymain: yframework_create failed", yrt_res);
    }
    struct yetty_yframework *yframework = yrt_res.value;

    /* yetty's two inputs: the generic runtime (GPU/event/RPC/render-
     * target services + borrowed config/pipes/clipboard/window from
     * yinit) and the yetty-specific pty_factory. */
    struct yetty_yetty_yetty_result yres = yetty_create(yframework, pty_factory);
    if (!YETTY_IS_OK(yres)) {
        yetty_yframework_destroy(yframework);
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "ymain: yetty_create failed", yres);
    }
    struct yetty_yetty_yetty *yetty = yres.value;

    /* Initial resize event so the first frame uses the live framebuffer
     * dimensions instead of the config defaults. */
    struct yetty_yui_event ev = {
        .type   = YETTY_YCORE_RESIZE,
        .resize = {.width  = (float)rt->surface_width,
                   .height = (float)rt->surface_height},
    };
    rt->platform_input_pipe->ops->write(rt->platform_input_pipe, &ev, sizeof(ev));

    struct yetty_ycore_void_result run_res = yetty_run(yetty);

    ydebug("ymain: yetty_run returned, tearing down");
    /* Strict order: yetty (terminal/yui/tabbar) must die BEFORE yframework
     * tears down the render target / wgpu await / event loop / device,
     * because pending readback callbacks dereference those. */
    yetty_destroy(yetty);
    yetty_yframework_destroy(yframework);
    pty_factory->ops->destroy(pty_factory);
    return run_res;
}

int main(int argc, char **argv)
{
    /* Advertise ourselves via the de-facto TERM_PROGRAM convention so
     * child processes (PTY shells, tools like ycat) can detect a yetty
     * terminal and adapt their output. Done here at the top of main so
     * every fork inherits it. */
    setenv("TERM_PROGRAM", "yetty", 1);

    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    return yetty_yinit_run(argc, argv, &cfg, yetty_worker, NULL);
}
