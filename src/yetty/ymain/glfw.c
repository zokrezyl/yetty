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
#include <yetty/yruntime/yruntime.h>
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
    struct yetty_yruntime_ptr_result yrt_res = yetty_yruntime_create(rt);
    if (!YETTY_IS_OK(yrt_res)) {
        pty_factory->ops->destroy(pty_factory);
        return YETTY_ERR(yetty_ycore_void, "ymain: yruntime_create failed", yrt_res);
    }
    struct yetty_yruntime *yruntime = yrt_res.value;

    /* App context still carries the legacy pieces (config, pipes,
     * clipboard, window manager, embedded app_gpu_context, pty_factory)
     * for backwards-compat with terminal/yui/view code that reads through
     * context.app_context.X. The new field is `.runtime`, which is what
     * yetty actually uses to reach the GPU / event-loop / render target. */
    struct yetty_yetty_app_context app_context = {
        .app_gpu_context     = yruntime->gpu.app_gpu_context,
        .config              = rt->config,
        .platform_input_pipe = rt->platform_input_pipe,
        .clipboard_manager   = rt->clipboard_manager,
        .window_manager      = rt->window_manager,
        .pty_factory         = pty_factory,
        .runtime             = yruntime,
    };

    struct yetty_yetty_yetty_result yres = yetty_create(&app_context);
    if (!YETTY_IS_OK(yres)) {
        yetty_yruntime_destroy(yruntime);
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
    /* Strict order: yetty (terminal/yui/tabbar) must die BEFORE yruntime
     * tears down the render target / wgpu await / event loop / device,
     * because pending readback callbacks dereference those. */
    yetty_destroy(yetty);
    yetty_yruntime_destroy(yruntime);
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
