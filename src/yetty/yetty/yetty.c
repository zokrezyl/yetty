/*
 * yetty.c - Main yetty implementation
 */

#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/ywebgpu.h>
#include <yetty/yevent/event.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender-utils/screenshot.h>
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
#include <yetty/ynotify/ynotify.h>
#include <yetty/yui/workspace.h>
#include <yetty/yui/tabbar.h>
#include <yetty/yui/tile.h>
#include <yetty/yui/yui.h>
#include <yetty/yui-core/view.h>
#include <yetty/yrpc/rpc-server.h>
#include <yetty/ymsdf/generator.h>
#include <yetty/yvnc/vnc-server.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/fs.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

/* Yetty instance */
struct yetty_yetty_yetty {
    struct yetty_context context;
    /* Top-level UI. The tabbar owns N workspaces; only the active
     * workspace renders. Single-workspace boots create one tab so the
     * existing render/event paths see exactly the same shape as before
     * the tabbar was introduced. */
    struct yetty_yui_tabbar *tabbar;
    /* App-level ygui (popups, dialogs, future statusbar). Sits as the
     * highest-z layer above every terminal in the active workspace.
     * The tabbar above stays on its legacy rect path for now — yui is
     * additive; it does not displace anything. See src/yetty/yui/yui.h. */
    struct yetty_yui *yui;
    struct yetty_yevent_event_loop *event_loop;
    struct yetty_yevent_event_listener listener;

    /* WebGPU state (owned by Yetty) */
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;

    /* Coroutine-aware wgpu await machinery (loop-thread tick + completion
     * routing). Owned by Yetty; lifetime spans event_loop + wgpu instance. */
    struct yetty_yplatform_wgpu *wgpu;

    /* Big render target - window-sized texture with surface for presentation */
    struct yetty_ydraw_target *render_target;

    /* RPC server (optional, enabled via -r/--rpc-socket) */
    struct yetty_yrpc_server *rpc_server;
    yetty_yevent_timer_id rpc_timer_id;
    struct yetty_yevent_event_listener rpc_timer_listener;

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
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
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

        /* Global clear() removed — each pane's background-layer (tile.c
         * pane_render → background_layer.render) wipes the pane to its bg
         * colour with an opaque RGBA fill before any other layer paints,
         * and the tabbar paints its own strip, so the only pixels the old
         * global Clear ever delivered to the screen were inter-pane gap
         * pixels that nothing else overwrites. Try without and see whether
         * any visible gap shows previous-frame ghosting; if so, re-add a
         * scissored clear for just the gap rects, not the whole 4K. */

        /* Render the tab strip + active workspace. Inactive workspaces hold
         * GPU state but don't contribute to the frame. */
        ytime_start(workspace_render);
        if (yetty->tabbar) {
            struct yetty_ycore_void_result res =
                yetty_yui_tabbar_render(yetty->tabbar, yetty->render_target);
            if (!YETTY_IS_OK(res)) {
                yerror("yetty: tabbar render failed: %s", res.error.msg);
            }
        }
        ytime_report(workspace_render);

        /* App-level yui on top of every terminal. ygui-produced
         * primitives travel via memory-pty → scene ydraw-layer here. */
        if (yetty->yui) {
            struct yetty_ycore_void_result yr =
                yetty_yui_render(yetty->yui, yetty->render_target);
            if (!YETTY_IS_OK(yr)) {
                yerror("yetty: yui render failed: %s", yr.error.msg);
                yetty_ycore_error_destroy(yr.error);
            }
        }

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
    if (event->type == YETTY_YCORE_MOUSE_SCROLL) {
        int mods = event->mouse_scroll.mods;
        bool ctrl = (mods & YETTY_MOD_CONTROL) != 0;
        bool shift = (mods & YETTY_MOD_SHIFT) != 0;

        if (ctrl && shift) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_CELL_SIZE;
            ev.zoom_cell_size.delta = event->mouse_scroll.dy * 0.04f;
            yetty_yevent_post_async(yetty->context.app_context.platform_input_pipe, &ev);
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ctrl) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_VISUAL;
            ev.zoom_visual.delta = event->mouse_scroll.dy * 0.1f;
            ev.zoom_visual.anchor_x = event->mouse_scroll.x;
            ev.zoom_visual.anchor_y = event->mouse_scroll.y;
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
            if (yetty->tabbar) {
                yetty_yui_tabbar_on_event(yetty->tabbar, &apply);
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
                if (yetty->tabbar) {
                    yetty_yui_tabbar_on_event(yetty->tabbar, &apply);
                }
            }
            if (yetty->event_loop && yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* SCREENSHOT — capture the current render target's texture to disk.
     *
     * Triggered today by an explicit YETTY_YCORE_SCREENSHOT event (RPC, test
     * harness, or future keybinding). We grab the last-rendered texture
     * straight off the target — no extra render pass, no extra GPU allocation
     * beyond a one-shot mappable readback buffer inside the screenshot
     * coroutine. If the path field is empty, fall back to a default under
     * /tmp so the user always gets *something* on disk. */
    if (event->type == YETTY_YCORE_SCREENSHOT) {
        if (!yetty->render_target || !yetty->render_target->ops->get_texture) {
            yerror("yetty: SCREENSHOT but no render_target/get_texture");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        WGPUTexture tex = yetty->render_target->ops->get_texture(yetty->render_target);
        if (!tex) {
            yerror("yetty: SCREENSHOT: render target has no texture");
            return YETTY_OK(yetty_ycore_int, 1);
        }

        const char *path = event->screenshot.path;
        char default_path[512];
        if (!path || !*path) {
            /* XDG-compliant default: $XDG_DATA_HOME/yetty/screenshots/ on
             * Linux (~/.local/share/yetty/screenshots/), platform-equivalent
             * elsewhere. The data dir itself is already mkdir_p'd at startup
             * (ymain/glfw.c); the screenshots subdir we create here on
             * demand. The env var was exported alongside it. */
            const char *data_dir = getenv("YETTY_DATA_DIR");
            if (!data_dir || !*data_dir) {
                yerror("yetty: SCREENSHOT: YETTY_DATA_DIR unset");
                return YETTY_OK(yetty_ycore_int, 1);
            }
            char dir[384];
            snprintf(dir, sizeof(dir), "%s/screenshots", data_dir);
            yetty_yplatform_mkdir_p(dir);

            time_t now = time(NULL);
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &now);
#else
            localtime_r(&now, &tm_buf);
#endif
            char ts[32];
            strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm_buf);
            snprintf(default_path, sizeof(default_path), "%s/yetty-%s.ppm", dir, ts);
            path = default_path;
        }

        struct yetty_ycore_void_result sr = yetty_yrender_utils_screenshot_capture(
            yetty->device, yetty->queue, yetty->wgpu, tex, path);
        if (!YETTY_IS_OK(sr)) {
            yerror("yetty: SCREENSHOT failed: %s", sr.error.msg);
            yetty_ycore_error_destroy(sr.error);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* ZOOM_CELL_SIZE — structural zoom. Forward to the workspace so the
     * active terminal can scale its layers' cell_size and recompute cols/rows.
     * See terminal.c for the actual restructuring. */
    if (event->type == YETTY_YCORE_ZOOM_CELL_SIZE) {
        if (yetty->tabbar) {
            yetty_yui_tabbar_on_event(yetty->tabbar, event);
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

        /* Resize the tabbar: it slices off the strip height at the top
         * and forwards the remainder to each workspace. We pre-subtract
         * yui's statusbar height from the bottom here so the workspace
         * area sits between [tabbar_strip .. H - statusbar_h]; without
         * this the bottom row of terminal cells is drawn under the
         * yui statusbar. */
        float bottom_inset =
            yetty->yui ? yetty_yui_statusbar_height(yetty->yui) : 0.0f;
        float ws_height = (float)height - bottom_inset;
        if (ws_height < 0.0f) {
            ws_height = 0.0f;
        }
        if (yetty->tabbar) {
            yetty_yui_tabbar_resize(yetty->tabbar, (float)width, ws_height);
        }

        /* Resize the app-level yui's scene canvas to match the full
         * framebuffer — yui's own bars (statusbar today, titlebar /
         * menubar later) live in the full canvas; only the terminal
         * workspaces below get the carved-out client area. */
        if (yetty->yui) {
            struct yetty_ycore_void_result yr = yetty_yui_resize(yetty->yui, width, height);
            if (YETTY_IS_ERR(yr)) {
                ywarn("yetty: yui resize failed: %s", yr.error.msg);
                yetty_ycore_error_destroy(yr.error);
            }
        }

        /* No second RESIZE forward to tabbar_on_event: tabbar_resize
         * already dispatched a synthetic RESIZE to every workspace
         * with the carved-out workspace height (ws_height - strip).
         * The old forward sent the *tabbar's input height* (which
         * still included the strip) and the terminal_view rebuilt its
         * bounds at that larger size, overflowing by `strip` pixels
         * over the statusbar at the bottom. */

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
        if (yetty->tabbar) {
            yetty_yui_tabbar_on_event(yetty->tabbar, event);
        }
        if (yetty->event_loop && yetty->event_loop->ops->stop) {
            yetty->event_loop->ops->stop(yetty->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Global key shortcuts. Translated into named events here (not in the
     * platform layer) so RPC / kb-mapping / tests can inject the same event
     * directly. Re-posted async so the readback work runs from a fresh loop
     * iteration and doesn't reenter this handler. Ctrl+F1 is reserved for a
     * help-context overlay; Ctrl+F2 captures a screenshot. */
    if (event->type == YETTY_YCORE_KEY_DOWN && (event->key.mods & YETTY_MOD_CONTROL) &&
        event->key.key == 291 /* GLFW_KEY_F2 */) {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_SCREENSHOT;
        yetty_yevent_post_async(yetty->context.app_context.platform_input_pipe, &ev);
        ydebug("yetty: Ctrl+F2 -> SCREENSHOT");
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Right-click in the workspace area → open the pane context menu.
     * Run BEFORE yui_on_event because yui only consumes events when
     * already-active (an open menu / dialog) and we want to OPEN it on
     * the click, not after. Also focus the clicked pane so the
     * subsequent split applies to it. The tabbar strip and statusbar
     * are excluded; right-click there falls through to existing
     * handlers. */
    if (yetty->yui && event->type == YETTY_YCORE_MOUSE_DOWN &&
        event->mouse.button == 1 /* GLFW right */) {
        float y = event->mouse.y;
        float tabbar_h = (float)YETTY_YUI_TABBAR_HEIGHT;
        float status_h = yetty_yui_statusbar_height(yetty->yui);
        if (y >= tabbar_h && y < yetty->window_height - status_h) {
            /* Focus the clicked pane — same logic the workspace's own
             * MOUSE_DOWN handler runs, but without forwarding the
             * click to the terminal (we don't want the inner program
             * to see a mouse event for a UI gesture). */
            struct yetty_yui_workspace *ws =
                yetty_yui_tabbar_active_workspace(yetty->tabbar);
            if (ws) {
                struct yetty_yui_tile *root = yetty_yui_workspace_root(ws);
                if (root) {
                    struct yetty_yui_tile *clicked = yetty_yui_tile_find_pane_at(
                        root, event->mouse.x, event->mouse.y);
                    if (clicked) {
                        yetty_yui_tile_clear_focus(root);
                        yetty_yui_tile_pane_set_focused(clicked, 1);
                    }
                }
            }
            yetty_yui_show_context_menu(yetty->yui, event->mouse.x, event->mouse.y);
            if (yetty->event_loop && yetty->event_loop->ops &&
                yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* yui priority: when a v-menu / dialog is up, mouse events go
     * through yui's ygui engine FIRST so the popup is hit-tested before
     * the workspace below sees them. If yui's not capturing (not active,
     * or event isn't a mouse type), fall through to the tabbar which
     * routes strip clicks and forwards the rest to the workspace. */
    if (yetty->yui) {
        struct yetty_ycore_int_result yr = yetty_yui_on_event(yetty->yui, event);
        if (YETTY_IS_OK(yr) && yr.value) {
            if (yetty->event_loop && yetty->event_loop->ops &&
                yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* Forward other events to the tabbar (which routes to active workspace). */
    if (yetty->tabbar) {
        return yetty_yui_tabbar_on_event(yetty->tabbar, event);
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
    yetty_ywebgpu_fill_default_limits(yetty->adapter, yetty->context.app_context.config, &limits);

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

    /* MSDF CDB generator (cpu | gpu). Selected by `msdf/generator` config
     * key. Created here so every consumer (currently ydraw-canvas font
     * materialisation) can grab it off gpu_context. */
    {
        const char *shaders_dir = yetty->context.app_context.config->ops->get_string(
            yetty->context.app_context.config, "paths/shaders", "");
        struct yetty_ymsdf_generator_ptr_result gres = yetty_ymsdf_generator_create_from_config(
            yetty->context.app_context.config, yetty->device,
            yetty->context.app_context.app_gpu_context.instance, shaders_dir);
        if (YETTY_IS_ERR(gres)) {
            return YETTY_ERR(yetty_ycore_void, "failed to create MSDF generator", gres);
        }
        yetty->context.gpu_context.msdf_generator = gres.value;
        yinfo("ymsdf: generator = %s", gres.value->ops->name(gres.value));
    }

    /* Check for VNC mode. --record sets vnc/record-file: it spins up a vnc
     * server (for the H.264 encode pipeline) without opening a TCP listener.
     * Window mode is unaffected — recording is a passive sink alongside
     * normal rendering. */
    struct yetty_yconfig_config *config = yetty->context.app_context.config;
    const char *vnc_server_str = config->ops->get_string(config, "vnc/server", NULL);
    const char *vnc_headless_str = config->ops->get_string(config, "vnc/headless", NULL);
    const char *vnc_record_str = config->ops->get_string(config, "vnc/record-file", NULL);
    int vnc_listen_enabled = (vnc_server_str && strcmp(vnc_server_str, "true") == 0) ||
                             (vnc_headless_str && strcmp(vnc_headless_str, "true") == 0);
    int vnc_record_enabled = vnc_record_str && vnc_record_str[0];
    int vnc_enabled = vnc_listen_enabled || vnc_record_enabled;

    /* Create VNC server if enabled */
    if (vnc_enabled) {
        struct yetty_vnc_server_ptr_result vnc_res = yetty_yvnc_server_create(
            instance, yetty->device, yetty->queue, yetty->event_loop, yetty->wgpu,
            yetty->context.app_context.platform_input_pipe, config);
        if (!YETTY_IS_OK(vnc_res)) {
            return YETTY_ERR(yetty_ycore_void, "failed to create VNC server");
        }
        yetty->vnc_server = vnc_res.value;
        ydebug("initWebGPU: VNC server created");

        /* Start TCP listener only when explicitly asked for. --record alone
         * keeps recording purely local. */
        if (vnc_listen_enabled) {
            int vnc_port = config->ops->get_int(config, "vnc/port", 5900);
            struct yetty_ycore_void_result start_res =
                yetty_yvnc_server_start(yetty->vnc_server, (uint16_t)vnc_port);
            if (!YETTY_IS_OK(start_res)) {
                yetty_yvnc_server_destroy(yetty->vnc_server);
                yetty->vnc_server = NULL;
                return YETTY_ERR(yetty_ycore_void, "failed to start VNC server", start_res);
            }
            yinfo("VNC server started on port %d", vnc_port);
        } else {
            /* Record-only: activate the encode pipeline without a TCP
             * listener. send_frame_* will accept frames because the
             * recording mux is registered as a consumer. */
            struct yetty_ycore_void_result act_res =
                yetty_yvnc_server_start_record_only(yetty->vnc_server);
            if (!YETTY_IS_OK(act_res)) {
                yetty_yvnc_server_destroy(yetty->vnc_server);
                yetty->vnc_server = NULL;
                return YETTY_ERR(yetty_ycore_void, "failed to activate VNC record mode", act_res);
            }
            yinfo("VNC record mode: %s", vnc_record_str);
        }
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
 * yui ↔ tabbar bridges
 *
 * The v-button on the tabbar fires `yetty_on_v_menu_click` which opens
 * yui's ygui popup_menu. When the user selects "Connect" in a dialog,
 * `yetty_on_yui_connect` translates the yui view-kind into the matching
 * tabbar kind and spawns a new workspace.
 *===========================================================================*/

static void yetty_on_v_menu_click(void *userdata, float anchor_x, float anchor_y)
{
    struct yetty_yetty_yetty *yetty = userdata;
    if (!yetty || !yetty->yui) {
        return;
    }
    yetty_yui_show_view_menu(yetty->yui, anchor_x, anchor_y);
    if (yetty->event_loop && yetty->event_loop->ops->request_render) {
        yetty->event_loop->ops->request_render(yetty->event_loop);
    }
}

/* Apply the kind-specific config keys (shell/command, ssh, telnet,
 * vnc/client) the same way yetty_on_yui_connect does so the next
 * terminal_create / viewer_create reads them. Returns 0 on success,
 * non-zero when validation fails (e.g. SSH/Telnet with empty host) —
 * caller surfaces a toast and aborts. */
static int yetty_apply_view_kind_to_config(struct yetty_yetty_yetty *yetty,
                                           enum yetty_yui_view_kind kind)
{
    struct yetty_yconfig_config *config = yetty->context.app_context.config;
    if (!config || !config->ops || !config->ops->set_string) {
        return -1;
    }
    /* Reset the exclusive flags up front so every kind has a clean slate. */
    config->ops->set_string(config, YETTY_YCONFIG_KEY_SSH, "false");
    config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET, "false");
    config->ops->set_string(config, "vnc/client", "");
    switch (kind) {
    case YETTY_YUI_VIEW_SHELL:
        config->ops->set_string(config, "shell/command", "");
        return 0;
    case YETTY_YUI_VIEW_EXEC: {
        const char *cmd = yetty_yui_get_exec_command(yetty->yui);
        if (!cmd || !cmd[0]) {
            ywarn("yetty: EXEC: empty command");
            return -1;
        }
        config->ops->set_string(config, "shell/command", cmd);
        return 0;
    }
    case YETTY_YUI_VIEW_SSH: {
        const char *host = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 0);
        const char *port = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 1);
        const char *key  = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 2);
        if (!host || !host[0]) {
            ywarn("yetty: SSH: empty host");
            return -1;
        }
        char cmd[1024];
        int n = snprintf(cmd, sizeof(cmd), "ssh");
        if (port && port[0]) n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -p %s", port);
        if (key && key[0])   n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -i %s", key);
        snprintf(cmd + n, sizeof(cmd) - (size_t)n, " %s", host);
        config->ops->set_string(config, "shell/command", cmd);
        return 0;
    }
    case YETTY_YUI_VIEW_TELNET: {
        const char *host = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 0);
        const char *port = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 1);
        if (!host || !host[0]) {
            ywarn("yetty: Telnet: empty host");
            return -1;
        }
        int port_i = (port && port[0]) ? atoi(port) : 23;
        if (port_i <= 0 || port_i > 65535) {
            return -1;
        }
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port_i);
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET, "true");
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET_HOST, host);
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET_PORT, port_str);
        return 0;
    }
    case YETTY_YUI_VIEW_YVNC:
        /* The yvnc dialog stashes host:port in vnc/client (TODO when
         * the dialog gets wired); for now the new pane will fall back
         * to a terminal if vnc/client is empty. */
        return 0;
    }
    return -1;
}

/* Right-click context menu: split the focused pane and seed the new
 * sibling with a view of the chosen kind. The split is on the active
 * workspace; orientation argument: 0 = vertical (sibling below),
 * 1 = horizontal (sibling to the right) — matches the labels in the
 * yui submenu. */
static void yetty_on_yui_split(void *userdata, enum yetty_yui_view_kind kind, int horizontal)
{
    struct yetty_yetty_yetty *yetty = userdata;
    if (!yetty || !yetty->tabbar) {
        return;
    }
    if (yetty_apply_view_kind_to_config(yetty, kind) != 0) {
        ynotify(YETTY_YNOTIFY_WARN, "Split aborted: missing fields");
        return;
    }

    struct yetty_yui_workspace *ws = yetty_yui_tabbar_active_workspace(yetty->tabbar);
    if (!ws) {
        ywarn("yetty: split: no active workspace");
        return;
    }
    struct yetty_yui_tile *root = yetty_yui_workspace_root(ws);
    if (!root) {
        ywarn("yetty: split: no root tile");
        return;
    }
    struct yetty_yui_tile *focused = yetty_yui_tile_find_focused_pane(root);
    if (!focused) {
        focused = yetty_yui_tile_find_first_pane(root);
    }
    if (!focused) {
        ywarn("yetty: split: no pane to split");
        return;
    }
    yetty_ycore_object_id pane_id = yetty_yui_tile_id(focused);

    /* "Split vertically" in the menu means a vertical divider line
     * between two panes side-by-side — i.e. a HORIZONTAL flexbox
     * direction. Map the user-facing label to the underlying
     * orientation enum: horizontal=1 → side-by-side (cross axis is
     * horizontal). */
    enum yetty_yui_orientation orient =
        horizontal ? YETTY_YUI_HORIZONTAL : YETTY_YUI_VERTICAL;
    struct yetty_ycore_void_result sr =
        yetty_yui_workspace_split_pane(ws, pane_id, orient);
    if (YETTY_IS_ERR(sr)) {
        yerror("yetty: split failed: %s", sr.error.msg);
        yetty_ycore_error_destroy(sr.error);
        return;
    }

    /* Re-resolve root (set_root may have replaced it) and find the new
     * sibling — workspace_split_pane sets the old pane as `first` and
     * creates the empty `second`. */
    root = yetty_yui_workspace_root(ws);
    struct yetty_yui_tile *parent_split = yetty_yui_tile_find_parent_split(root, pane_id);
    if (!parent_split) {
        ywarn("yetty: split: parent split not found post-split");
        return;
    }
    struct yetty_yui_tile *new_pane = yetty_yui_tile_split_second(parent_split);
    if (!new_pane) {
        ywarn("yetty: split: new pane not found");
        return;
    }

    /* Re-propagate bounds so the new sibling gets a non-empty rect
     * (workspace_split_pane only wires the tree pointers). The synth
     * RESIZE goes through the existing tabbar→workspace fan-out. */
    yetty_yui_tabbar_resize(yetty->tabbar, yetty->window_width,
                            yetty->window_height -
                                (yetty->yui ? yetty_yui_statusbar_height(yetty->yui) : 0.0f));

    /* All kinds spawn a terminal — config flags set above tell its PTY
     * what to run (ssh / telnet / shell-with-command). yVNC in a split
     * isn't wired yet (would need yvnc_viewer_create + linking yvnc
     * into yetty.c's TU); falling back to a terminal keeps the split
     * working for the other kinds. */
    struct yetty_yui_view *new_view = NULL;
    struct yetty_ycore_grid_size gs = {.rows = 24, .cols = 80};
    struct yetty_yterm_terminal_result tr =
        yetty_yterm_terminal_create(gs, &yetty->context);
    if (YETTY_IS_ERR(tr)) {
        yerror("yetty: split: terminal create: %s", tr.error.msg);
        yetty_ycore_error_destroy(tr.error);
        return;
    }
    new_view = yetty_yterm_terminal_as_view(tr.value);
    if (!new_view) {
        ywarn("yetty: split: no view created");
        return;
    }
    if (kind == YETTY_YUI_VIEW_YVNC) {
        ynotify(YETTY_YNOTIFY_INFO,
                "yVNC-in-split not yet wired — opened a terminal instead");
    }

    struct yetty_ycore_void_result pr = yetty_yui_tile_pane_push_view(new_pane, new_view);
    if (YETTY_IS_ERR(pr)) {
        yerror("yetty: split: push_view: %s", pr.error.msg);
        yetty_ycore_error_destroy(pr.error);
        return;
    }

    /* Focus the new pane so the user can immediately type into it. */
    yetty_yui_tile_clear_focus(root);
    yetty_yui_tile_pane_set_focused(new_pane, 1);

    if (yetty->event_loop && yetty->event_loop->ops &&
        yetty->event_loop->ops->request_render) {
        yetty->event_loop->ops->request_render(yetty->event_loop);
    }
}

static void yetty_on_yui_connect(void *userdata, enum yetty_yui_view_kind kind)
{
    struct yetty_yetty_yetty *yetty = userdata;
    if (!yetty || !yetty->tabbar) {
        return;
    }
    enum yetty_yui_tabbar_kind tk;
    struct yetty_yconfig_config *config = yetty->context.app_context.config;
    switch (kind) {
    case YETTY_YUI_VIEW_SHELL:
        /* Default shell — clear shell/command so get_shell_argv falls
         * through to the resolved $SHELL / shell/default path even if a
         * prior EXEC tab (or the -e flag) had stashed a command there. */
        if (config && config->ops && config->ops->set_string) {
            config->ops->set_string(config, "shell/command", "");
        }
        tk = YETTY_YUI_TAB_SHELL;
        break;
    case YETTY_YUI_VIEW_EXEC: {
        /* Take the command from the EXEC dialog's textinput and stash it
         * in shell/command. The PTY's get_shell_argv tokenizes it instead
         * of running $SHELL. */
        const char *cmd = yetty_yui_get_exec_command(yetty->yui);
        if (!cmd || !cmd[0]) {
            ywarn("yetty: EXEC connect with empty command — ignoring");
            return;
        }
        if (!config || !config->ops || !config->ops->set_string) {
            yerror("yetty: EXEC connect: no writable config");
            return;
        }
        config->ops->set_string(config, "shell/command", cmd);
        ydebug("yetty: EXEC connect with shell/command='%s'", cmd);
        tk = YETTY_YUI_TAB_SHELL;
        break;
    }
    case YETTY_YUI_VIEW_SSH: {
        const char *host = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 0);
        const char *port = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 1);
        const char *key  = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 2);
        if (!host || !host[0]) {
            ywarn("yetty: SSH connect with empty host — ignoring");
            return;
        }
        if (!config || !config->ops || !config->ops->set_string) {
            yerror("yetty: SSH connect: no writable config");
            return;
        }
        char cmd[1024];
        int n = snprintf(cmd, sizeof(cmd), "ssh");
        if (port && port[0])
            n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -p %s", port);
        if (key && key[0])
            n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -i %s", key);
        snprintf(cmd + n, sizeof(cmd) - (size_t)n, " %s", host);
        config->ops->set_string(config, "shell/command", cmd);
        ydebug("yetty: SSH connect via ssh(1): %s", cmd);
        tk = YETTY_YUI_TAB_SHELL;
        break;
    }
    case YETTY_YUI_VIEW_TELNET: {
        const char *host = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 0);
        const char *port = yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 1);
        if (!host || !host[0]) {
            ywarn("yetty: Telnet connect with empty host — ignoring");
            return;
        }
        if (!config || !config->ops || !config->ops->set_string) {
            yerror("yetty: Telnet connect: no writable config");
            return;
        }
        int port_i = (port && port[0]) ? atoi(port) : 23;
        if (port_i <= 0 || port_i > 65535) {
            ywarn("yetty: Telnet connect: invalid port '%s'", port ? port : "");
            return;
        }
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port_i);
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET, "true");
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET_HOST, host);
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET_PORT, port_str);
        ydebug("yetty: Telnet connect to '%s:%d' via in-process telnet", host, port_i);
        tk = YETTY_YUI_TAB_TELNET;
        break;
    }
    case YETTY_YUI_VIEW_YVNC:   tk = YETTY_YUI_TAB_YVNC;   break;
    default: return;
    }
    struct yetty_ycore_void_result r = yetty_yui_tabbar_add_workspace_of_kind(yetty->tabbar, tk);
    if (YETTY_IS_ERR(r)) {
        /* Surface the failure as an in-canvas toast so the user actually
         * sees it — silent failure was the original Telnet/SSH bug. The
         * label name is the human-readable view kind. */
        static const char *kind_label[] = {
            [YETTY_YUI_VIEW_SHELL]  = "Shell",
            [YETTY_YUI_VIEW_SSH]    = "SSH",
            [YETTY_YUI_VIEW_TELNET] = "Telnet",
            [YETTY_YUI_VIEW_YVNC]   = "yVNC",
            [YETTY_YUI_VIEW_EXEC]   = "Exec",
        };
        const char *label = ((unsigned)kind < (sizeof(kind_label) / sizeof(kind_label[0])))
                                ? kind_label[kind] : "Connect";
        ynotify(YETTY_YNOTIFY_ERROR, "%s connect failed: %s", label, r.error.msg);
        yetty_ycore_error_destroy(r.error);
    } else {
        ydebug("yetty: connect (kind=%d) spawned new workspace", (int)kind);
    }
    if (yetty->event_loop && yetty->event_loop->ops->request_render) {
        yetty->event_loop->ops->request_render(yetty->event_loop);
    }
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

    /* Create tabbar — owns the (initially single) workspace. */
    ydebug("yetty_create: Creating tabbar...");
    struct yetty_yui_tabbar_ptr_result tb_res = yetty_yui_tabbar_create(app_context->config);
    if (!YETTY_IS_OK(tb_res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "Failed to create tabbar");
    }
    yetty->tabbar = tb_res.value;
    ydebug("yetty_create: tabbar created");

    /* First workspace = the one defined by config; same payload the
     * pre-tabbar load_layout consumed. */
    ydebug("yetty_create: Loading initial workspace from config...");
    struct yetty_ycore_void_result layout_res = yetty_yui_tabbar_add_workspace_from_config(
        yetty->tabbar, app_context->config, &yetty->context);
    if (!YETTY_IS_OK(layout_res)) {
        yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, layout_res.error.msg);
    }
    ydebug("yetty_create: initial workspace loaded");

    /* App-level yui singleton. Sized to the initial framebuffer; cell
     * stride is a coarse 10x16 default — yui elements are absolute-pixel
     * positioned, so the scene-canvas grid is only an addressing index.
     * Non-fatal on failure: the rest of the app keeps working without
     * the top-z yui layer. */
    {
        uint32_t sw = app_context->app_gpu_context.surface_width;
        uint32_t sh = app_context->app_gpu_context.surface_height;
        if (sw == 0) {
            sw = 1;
        }
        if (sh == 0) {
            sh = 1;
        }
        struct yetty_yui_ptr_result yr =
            yetty_yui_create(&yetty->context, sw, sh, 10.0f, 16.0f);
        if (YETTY_IS_OK(yr)) {
            yetty->yui = yr.value;
            ydebug("yetty_create: yui created");

            /* Bridge tabbar v-button click → yui popup menu, and yui
             * Connect → tabbar add_workspace_of_kind. */
            yetty_yui_tabbar_set_v_menu_callback(yetty->tabbar, yetty_on_v_menu_click, yetty);
            yetty_yui_set_connect_callback(yetty->yui, yetty_on_yui_connect, yetty);
            yetty_yui_set_split_callback(yetty->yui, yetty_on_yui_split, yetty);
            /* Bind the tabbar model so yui's engine-pinned titlebar
             * (≡, tabs, +, drag, _, □, ✕) renders and reconciles
             * against the same workspace list yetty owns. */
            yetty_yui_set_tabbar_model(yetty->yui, yetty->tabbar);
        } else {
            ywarn("yetty_create: yui create failed: %s", yr.error.msg);
            yetty_ycore_error_destroy(yr.error);
        }
    }

    /* --temu / --qemu spawn an in-process VM whose only useful interfaces
     * are (a) the hvc0/virtio console and (b) a slirp-NAT'd telnet client
     * to the in-guest telnetd. The pty factory dispenses (a) on its first
     * create_pty call and (b) on its second; opening a second tab here
     * gives the user both views of the same VM out of the box.
     *
     * On webasm there is no flag — the only PTY backend is the in-iframe
     * TinyEMU VM (its factory is the only one compiled in), so the same
     * console+telnet pair is the default shape regardless of config. */
    bool is_temu = app_context->config->ops->get_bool(
        app_context->config, YETTY_YCONFIG_KEY_TEMU, 0);
    bool is_qemu = app_context->config->ops->get_bool(
        app_context->config, YETTY_YCONFIG_KEY_QEMU, 0);
#ifdef __EMSCRIPTEN__
    bool needs_console_telnet_pair = true;
    const char *which = "webasm";
#else
    bool needs_console_telnet_pair = is_temu || is_qemu;
    const char *which = is_temu ? "temu" : "qemu";
#endif
    if (needs_console_telnet_pair) {
        ydebug("yetty_create: %s — adding telnet tab", which);
        struct yetty_ycore_void_result tab2_res = yetty_yui_tabbar_add_workspace_from_config(
            yetty->tabbar, app_context->config, &yetty->context);
        if (!YETTY_IS_OK(tab2_res)) {
            yerror("yetty_create: failed to add telnet tab: %s", tab2_res.error.msg);
            yetty_ycore_error_destroy(tab2_res.error);
        } else {
            ydebug("yetty_create: telnet tab added");
        }
    }
    (void)is_temu;
    (void)is_qemu;

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

    /* Destroy app-level yui before tabbar/render_target. yui holds GPU
     * resources owned by the same wgpu/event-loop teardown chain below. */
    if (yetty->yui) {
        ydebug("yetty_destroy: destroying yui");
        struct yetty_ycore_void_result yr = yetty_yui_destroy(yetty->yui);
        if (YETTY_IS_ERR(yr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = yr;
            } else {
                yetty_ycore_error_destroy(yr.error);
            }
        }
        yetty->yui = NULL;
    }

    /* Destroy tabbar (cascades to each workspace, its tiles, and views). */
    if (yetty->tabbar) {
        ydebug("yetty_destroy: destroying tabbar");
        yetty_yui_tabbar_destroy(yetty->tabbar);
        yetty->tabbar = NULL;
        ydebug("yetty_destroy: tabbar destroyed");
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

    /* Destroy MSDF generator before the device (gpu impl borrows it). */
    if (yetty->context.gpu_context.msdf_generator) {
        ydebug("yetty_destroy: destroying MSDF generator");
        yetty->context.gpu_context.msdf_generator->ops->destroy(
            yetty->context.gpu_context.msdf_generator);
        yetty->context.gpu_context.msdf_generator = NULL;
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
