/*
 * yetty.c - Main yetty implementation
 */

#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/yplatform/ywebgpu.h>
#include <yetty/ycore/event.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/render-target.h>
#ifdef YETTY_HAS_X11_TILE
#include <yetty/yrender/render-target-x11-tile.h>
#endif
#include <yetty/yterm/terminal.h>
#include <yetty/webgpu/error.h>
#include <yetty/ywebgpu/utils.h>
#include <yetty/ywebgpu/limits.h>
#include <yetty/ywebgpu/request.h>
#include <yetty/ycore/math.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yui/workspace.h>
#include <yetty/yui/tile.h>
#include <yetty/yui/view.h>
#include <yetty/yrpc/rpc-server.h>
#include <yetty/yvnc/vnc-server.h>
#include <yetty/platform/platform-input-pipe.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

/* Yetty instance */
struct yetty_yetty_yetty {
    struct yetty_context context;
    struct yetty_yui_workspace *workspace;
    struct yetty_yplatform_event_loop *event_loop;
    struct yetty_ycore_event_listener listener;

    /* WebGPU state (owned by Yetty) */
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;

    /* Coroutine-aware wgpu await machinery (loop-thread tick + completion
     * routing). Owned by Yetty; lifetime spans event_loop + wgpu instance. */
    struct yetty_yplatform_wgpu *wgpu;

    /* Big render target - window-sized texture with surface for presentation */
    struct yetty_ypaint_core_target *render_target;

    /* RPC server (optional, enabled via -r/--rpc-socket) */
    struct yetty_yrpc_server *rpc_server;
    yetty_ycore_timer_id rpc_timer_id;
    struct yetty_ycore_event_listener rpc_timer_listener;

    /* VNC server (optional, for --vnc-server or --vnc-headless) */
    struct yetty_yvnc_server *vnc_server;

    /* Visual (shader-level) zoom state — applied by the final blend. */
    float visual_zoom_scale;    /* 1.0 = off; clamped to [1.0, 100.0] */
    float visual_zoom_offset_x; /* source-pixel pan */
    float visual_zoom_offset_y;

    /* Drag-to-pan state while visual zoom is active. Mouse button 0 enters
     * drag; move events translate into ZOOM_VISUAL_PAN with the screen-space
     * delta; button-up exits. */
    int visual_zoom_dragging;
    float visual_zoom_drag_last_x;
    float visual_zoom_drag_last_y;

    /* Cached window size so ZOOM_CELL_SIZE can re-post a RESIZE that
     * forces the terminal to re-derive cols/rows. */
    float window_width;
    float window_height;
};

/* GLFW modifier bit layout (matches glfw-main.c and the VNC shim). */
#define YETTY_MOD_SHIFT 0x0001
#define YETTY_MOD_CONTROL 0x0002

/* Clamp helpers — no float.h dependency needed. */
/* yetty_clampf moved to <yetty/ycore/math.h> */

/* Event posting / listener registration moved to <yetty/yevent/dispatch.h> */

/*===========================================================================
 * Event handling
 *===========================================================================*/

static struct yetty_ycore_int_result yetty_event_handler(
    struct yetty_ycore_event_listener *listener, const struct yetty_yui_event *event)
{
    struct yetty_yetty_yetty *yetty = container_of(listener, struct yetty_yetty_yetty, listener);

    /* X11 Expose / window-uncover: mark every tile dirty on damage-aware
     * targets so the next render actually repaints the window. Fall through
     * to the normal RENDER path below (no early return). */
    if (event->type == YETTY_YCORE_WINDOW_REFRESH) {
        if (yetty->render_target && yetty->render_target->ops->refresh_full) {
            yetty->render_target->ops->refresh_full(yetty->render_target);
        }
        /* Re-dispatch as a normal RENDER so the single render pipeline runs
         * exactly once, via the same code path used by all other triggers. */
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return yetty_event_handler(listener, &re);
    }

    /* Handle RENDER event directly - yetty owns the render cycle */
    if (event->type == YETTY_YCORE_RENDER) {
        if (!yetty->render_target) {
            yerror("yetty: RENDER but no render_target");
            return YETTY_OK(yetty_ycore_int, 0);
        }

        /* Back-pressure: if the target is still flushing an async readback
         * (x11-tile, vnc with a pending wire send), skip the entire
         * pipeline — running workspace_render now would submit GPU work
         * that feeds a present() we'd just drop, starving the driver of
         * handles (NVIDIA fd exhaustion during bursty scroll). Tell the
         * target we skipped so it knows to fire a catch-up render via
         * on_idle once it's free again — otherwise the skip is silent and
         * the display stays one event behind (visible as the "one-char
         * delay" in nvim bursts). */
        if (yetty->render_target->ops->is_busy &&
            yetty->render_target->ops->is_busy(yetty->render_target)) {
            if (yetty->render_target->ops->notify_render_skipped) {
                yetty->render_target->ops->notify_render_skipped(yetty->render_target);
            }
            ydebug("yetty: RENDER skipped (target busy)");
            return YETTY_OK(yetty_ycore_int, 1);
        }

        ydebug("yetty: RENDER event - calling workspace render");

        ytime_start(full_frame);

        /* Clear the big target once before rendering all panes */
        ytime_start(clear);
        struct yetty_ycore_void_result clr_res =
            yetty->render_target->ops->clear(yetty->render_target);
        ytime_report(clear);
        if (!YETTY_IS_OK(clr_res)) {
            yerror("yetty: clear failed: %s", clr_res.error.msg);
        }

        /* Render workspace tree - pass render_target down */
        ytime_start(workspace_render);
        if (yetty->workspace) {
            struct yetty_ycore_void_result res =
                yetty_yui_workspace_render(yetty->workspace, yetty->render_target);
            if (!YETTY_IS_OK(res)) {
                yerror("yetty: workspace render failed: %s", res.error.msg);
            }
        }
        ytime_report(workspace_render);

        /* Present the big target to surface */
        ytime_start(present);
        struct yetty_ycore_void_result res =
            yetty->render_target->ops->present(yetty->render_target);
        ytime_report(present);
        if (!YETTY_IS_OK(res)) {
            yerror("yetty: present failed: %s", res.error.msg);
        }

        ytime_report(full_frame);

        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Translate raw Ctrl-modifier scrolls into named zoom events, asynchronously.
     *
     * Why a separate named event (instead of branching inline here): anything
     * that wants to trigger zoom — RPC, keyboard remapping, macro replay — can
     * push the same ZOOM_VISUAL / ZOOM_CELL_SIZE event. The raw scroll keeps
     * flowing to the workspace unchanged when no zoom modifier is held. */
    if (event->type == YETTY_YCORE_SCROLL) {
        int mods = event->scroll.mods;
        bool ctrl = (mods & YETTY_MOD_CONTROL) != 0;
        bool shift = (mods & YETTY_MOD_SHIFT) != 0;

        if (ctrl && shift) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_CELL_SIZE;
            ev.zoom_cell_size.delta = event->scroll.dy * 0.04f;
            yetty_yevent_post_async(yetty->context.app_context.platform_input_pipe, &ev);
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ctrl) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_VISUAL;
            ev.zoom_visual.delta = event->scroll.dy * 0.1f;
            ev.zoom_visual.anchor_x = event->scroll.x;
            ev.zoom_visual.anchor_y = event->scroll.y;
            yetty_yevent_post_async(yetty->context.app_context.platform_input_pipe, &ev);
            return YETTY_OK(yetty_ycore_int, 1);
        }
        /* No zoom modifier — fall through to workspace forwarding below. */
    }

    /* While visual zoom is active, the keyboard is captured: Enter and Esc
     * exit the zoom; every other key/char event is silently swallowed so the
     * shell under the terminal doesn't see a phantom keystroke while the
     * user is just trying to look around. */
    if (yetty->visual_zoom_scale > 1.0f &&
        (event->type == YETTY_YCORE_KEY_DOWN || event->type == YETTY_YCORE_KEY_UP ||
         event->type == YETTY_YCORE_CHAR)) {
        if (event->type == YETTY_YCORE_KEY_DOWN &&
            (event->key.key == 256 /* ESC */ || event->key.key == 257 /* ENTER */)) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_VISUAL;
            ev.zoom_visual.reset = 1;
            yetty_yevent_post_async(yetty->context.app_context.platform_input_pipe, &ev);
            ydebug("yetty: visual zoom EXIT (key=%d)", event->key.key);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Mouse drag translation while visual zoom is active. Button-0 down starts
     * a drag; subsequent moves emit ZOOM_VISUAL_PAN with the pixel delta; up
     * ends it. We swallow these events so the terminal underneath doesn't see
     * a phantom click-drag. */
    if (event->type == YETTY_YCORE_MOUSE_DOWN && event->mouse.button == 0 &&
        yetty->visual_zoom_scale > 1.0f) {
        yetty->visual_zoom_dragging = 1;
        yetty->visual_zoom_drag_last_x = event->mouse.x;
        yetty->visual_zoom_drag_last_y = event->mouse.y;
        ydebug("yetty: visual zoom drag START at (%.1f,%.1f)", event->mouse.x, event->mouse.y);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if ((event->type == YETTY_YCORE_MOUSE_MOVE || event->type == YETTY_YCORE_MOUSE_DRAG) &&
        yetty->visual_zoom_dragging) {
        float dx = event->mouse.x - yetty->visual_zoom_drag_last_x;
        float dy = event->mouse.y - yetty->visual_zoom_drag_last_y;
        yetty->visual_zoom_drag_last_x = event->mouse.x;
        yetty->visual_zoom_drag_last_y = event->mouse.y;
        if (dx != 0.0f || dy != 0.0f) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_VISUAL_PAN;
            ev.zoom_visual_pan.dx = dx;
            ev.zoom_visual_pan.dy = dy;
            yetty_yevent_post_async(yetty->context.app_context.platform_input_pipe, &ev);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (event->type == YETTY_YCORE_MOUSE_UP && yetty->visual_zoom_dragging) {
        yetty->visual_zoom_dragging = 0;
        ydebug("yetty: visual zoom drag END");
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* ZOOM_VISUAL — update the blend-uniform state on the big render target.
     * Zoom is anchored at the mouse cursor: the point under the cursor before
     * the scale change stays under the cursor after. This matches the user's
     * mental model of "zoom here, not at the center of the window".
     *
     * Derivation: the blend shader computes the source sample UV as
     *     s_uv = 0.5 + (uv - 0.5)/scale + off/size
     * For the anchor at screen pixel m = (mx,my), m_uv = m/size. Pinning
     * s_uv_mouse across a scale change yields
     *     off_new = off_old + (m - size/2) * (1/scale_old - 1/scale_new).
     */
    if (event->type == YETTY_YCORE_ZOOM_VISUAL) {
        if (event->zoom_visual.reset) {
            yetty->visual_zoom_scale = 1.0f;
            yetty->visual_zoom_offset_x = 0.0f;
            yetty->visual_zoom_offset_y = 0.0f;
        } else {
            float old_scale = yetty->visual_zoom_scale;
            float new_scale = yetty_clampf(old_scale + event->zoom_visual.delta, 1.0f, 100.0f);

            if (new_scale <= 1.0f) {
                /* Bottom of the range — collapse offsets too so the next
                 * zoom-in starts fresh around the new anchor. */
                yetty->visual_zoom_scale = 1.0f;
                yetty->visual_zoom_offset_x = 0.0f;
                yetty->visual_zoom_offset_y = 0.0f;
            } else {
                float W = yetty->window_width > 0 ? yetty->window_width : 1.0f;
                float H = yetty->window_height > 0 ? yetty->window_height : 1.0f;
                float cx = W * 0.5f, cy = H * 0.5f;
                float mx = event->zoom_visual.anchor_x;
                float my = event->zoom_visual.anchor_y;
                /* First zoom step from 1.0 — seed the offset to the anchor
                 * so the subsequent delta math is continuous. */
                if (old_scale > 0.0f && new_scale != old_scale) {
                    float k = (1.0f / old_scale) - (1.0f / new_scale);
                    yetty->visual_zoom_offset_x += (mx - cx) * k;
                    yetty->visual_zoom_offset_y += (my - cy) * k;
                }
                yetty->visual_zoom_scale = new_scale;

                /* Clamp pan so we never reveal beyond-edge source pixels.
                 * Matches POC clampVisualZoomOffset. */
                float max_off_x = (W * 0.5f) * (1.0f - 1.0f / new_scale);
                float max_off_y = (H * 0.5f) * (1.0f - 1.0f / new_scale);
                if (max_off_x < 0) {
                    max_off_x = 0;
                }
                if (max_off_y < 0) {
                    max_off_y = 0;
                }
                yetty->visual_zoom_offset_x =
                    yetty_clampf(yetty->visual_zoom_offset_x, -max_off_x, max_off_x);
                yetty->visual_zoom_offset_y =
                    yetty_clampf(yetty->visual_zoom_offset_y, -max_off_y, max_off_y);
            }
        }

        ydebug("yetty: ZOOM_VISUAL scale=%.2f off=(%.1f,%.1f)", yetty->visual_zoom_scale,
               yetty->visual_zoom_offset_x, yetty->visual_zoom_offset_y);
        /* Push the zoom into every layer's own uniforms so each fragment
         * shader re-evaluates MSDF/SDF at the transformed pixel. Doing this
         * at the blend stage was a post-rasterization bitmap stretch and
         * produced blurry text at large scales. */
        {
            struct yetty_yui_event apply = {0};
            apply.type = YETTY_YCORE_ZOOM_VISUAL_APPLY;
            apply.zoom_visual_apply.scale = yetty->visual_zoom_scale;
            apply.zoom_visual_apply.offset_x = yetty->visual_zoom_offset_x;
            apply.zoom_visual_apply.offset_y = yetty->visual_zoom_offset_y;
            if (yetty->workspace) {
                yetty_yui_workspace_on_event(yetty->workspace, &apply);
            }
        }
        if (yetty->event_loop && yetty->event_loop->ops->request_render) {
            yetty->event_loop->ops->request_render(yetty->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* ZOOM_VISUAL_PAN — translate the zoomed view by a screen-space delta.
     * Normally produced by drag translation below, but also dispatchable by
     * RPC/kb. The offset moves opposite to the drag (so content follows the
     * cursor) and is scaled down by the zoom factor (a pixel of drag moves
     * fewer source pixels when zoomed in). */
    if (event->type == YETTY_YCORE_ZOOM_VISUAL_PAN) {
        if (yetty->visual_zoom_scale > 1.0f) {
            float inv = 1.0f / yetty->visual_zoom_scale;
            yetty->visual_zoom_offset_x -= event->zoom_visual_pan.dx * inv;
            yetty->visual_zoom_offset_y -= event->zoom_visual_pan.dy * inv;

            float W = yetty->window_width > 0 ? yetty->window_width : 1.0f;
            float H = yetty->window_height > 0 ? yetty->window_height : 1.0f;
            float max_off_x = (W * 0.5f) * (1.0f - inv);
            float max_off_y = (H * 0.5f) * (1.0f - inv);
            if (max_off_x < 0) {
                max_off_x = 0;
            }
            if (max_off_y < 0) {
                max_off_y = 0;
            }
            yetty->visual_zoom_offset_x =
                yetty_clampf(yetty->visual_zoom_offset_x, -max_off_x, max_off_x);
            yetty->visual_zoom_offset_y =
                yetty_clampf(yetty->visual_zoom_offset_y, -max_off_y, max_off_y);

            {
                struct yetty_yui_event apply = {0};
                apply.type = YETTY_YCORE_ZOOM_VISUAL_APPLY;
                apply.zoom_visual_apply.scale = yetty->visual_zoom_scale;
                apply.zoom_visual_apply.offset_x = yetty->visual_zoom_offset_x;
                apply.zoom_visual_apply.offset_y = yetty->visual_zoom_offset_y;
                if (yetty->workspace) {
                    yetty_yui_workspace_on_event(yetty->workspace, &apply);
                }
            }
            if (yetty->event_loop && yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* ZOOM_CELL_SIZE — structural zoom. Forward to the workspace so the
     * active terminal can scale its layers' cell_size and recompute cols/rows.
     * See terminal.c for the actual restructuring. */
    if (event->type == YETTY_YCORE_ZOOM_CELL_SIZE) {
        if (yetty->workspace) {
            yetty_yui_workspace_on_event(yetty->workspace, event);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Handle RESIZE event - reconfigure surface and resize render target */
    if (event->type == YETTY_YCORE_RESIZE) {
        uint32_t width = (uint32_t)event->resize.width;
        uint32_t height = (uint32_t)event->resize.height;

        ydebug("yetty: RESIZE %ux%u", width, height);

        if (width == 0 || height == 0) {
            return YETTY_OK(yetty_ycore_int, 1);
        }

        yetty->window_width = (float)width;
        yetty->window_height = (float)height;

        /* Reconfigure surface */
        WGPUSurface surface = yetty->context.app_context.app_gpu_context.surface;
        if (surface && yetty->device) {
            WGPUSurfaceConfiguration config = {0};
            config.device = yetty->device;
            config.format = yetty->surface_format;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.width = width;
            config.height = height;
            config.presentMode = WGPUPresentMode_Fifo;
            wgpuSurfaceConfigure(surface, &config);

            yetty->context.app_context.app_gpu_context.surface_width = width;
            yetty->context.app_context.app_gpu_context.surface_height = height;
            yetty->context.gpu_context.app_gpu_context.surface_width = width;
            yetty->context.gpu_context.app_gpu_context.surface_height = height;
        }

        /* Resize render target */
        if (yetty->render_target && yetty->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, (float)width, (float)height};
            yetty->render_target->ops->resize(yetty->render_target, vp);
        }

        /* Resize workspace */
        if (yetty->workspace) {
            yetty_yui_workspace_resize(yetty->workspace, (float)width, (float)height);
        }

        /* Forward to workspace for tile/view resize handling */
        if (yetty->workspace) {
            yetty_yui_workspace_on_event(yetty->workspace, event);
        }

        /* Request re-render after resize */
        if (yetty->event_loop && yetty->event_loop->ops->request_render) {
            yetty->event_loop->ops->request_render(yetty->event_loop);
        }

        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Graceful shutdown.
     *
     * Same handler for window-close (window_close_callback posts SHUTDOWN)
     * and SIGINT/SIGTERM (libuv signal handler dispatches SHUTDOWN). The
     * sequence is:
     *   1. Forward to workspace → terminals set shutting_down=1, which
     *      makes terminal_render_frame skip further GPU work (avoids
     *      racing the device tear-down).
     *   2. Stop the event loop → yetty_run returns → main thread sees
     *      *running=0 and breaks out of glfwWaitEvents, after which the
     *      cleanup chain (yetty_destroy → workspace → terminals →
     *      fork_pty_stop) reaps the PTY children. */
    if (event->type == YETTY_YCORE_SHUTDOWN) {
        ydebug("yetty: SHUTDOWN — winding down");
        if (yetty->workspace) {
            yetty_yui_workspace_on_event(yetty->workspace, event);
        }
        if (yetty->event_loop && yetty->event_loop->ops->stop) {
            yetty->event_loop->ops->stop(yetty->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Forward other events to workspace */
    if (yetty->workspace) {
        return yetty_yui_workspace_on_event(yetty->workspace, event);
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

/* Bind yetty's main listener to the event loop for all event types this
 * dispatcher cares about. Thin wrapper over yetty_yevent_register_default_listeners
 * that knows where the listener and event loop live on the yetty struct. */
static struct yetty_ycore_void_result register_event_listeners(struct yetty_yetty_yetty *yetty)
{
    yetty->listener.handler = yetty_event_handler;
    return yetty_yevent_register_default_listeners(yetty->event_loop, &yetty->listener);
}

/*===========================================================================
 * WebGPU initialization
 *===========================================================================*/

void yetty_log_gpu_info(WGPUAdapter adapter)
{
    if (!adapter) {
        ywarn("yetty_log_gpu_info: adapter is NULL");
        return;
    }

    char *desc = yetty_ywebgpu_get_webgpu_description(adapter);
    if (!desc) {
        ywarn("yetty_log_gpu_info: failed to get WebGPU description");
        return;
    }
    yinfo("WebGPU adapter description:\n%s", desc);
    free(desc);
}

static struct yetty_ycore_void_result init_webgpu(struct yetty_yetty_yetty *yetty)
{
    ydebug("initWebGPU: Starting...");

    /* Instance and surface from platform's AppGpuContext */
    WGPUInstance instance = yetty->context.app_context.app_gpu_context.instance;
    WGPUSurface surface = yetty->context.app_context.app_gpu_context.surface;

    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "No WebGPU instance provided");
    }
    ydebug("initWebGPU: instance=%p surface=%p", (void *)instance, (void *)surface);

    /* Request adapter (surface can be NULL for headless mode) */
    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = surface; /* NULL is OK for headless */
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;

    int adapter_ready = 0;
    WGPURequestAdapterCallbackInfo adapter_cb = {0};
    adapter_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    adapter_cb.callback = yetty_ywebgpu_adapter_request_callback;
    adapter_cb.userdata1 = &yetty->adapter;
    adapter_cb.userdata2 = &adapter_ready;

    ydebug("initWebGPU: Requesting adapter...");
    wgpuInstanceRequestAdapter(instance, &adapter_opts, adapter_cb);

#ifdef __EMSCRIPTEN__
    /* On WebASM, adapter request is async - yield to JS event loop */
    while (!adapter_ready) {
        emscripten_sleep(0);
    }
#endif

    if (!yetty->adapter) {
        return YETTY_ERR(yetty_ycore_void, "Failed to get WebGPU adapter");
    }
    ydebug("initWebGPU: Adapter obtained");

    /* Log adapter info (backend, vendor, device, limits) at yinfo level */
    yetty_log_gpu_info(yetty->adapter);

    /* Request device.
     *
     * NOTE: the `config` argument to fill_default_limits is currently IGNORED;
     * the values are still hardcoded in yetty_ywebgpu_fill_default_limits().
     * See https://github.com/zokrezyl/yetty/issues/138 for the plan to source
     * these knobs from the yetty config file. */
    WGPULimits limits;
    yetty_ywebgpu_fill_default_limits(yetty->adapter,
                                      yetty->context.app_context.config, &limits);

    WGPUStringView device_label = {.data = "yetty device", .length = 12};
    WGPUStringView queue_label = {.data = "default queue", .length = 13};

    WGPUDeviceDescriptor device_desc = {0};
    device_desc.label = device_label;
    device_desc.requiredLimits = &limits;
    device_desc.defaultQueue.label = queue_label;
    device_desc.uncapturedErrorCallbackInfo = yetty_ywebgpu_get_error_callback_info();

    struct yetty_ywebgpu_device_request_state device_cb_data = {{0}, 0};
    WGPURequestDeviceCallbackInfo device_cb = {0};
    device_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    device_cb.callback = yetty_ywebgpu_device_request_callback;
    device_cb.userdata1 = &yetty->device;
    device_cb.userdata2 = &device_cb_data;

    ydebug("initWebGPU: Requesting device...");
    wgpuAdapterRequestDevice(yetty->adapter, &device_desc, device_cb);

#ifdef __EMSCRIPTEN__
    /* On WebASM, device request is async - yield to JS event loop */
    while (!device_cb_data.ready) {
        emscripten_sleep(0);
    }
#endif

    if (!yetty->device) {
        yerror("initWebGPU: device request failed: %s",
               device_cb_data.error_msg[0] ? device_cb_data.error_msg : "(no message)");
        return YETTY_ERR(yetty_ycore_void, "Failed to get WebGPU device");
    }
    ydebug("initWebGPU: Device obtained");

    yetty->queue = wgpuDeviceGetQueue(yetty->device);
    ydebug("initWebGPU: Queue obtained");

    /* Determine surface format */
    if (surface) {
        WGPUSurfaceCapabilities caps = {0};
        wgpuSurfaceGetCapabilities(surface, yetty->adapter, &caps);
        if (caps.formatCount > 0) {
            yetty->surface_format = caps.formats[0];
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    } else {
        yetty->surface_format = WGPUTextureFormat_BGRA8Unorm;
    }
    ydebug("initWebGPU: Surface format = %d", (int)yetty->surface_format);

    /* Configure surface (skip for headless) */
    if (surface) {
        WGPUSurfaceConfiguration surface_config = {0};
        surface_config.device = yetty->device;
        surface_config.format = yetty->surface_format;
        surface_config.usage = WGPUTextureUsage_RenderAttachment;
        surface_config.width = yetty->context.app_context.app_gpu_context.surface_width;
        surface_config.height = yetty->context.app_context.app_gpu_context.surface_height;
        surface_config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &surface_config);
        ydebug("initWebGPU: Surface configured %ux%u", surface_config.width, surface_config.height);
    } else {
        ydebug("initWebGPU: No surface (headless mode)");
    }

    /* Create GPU allocator */
    struct yetty_yrender_gpu_allocator_result alloc_res =
        yetty_yrender_gpu_allocator_create(yetty->device);
    if (!YETTY_IS_OK(alloc_res)) {
        return YETTY_ERR(yetty_ycore_void, "failed to create GPU allocator");
    }
    ydebug("initWebGPU: GPU allocator created");

    /* Complete context with owned GPU objects */
    yetty->context.gpu_context.app_gpu_context = yetty->context.app_context.app_gpu_context;
    yetty->context.gpu_context.adapter = yetty->adapter;
    yetty->context.gpu_context.device = yetty->device;
    yetty->context.gpu_context.queue = yetty->queue;
    yetty->context.gpu_context.surface_format = yetty->surface_format;
    yetty->context.gpu_context.allocator = alloc_res.value;

    /* Check for VNC mode */
    struct yetty_yconfig_config *config = yetty->context.app_context.config;
    const char *vnc_server_str = config->ops->get_string(config, "vnc/server", NULL);
    const char *vnc_headless_str = config->ops->get_string(config, "vnc/headless", NULL);
    int vnc_enabled = (vnc_server_str && strcmp(vnc_server_str, "true") == 0) ||
                      (vnc_headless_str && strcmp(vnc_headless_str, "true") == 0);

    /* Create VNC server if enabled */
    if (vnc_enabled) {
        struct yetty_vnc_server_ptr_result vnc_res = yetty_yvnc_server_create(
            instance, yetty->device, yetty->queue, yetty->event_loop, yetty->wgpu,
            yetty->context.app_context.platform_input_pipe);
        if (!YETTY_IS_OK(vnc_res)) {
            return YETTY_ERR(yetty_ycore_void, "failed to create VNC server");
        }
        yetty->vnc_server = vnc_res.value;
        ydebug("initWebGPU: VNC server created");

        /* Apply per-flag compression / delta-tracking settings. Each config
         * key comes from the matching --vnc-* CLI flag and tunes the VNC
         * server's encode+send path. Setters are no-ops on NULL / unset. */
        if (config->ops->get_bool(config, "vnc/raw", 0)) {
            yetty_yvnc_server_set_force_raw(yetty->vnc_server, 1);
        }
        int jpeg_q = config->ops->get_int(config, "vnc/compression-quality", 0);
        if (jpeg_q > 0) {
            yetty_yvnc_server_set_jpeg_quality(yetty->vnc_server, (uint8_t)jpeg_q);
        }
        if (config->ops->get_bool(config, "vnc/always-full", 0)) {
            yetty_yvnc_server_set_always_full_frame(yetty->vnc_server, 1);
        }
        if (config->ops->get_bool(config, "vnc/use-h264", 0)) {
            yetty_yvnc_server_set_use_h264(yetty->vnc_server, 1);
        }
        if (config->ops->get_bool(config, "vnc/merge-rects", 0)) {
            yetty_yvnc_server_set_merge_rectangles(yetty->vnc_server, 1);
        }

        /* H.264 tuning knobs — read from vnc/h264/... config keys. Each is
         * optional; the server treats zero / unset as "use encoder defaults". */
        int h264_bps = config->ops->get_int(config, "vnc/h264/bitrate", 0);
        if (h264_bps > 0) {
            yetty_yvnc_server_set_h264_bitrate(yetty->vnc_server, (uint32_t)h264_bps);
        }
        int h264_fps = config->ops->get_int(config, "vnc/h264/framerate", 0);
        if (h264_fps > 0) {
            yetty_yvnc_server_set_h264_framerate(yetty->vnc_server, (float)h264_fps);
        }
        int h264_idr = config->ops->get_int(config, "vnc/h264/idr-interval", 0);
        if (h264_idr > 0) {
            yetty_yvnc_server_set_h264_idr_interval(yetty->vnc_server, (uint32_t)h264_idr);
        }
        if (config->ops->has(config, "vnc/h264/screen-content")) {
            yetty_yvnc_server_set_h264_screen_content(
                yetty->vnc_server, config->ops->get_bool(config, "vnc/h264/screen-content", 1));
        }

        /* Start VNC server */
        int vnc_port = config->ops->get_int(config, "vnc/port", 5900);
        struct yetty_ycore_void_result start_res =
            yetty_yvnc_server_start(yetty->vnc_server, (uint16_t)vnc_port);
        if (!YETTY_IS_OK(start_res)) {
            yetty_yvnc_server_destroy(yetty->vnc_server);
            yetty->vnc_server = NULL;
            return YETTY_ERR(yetty_ycore_void, "failed to start VNC server");
        }
        yinfo("VNC server started on port %d", vnc_port);
    }

    /* Create render target */
    struct yetty_yrender_viewport vp = {
        .x = 0,
        .y = 0,
        .w = (float)yetty->context.app_context.app_gpu_context.surface_width,
        .h = (float)yetty->context.app_context.app_gpu_context.surface_height};

    struct yetty_yrender_target_ptr_result target_res;

    /*
     * When we're running on X11 — and the platform handed us native Display
     * and Window handles (GLFW's X11 backend; Wayland/Cocoa/Win32 leave
     * these NULL/0) — use the X11-tile target. It renders offscreen and
     * XShmPutImage's only the dirty tiles into the X window, which keeps
     * per-frame wire traffic tiny on remote displays (VNC+VGL) and stays
     * competitive locally where XShm is a shared-memory memcpy.
     *
     * No opt-in flag: X11 means tile target, Wayland/other means the
     * standard WebGPU surface path. VNC-server mode always wins above.
     */
    bool x11_tile_available = false;
#ifdef YETTY_HAS_X11_TILE
    x11_tile_available = yetty->context.app_context.app_gpu_context.x11_display != NULL &&
                         yetty->context.app_context.app_gpu_context.x11_window != 0UL;
#endif

    if (vnc_enabled) {
        /* VNC render target: sends frames to VNC, optionally presents to surface */
        target_res = yetty_yrender_target_vnc_create(
            yetty->device, yetty->queue, yetty->surface_format, alloc_res.value,
            surface, /* NULL for headless, non-NULL for mirror */
            yetty->vnc_server, vp);
#ifdef YETTY_HAS_X11_TILE
    } else if (x11_tile_available) {
        yinfo("render target: X11-tile (XShmPutImage per dirty tile)");
        target_res = yetty_yrender_target_x11_tile_create(
            yetty->device, yetty->queue, yetty->surface_format, alloc_res.value, yetty->wgpu,
            yetty->event_loop, yetty->context.app_context.app_gpu_context.x11_display,
            yetty->context.app_context.app_gpu_context.x11_window, vp);
        if (!YETTY_IS_OK(target_res)) {
            ywarn("X11-tile target failed (%s); falling back to texture target",
                  target_res.error.msg);
            target_res = yetty_yrender_target_texture_create(
                yetty->device, yetty->queue, yetty->surface_format, alloc_res.value, surface, vp);
        }
#endif
    } else {
        /* Standard texture render target */
        target_res = yetty_yrender_target_texture_create(
            yetty->device, yetty->queue, yetty->surface_format, alloc_res.value, surface, vp);
    }
    if (!YETTY_IS_OK(target_res)) {
        return YETTY_ERR(yetty_ycore_void, "failed to create render target");
    }
    yetty->render_target = target_res.value;
    ydebug("initWebGPU: render target created %.0fx%.0f vnc=%d", vp.w, vp.h, vnc_enabled);

    ydebug("initWebGPU: Complete");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API
 *===========================================================================*/

struct yetty_yetty_yetty_result yetty_create(const struct yetty_yetty_app_context *app_context)
{
    ydebug("yetty_create: Starting...");

    struct yetty_yetty_yetty *yetty = calloc(1, sizeof(struct yetty_yetty_yetty));
    if (!yetty) {
        return YETTY_ERR(yetty_yetty_yetty, "Failed to allocate yetty");
    }
    yetty->visual_zoom_scale = 1.0f;
    ydebug("yetty_create: Allocated yetty struct");

    /* Copy app context */
    yetty->context.app_context = *app_context;
    ydebug("yetty_create: Copied app context");

    /* Create event loop early - needed by VNC in init_webgpu */
    struct yetty_ycore_xthread_event_pipe *pipe = app_context->platform_input_pipe;
    struct yetty_ycore_event_loop_result event_loop_res = yetty_ycore_event_loop_create(pipe);
    if (!YETTY_IS_OK(event_loop_res)) {
        free(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "failed to create event loop");
    }
    yetty->event_loop = event_loop_res.value;
    yetty->context.event_loop = yetty->event_loop;
    ydebug("yetty_create: event loop created at %p", (void *)yetty->event_loop);

    /* Create the GPU await machinery before init_webgpu so the VNC server
     * (which init_webgpu may create) has it available. */
    struct yplatform_wgpu_ptr_result wgpu_res = yetty_yplatform_wgpu_create(
        yetty->context.app_context.app_gpu_context.instance, yetty->event_loop);
    if (!YETTY_IS_OK(wgpu_res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "yplatform_wgpu_create failed");
    }
    yetty->wgpu = wgpu_res.value;
    ydebug("yetty_create: ywebgpu await machinery created");

    /* Initialize WebGPU */
    struct yetty_ycore_void_result res = init_webgpu(yetty);
    if (!YETTY_IS_OK(res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "WebGPU init failed");
    }
    ydebug("yetty_create: WebGPU initialized");

    /* Register event listeners */
    res = register_event_listeners(yetty);
    if (!YETTY_IS_OK(res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "failed to register event listeners");
    }

    /* Create workspace */
    ydebug("yetty_create: Creating workspace...");
    struct yetty_yui_workspace_ptr_result ws_res = yetty_yui_workspace_create();
    if (!YETTY_IS_OK(ws_res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "Failed to create workspace");
    }
    yetty->workspace = ws_res.value;
    ydebug("yetty_create: Workspace created");

    /* Load layout from config */
    ydebug("yetty_create: Loading layout from config...");
    struct yetty_ycore_void_result layout_res =
        yetty_yui_workspace_load_layout(yetty->workspace, app_context->config, &yetty->context);
    if (!YETTY_IS_OK(layout_res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, layout_res.error.msg);
    }
    ydebug("yetty_create: Layout loaded");

    /* Start RPC server if configured */
    const char *rpc_port_str =
        app_context->config->ops->get_string(app_context->config, YETTY_YCONFIG_KEY_RPC_PORT, NULL);
    if (rpc_port_str) {
        const char *rpc_host = app_context->config->ops->get_string(
            app_context->config, YETTY_YCONFIG_KEY_RPC_HOST, "127.0.0.1");
        int rpc_port = atoi(rpc_port_str);
        ydebug("yetty_create: Starting RPC server on %s:%d", rpc_host, rpc_port);
        struct yetty_rpc_server_ptr_result rpc_res = yetty_yrpc_server_create(yetty->event_loop);
        if (YETTY_IS_OK(rpc_res)) {
            yetty->rpc_server = rpc_res.value;
            struct yetty_ycore_void_result start_res =
                yetty_yrpc_server_start(yetty->rpc_server, rpc_host, rpc_port);
            if (YETTY_IS_OK(start_res)) {
                yinfo("yetty: RPC server listening on %s:%d", rpc_host, rpc_port);
            } else {
                yerror("yetty: failed to start RPC server: %s", start_res.error.msg);
                yetty_yrpc_server_destroy(yetty->rpc_server);
                yetty->rpc_server = NULL;
            }
        } else {
            yerror("yetty: failed to create RPC server: %s", rpc_res.error.msg);
        }
    }

    ydebug("yetty_create: Complete");
    return YETTY_OK(yetty_yetty_yetty, yetty);
}

struct yetty_ycore_void_result yetty_destroy(struct yetty_yetty_yetty *yetty)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (!yetty) {
        return YETTY_ERR(yetty_ycore_void, "yetty_destroy: NULL yetty");
    }

    ydebug("yetty_destroy: starting");

    /* Destroy RPC server */
    if (yetty->rpc_server) {
        ydebug("yetty_destroy: destroying RPC server");
        struct yetty_ycore_void_result rr = yetty_yrpc_server_destroy(yetty->rpc_server);
        if (YETTY_IS_ERR(rr)) {
            first_err = rr;
        }
        yetty->rpc_server = NULL;
    }

    /* Destroy workspace (also destroys tiles and views including terminals) */
    if (yetty->workspace) {
        ydebug("yetty_destroy: destroying workspace");
        yetty_yui_workspace_destroy(yetty->workspace);
        yetty->workspace = NULL;
        ydebug("yetty_destroy: workspace destroyed");
    }

    /* Destroy render target BEFORE wgpu and the event loop.
     *
     * Releasing GPU buffers (e.g. tile-diff readback buffers) synchronously
     * fires any pending wgpuBufferMapAsync callbacks with status=Aborted.
     * Those callbacks (map_callback in ywebgpu.c) dereference wgpu->loop to
     * post a coro-resume — so wgpu and the event loop must still be alive
     * when render_target's destroy runs. */
    if (yetty->render_target && yetty->render_target->ops && yetty->render_target->ops->destroy) {
        ydebug("yetty_destroy: destroying render_target");
        yetty->render_target->ops->destroy(yetty->render_target);
        yetty->render_target = NULL;
    }

    /* Tear down the GPU await machinery before the event loop. The tick
     * timer is owned by the loop, so this must happen first. */
    if (yetty->wgpu) {
        yetty_yplatform_wgpu_destroy(yetty->wgpu);
        yetty->wgpu = NULL;
    }

    /* Destroy event loop */
    if (yetty->event_loop && yetty->event_loop->ops && yetty->event_loop->ops->destroy) {
        ydebug("yetty_destroy: destroying event_loop");
        yetty->event_loop->ops->destroy(yetty->event_loop);
        yetty->event_loop = NULL;
        yetty->context.event_loop = NULL;
        ydebug("yetty_destroy: event_loop destroyed");
    }

    /* Destroy VNC server after render target (render target references it) */
    if (yetty->vnc_server) {
        ydebug("yetty_destroy: stopping VNC server");
        struct yetty_ycore_void_result vsr = yetty_yvnc_server_stop(yetty->vnc_server);
        if (YETTY_IS_ERR(vsr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = vsr;
            } else {
                yetty_ycore_error_destroy(vsr.error);
            }
        }
        ydebug("yetty_destroy: destroying VNC server");
        struct yetty_ycore_void_result vdr = yetty_yvnc_server_destroy(yetty->vnc_server);
        if (YETTY_IS_ERR(vdr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = vdr;
            } else {
                yetty_ycore_error_destroy(vdr.error);
            }
        }
        yetty->vnc_server = NULL;
    }

    /* Destroy GPU allocator before device */
    if (yetty->context.gpu_context.allocator) {
        ydebug("yetty_destroy: destroying GPU allocator");
        yetty->context.gpu_context.allocator->ops->destroy(yetty->context.gpu_context.allocator);
        yetty->context.gpu_context.allocator = NULL;
    }

    /* Surface is created by platform (glfw-main.c), but we configured it.
     * Must release BEFORE device since release needs device for swapchain detach. */
    WGPUSurface surface = yetty->context.app_context.app_gpu_context.surface;
    if (surface && yetty->device) {
        ydebug("yetty_destroy: unconfiguring surface");
        wgpuSurfaceUnconfigure(surface);
#ifndef __EMSCRIPTEN__
        wgpuDeviceTick(yetty->device);
#endif
        ydebug("yetty_destroy: releasing surface");
        wgpuSurfaceRelease(surface);
        yetty->context.app_context.app_gpu_context.surface = NULL;
    }

    if (yetty->queue) {
        ydebug("yetty_destroy: releasing queue");
        wgpuQueueRelease(yetty->queue);
    }
    if (yetty->device) {
        ydebug("yetty_destroy: releasing device");
        wgpuDeviceRelease(yetty->device);
    }
    if (yetty->adapter) {
        ydebug("yetty_destroy: releasing adapter");
        wgpuAdapterRelease(yetty->adapter);
    }

    ydebug("yetty_destroy: freeing yetty struct");
    free(yetty);
    ydebug("yetty_destroy: done");

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_destroy: subsystem destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_run(struct yetty_yetty_yetty *yetty)
{
    ydebug("yetty_run: Starting...");

    if (!yetty) {
        ydebug("yetty_run: yetty is null!");
        return YETTY_ERR(yetty_ycore_void, "yetty is null");
    }

    if (!yetty->event_loop) {
        ydebug("yetty_run: no event_loop!");
        return YETTY_ERR(yetty_ycore_void, "no event_loop");
    }

    if (!yetty->event_loop->ops || !yetty->event_loop->ops->start) {
        ydebug("yetty_run: event_loop has no start op!");
        return YETTY_ERR(yetty_ycore_void, "event_loop has no start op");
    }

    ydebug("yetty_run: Starting event loop...");
    struct yetty_ycore_void_result res = yetty->event_loop->ops->start(yetty->event_loop);
    ydebug("yetty_run: event_loop start returned, ok=%d", YETTY_IS_OK(res));

    return res;
}
