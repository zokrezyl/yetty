/*
 * yetty.c - Main yetty implementation
 */

#include <yetty/yetty/yetty.h>
#include <yetty/ycore/result.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yevent/event.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender-utils/screenshot.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ycore/math.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ynotify/ynotify.h>
#include <yetty/yui/workspace.h>
#include <yetty/yui/tabbar-model.h>
#include <yetty/yui/tile.h>
#include <yetty/yui/yui.h>
#include <yetty/yui-core/view.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/fs.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* Yetty instance */
struct yetty_yetty_yetty {
    struct yetty_context context;
    /* Cached `app_context.runtime` — the generic services layer (event
     * loop, adapter/device/queue/allocator/msdf, wgpu await, optional
     * VNC + RPC, render target). Created and owned by ymain via
     * yetty_yframework_create; yetty borrows everything through it. */
    struct yetty_yframework *runtime;
    /* Top-level UI. The tabbar owns N workspaces; only the active
     * workspace renders. Single-workspace boots create one tab so the
     * existing render/event paths see exactly the same shape as before
     * the tabbar was introduced. */
    struct yetty_yui_tabbar_model *tabbar;
    /* App-level ygui (popups, dialogs, future statusbar). Sits as the
     * highest-z layer above every terminal in the active workspace.
     * The tabbar above stays on its legacy rect path for now — yui is
     * additive; it does not displace anything. See src/yetty/yui/yui.h. */
    struct yetty_yui *yui;
    /* Alias of runtime->event_loop, for terseness in hot paths. */
    struct yetty_yevent_event_loop *event_loop;
    struct yetty_yevent_event_listener listener;

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
        if (yetty->runtime->render_target && yetty->runtime->render_target->ops->refresh_full) {
            yetty->runtime->render_target->ops->refresh_full(yetty->runtime->render_target);
        }
        /* Re-dispatch as a normal RENDER so the single render pipeline runs
         * exactly once, via the same code path used by all other triggers. */
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return yetty_event_handler(listener, &re);
    }

    /* Handle RENDER event directly - yetty owns the render cycle */
    if (event->type == YETTY_YCORE_RENDER) {
        if (!yetty->runtime->render_target) {
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
        if (yetty->runtime->render_target->ops->is_busy &&
            yetty->runtime->render_target->ops->is_busy(yetty->runtime->render_target)) {
            if (yetty->runtime->render_target->ops->notify_render_skipped) {
                yetty->runtime->render_target->ops->notify_render_skipped(
                    yetty->runtime->render_target);
            }
            ydebug("yetty: RENDER skipped (target busy)");
            return YETTY_OK(yetty_ycore_int, 1);
        }

        ydebug("yetty: RENDER event - calling workspace render");

        ytime_start(full_frame);

        /* Probe the yui scene-canvas BEFORE the panes draw. If yui has
         * pending chrome mutations (tab add/remove, dialog toggle,
         * statusbar update, menu open, splitter move) it'll paint over
         * the pane areas later this frame — the pixels it vacates would
         * otherwise show the previous frame's chrome. Forcing every
         * pane to redraw makes the bottom layers wipe those regions
         * before yui composites on top. Read once here; yui_render's
         * own sync runs again (idempotent) and emits + drains the OSC
         * frame. */
        struct yetty_ycore_int_result dirty_result = yetty_yui_is_dirty(yetty->yui);
        int yui_force = 1; /* on a failed probe, force a redraw to avoid stale chrome */
        if (YETTY_IS_OK(dirty_result)) {
            yui_force = dirty_result.value;
        } else {
            yetty_ycore_error_destroy(dirty_result.error);
        }

        ytime_start(workspace_render);
        if (yetty->tabbar) {
            struct yetty_ycore_void_result res = yetty_yui_tabbar_model_render(
                yetty->tabbar, yetty->runtime->render_target, yui_force);
            if (!YETTY_IS_OK(res)) {
                yerror("yetty: tabbar render failed: %s", res.error.msg);
            }
        }
        ytime_report(workspace_render);

        if (yetty->yui) {
            struct yetty_ycore_void_result yr =
                yetty_yui_render(yetty->yui, yetty->runtime->render_target);
            if (!YETTY_IS_OK(yr)) {
                yerror("yetty: yui render failed: %s", yr.error.msg);
                yetty_ycore_error_destroy(yr.error);
            }
        }

        ytime_start(present);
        struct yetty_ycore_void_result res =
            yetty->runtime->render_target->ops->present(yetty->runtime->render_target);
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
            yetty_yevent_post_async(yetty->context.runtime->platform_input_pipe, &ev);
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (ctrl) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_ZOOM_VISUAL;
            ev.zoom_visual.delta = event->mouse_scroll.dy * 0.1f;
            ev.zoom_visual.anchor_x = event->mouse_scroll.x;
            ev.zoom_visual.anchor_y = event->mouse_scroll.y;
            yetty_yevent_post_async(yetty->context.runtime->platform_input_pipe, &ev);
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
            yetty_yevent_post_async(yetty->context.runtime->platform_input_pipe, &ev);
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
            yetty_yevent_post_async(yetty->context.runtime->platform_input_pipe, &ev);
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
                {
                    struct yetty_ycore_int_result drop_r =
                        yetty_yui_tabbar_model_on_event(yetty->tabbar, &apply);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r,
                                        "drop: yetty_yui_tabbar_model_on_event");
                }
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
                    {
                        struct yetty_ycore_int_result drop_r =
                            yetty_yui_tabbar_model_on_event(yetty->tabbar, &apply);
                        YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r,
                                            "drop: yetty_yui_tabbar_model_on_event");
                    }
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
        struct yetty_ydraw_target *render_target = yetty->runtime->render_target;
        if (!render_target || !render_target->ops->get_texture) {
            yerror("yetty: SCREENSHOT but no render_target/get_texture");
            return YETTY_OK(yetty_ycore_int, 1);
        }
        WGPUTexture tex = render_target->ops->get_texture(render_target);
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
            yetty->runtime->gpu.device, yetty->runtime->gpu.queue, yetty->runtime->wgpu, tex, path);
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
            {
                struct yetty_ycore_int_result drop_r =
                    yetty_yui_tabbar_model_on_event(yetty->tabbar, event);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r,
                                    "drop: yetty_yui_tabbar_model_on_event");
            }
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

        /* Reconfigure surface via runtime helper — it also updates
         * runtime->gpu.app_gpu_context.surface_{width,height} so any
         * consumer reading those dimensions sees the post-resize size. */
        struct yetty_ycore_void_result rc =
            yetty_yframework_reconfigure_surface(yetty->runtime, width, height);
        if (YETTY_IS_ERR(rc)) {
            ywarn("yetty: surface reconfigure failed: %s", rc.error.msg);
            yetty_ycore_error_destroy(rc.error);
        }

        /* Resize render target */
        if (yetty->runtime->render_target && yetty->runtime->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, (float)width, (float)height};
            yetty->runtime->render_target->ops->resize(yetty->runtime->render_target, vp);
        }

        /* Resize the tabbar: it slices off the strip height at the top
         * and forwards the remainder to each workspace. We pre-subtract
         * yui's statusbar height from the bottom here so the workspace
         * area sits between [tabbar_strip .. H - statusbar_h]; without
         * this the bottom row of terminal cells is drawn under the
         * yui statusbar. */
        float bottom_inset = yetty->yui ? yetty_yui_statusbar_height(yetty->yui) : 0.0f;
        float ws_height = (float)height - bottom_inset;
        if (ws_height < 0.0f) {
            ws_height = 0.0f;
        }
        if (yetty->tabbar) {
            {
                struct yetty_ycore_void_result drop_r = yetty_yui_tabbar_model_resize(
                    yetty->tabbar, (float)width, ws_height, (float)height);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r, "drop: yetty_yui_tabbar_model_resize");
            }
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

    /* Chrome-driven structural mutations. The chrome side (yui's ygui
     * widgets, context-menu actions, splitter drags) doesn't mutate the
     * workspace directly — it pre-allocates the ids of every new tile
     * and posts one of these four events. The handler below materialises
     * the tile tree using the ids the chrome already keyed its widget
     * map on, so the two domains agree on identifiers without a
     * discovery round-trip. */
    if (event->type == YETTY_YCORE_WORKSPACE_CREATE) {
        if (yetty->tabbar) {
            struct yetty_yui_workspace *ws = NULL;
            struct yetty_ycore_void_result r = yetty_yui_tabbar_model_attach_empty_workspace(
                yetty->tabbar, event->workspace_create.workspace_id, yetty->context.runtime->config,
                &yetty->context, &ws);
            if (YETTY_IS_ERR(r)) {
                yerror("yetty: WORKSPACE_CREATE failed: %s", r.error.msg);
                yetty_ycore_error_destroy(r.error);
            } else {
                ydebug("yetty: WORKSPACE_CREATE id=%llu",
                       (unsigned long long)event->workspace_create.workspace_id);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    if (event->type == YETTY_YCORE_PANE_CREATE) {
        if (yetty->tabbar) {
            struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_find_workspace(
                yetty->tabbar, event->pane_create.workspace_id);
            if (!ws) {
                ywarn("yetty: PANE_CREATE: workspace %llu not found",
                      (unsigned long long)event->pane_create.workspace_id);
                return YETTY_OK(yetty_ycore_int, 1);
            }
            struct yetty_ycore_void_result r =
                yetty_yui_workspace_create_first_pane(ws, event->pane_create.pane_id);
            if (YETTY_IS_ERR(r)) {
                yerror("yetty: PANE_CREATE failed: %s", r.error.msg);
                yetty_ycore_error_destroy(r.error);
                return YETTY_OK(yetty_ycore_int, 1);
            }
            /* Install a default terminal view in the brand-new pane so
             * the first frame has something to render. The config-driven
             * view selection (vnc/client, telnet, ssh) still lives in
             * workspace_load_layout — chrome-driven creation defaults to
             * a plain terminal; the connect-dialog path swaps it later
             * via the existing yetty_on_yui_connect machinery. */
            struct yetty_yui_tile *root = yetty_yui_workspace_root(ws);
            struct yetty_yui_tile *pane =
                yetty_yui_tile_find_by_id(root, event->pane_create.pane_id);
            if (pane) {
                struct yetty_ycore_grid_size gs = {.rows = 24, .cols = 80};
                struct yetty_yterminal_terminal_result tr =
                    yetty_yterminal_terminal_create(gs, &yetty->context);
                if (YETTY_IS_ERR(tr)) {
                    yerror("yetty: PANE_CREATE: terminal create: %s", tr.error.msg);
                    yetty_ycore_error_destroy(tr.error);
                } else {
                    struct yetty_ycore_void_result pr = yetty_yui_tile_pane_push_view(
                        pane, yetty_yterminal_terminal_as_view(tr.value));
                    if (YETTY_IS_ERR(pr)) {
                        yerror("yetty: PANE_CREATE: push_view: %s", pr.error.msg);
                        yetty_ycore_error_destroy(pr.error);
                    }
                }
            }
            /* Make sure the new pane gets a non-empty rect now that a
             * root tile exists. */
            if (yetty->window_width > 0 && yetty->window_height > 0) {
                {
                    struct yetty_ycore_void_result drop_r = yetty_yui_tabbar_model_resize(
                        yetty->tabbar, yetty->window_width,
                        yetty->window_height -
                            (yetty->yui ? yetty_yui_statusbar_height(yetty->yui) : 0.0f),
                        yetty->window_height);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r, "yetty: tabbar resize");
                }
            }
            {
                struct yetty_ycore_void_result drop_r = yetty_yui_workspace_set_active(ws, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r,
                                    "drop: yetty_yui_workspace_set_active");
            }
            if (yetty->event_loop && yetty->event_loop->ops &&
                yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
            ydebug("yetty: PANE_CREATE ws=%llu pane=%llu",
                   (unsigned long long)event->pane_create.workspace_id,
                   (unsigned long long)event->pane_create.pane_id);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    if (event->type == YETTY_YCORE_PANE_SPLIT) {
        if (yetty->tabbar) {
            struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_find_workspace(
                yetty->tabbar, event->pane_split.workspace_id);
            if (!ws) {
                ywarn("yetty: PANE_SPLIT: workspace %llu not found",
                      (unsigned long long)event->pane_split.workspace_id);
                return YETTY_OK(yetty_ycore_int, 1);
            }
            enum yetty_yui_orientation orient =
                event->pane_split.orientation ? YETTY_YUI_HORIZONTAL : YETTY_YUI_VERTICAL;
            struct yetty_ycore_void_result sr = yetty_yui_workspace_split_pane_with_ids(
                ws, event->pane_split.target_pane_id, event->pane_split.new_pane_id,
                event->pane_split.new_split_id, orient);
            if (YETTY_IS_ERR(sr)) {
                yerror("yetty: PANE_SPLIT failed: %s", sr.error.msg);
                yetty_ycore_error_destroy(sr.error);
                return YETTY_OK(yetty_ycore_int, 1);
            }

            /* Drop a default terminal view into the freshly-minted
             * sibling and re-layout. Same logic the old synchronous
             * split path ran. */
            struct yetty_yui_tile *root = yetty_yui_workspace_root(ws);
            struct yetty_yui_tile *new_pane =
                yetty_yui_tile_find_by_id(root, event->pane_split.new_pane_id);
            if (new_pane) {
                struct yetty_ycore_grid_size gs = {.rows = 24, .cols = 80};
                struct yetty_yterminal_terminal_result tr =
                    yetty_yterminal_terminal_create(gs, &yetty->context);
                if (YETTY_IS_ERR(tr)) {
                    yerror("yetty: PANE_SPLIT: terminal create: %s", tr.error.msg);
                    //
                    yetty_ycore_error_destroy(tr.error);
                } else {
                    {
                        struct yetty_ycore_void_result drop_r = yetty_yui_tile_pane_push_view(
                            new_pane, yetty_yterminal_terminal_as_view(tr.value));
                        YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r,
                                            "drop: yetty_yui_tile_pane_push_view");
                    }
                }
                yetty_yui_tile_clear_focus(root);
                yetty_yui_tile_pane_set_focused(new_pane, 1);
            }
            if (yetty->window_width > 0 && yetty->window_height > 0) {
                {
                    struct yetty_ycore_void_result drop_r = yetty_yui_tabbar_model_resize(
                        yetty->tabbar, yetty->window_width,
                        yetty->window_height -
                            (yetty->yui ? yetty_yui_statusbar_height(yetty->yui) : 0.0f),
                        yetty->window_height);
                    YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r, "yetty: tabbar resize");
                }
            }
            if (yetty->event_loop && yetty->event_loop->ops &&
                yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
            ydebug("yetty: PANE_SPLIT ws=%llu target=%llu new_pane=%llu new_split=%llu",
                   (unsigned long long)event->pane_split.workspace_id,
                   (unsigned long long)event->pane_split.target_pane_id,
                   (unsigned long long)event->pane_split.new_pane_id,
                   (unsigned long long)event->pane_split.new_split_id);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    if (event->type == YETTY_YCORE_SPLIT_RESIZE) {
        if (yetty->tabbar) {
            struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_find_workspace(
                yetty->tabbar, event->split_resize.workspace_id);
            if (ws) {
                struct yetty_ycore_void_result r = yetty_yui_workspace_resize_split(
                    ws, event->split_resize.split_id, event->split_resize.ratio);
                if (YETTY_IS_ERR(r)) {
                    yerror("yetty: SPLIT_RESIZE failed: %s", r.error.msg);
                    yetty_ycore_error_destroy(r.error);
                }
            }
            if (yetty->event_loop && yetty->event_loop->ops &&
                yetty->event_loop->ops->request_render) {
                yetty->event_loop->ops->request_render(yetty->event_loop);
            }
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
            {
                struct yetty_ycore_int_result drop_r =
                    yetty_yui_tabbar_model_on_event(yetty->tabbar, event);
                YETTY_RETURN_IF_ERR(yetty_ycore_int, drop_r,
                                    "drop: yetty_yui_tabbar_model_on_event");
            }
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
        yetty_yevent_post_async(yetty->context.runtime->platform_input_pipe, &ev);
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
        float tabbar_h = yetty_dp_to_px(&yetty->context.runtime->gpu.app_gpu_context,
                                        YETTY_YUI_TABBAR_HEIGHT_DP);
        float status_h = yetty_yui_statusbar_height(yetty->yui);
        if (y >= tabbar_h && y < yetty->window_height - status_h) {
            /* Focus the clicked pane — same logic the workspace's own
             * MOUSE_DOWN handler runs, but without forwarding the
             * click to the terminal (we don't want the inner program
             * to see a mouse event for a UI gesture). */
            struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_active_workspace(yetty->tabbar);
            if (ws) {
                struct yetty_yui_tile *root = yetty_yui_workspace_root(ws);
                if (root) {
                    struct yetty_yui_tile *clicked =
                        yetty_yui_tile_find_pane_at(root, event->mouse.x, event->mouse.y);
                    if (clicked) {
                        yetty_yui_tile_clear_focus(root);
                        yetty_yui_tile_pane_set_focused(clicked, 1);
                    }
                }
            }
            /* The context menu is ygui chrome laid out in logical pixels;
             * convert the framebuffer-pixel click to logical for its anchor. */
            float menu_cs =
                yetty->runtime ? yetty->runtime->gpu.app_gpu_context.content_scale : 1.0f;
            if (menu_cs <= 0.0f) {
                menu_cs = 1.0f;
            }
            struct yetty_ycore_void_result menu_result = yetty_yui_show_context_menu(
                yetty->yui, event->mouse.x / menu_cs, event->mouse.y / menu_cs);
            if (YETTY_IS_ERR(menu_result)) {
                yetty_ycore_error_print(stderr, "yetty: show context menu", menu_result.error);
                yetty_ycore_error_destroy(menu_result.error);
            }
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
        int consumed = YETTY_IS_OK(yr) && yr.value;
        if (YETTY_IS_ERR(yr)) {
            yetty_ycore_error_destroy(yr.error);
        }
        /* Schedule a render whenever the event left the ygui engine dirty —
         * not only when yui *consumed* it. A hover-only mouse-move over a
         * titlebar control (minimize/maximize/close) flips the widget's hover
         * state (engine dirty) but is reported not-consumed so it can still
         * fall through to the tabbar for cursor/resize. Without this the hover
         * highlight wouldn't repaint until some unrelated render happened. */
        int want_render = consumed;
        if (!want_render) {
            struct yetty_ycore_int_result dirty_r = yetty_yui_is_dirty(yetty->yui);
            want_render = YETTY_IS_OK(dirty_r) && dirty_r.value;
            if (YETTY_IS_ERR(dirty_r)) {
                yetty_ycore_error_destroy(dirty_r.error);
            }
        }
        if (want_render && yetty->event_loop && yetty->event_loop->ops &&
            yetty->event_loop->ops->request_render) {
            yetty->event_loop->ops->request_render(yetty->event_loop);
        }
        if (consumed) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* Forward other events to the tabbar (which routes to active workspace). */
    if (yetty->tabbar) {
        return yetty_yui_tabbar_model_on_event(yetty->tabbar, event);
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
 * yui ↔ tabbar bridges
 *
 * The v-button on the tabbar fires `yetty_on_v_menu_click` which opens
 * yui's ygui popup_menu. When the user selects "Connect" in a dialog,
 * `yetty_on_yui_connect` translates the yui view-kind into the matching
 * tabbar kind and spawns a new workspace.
 *===========================================================================*/

YETTY_EXTERNAL_CALLBACK
static void yetty_on_v_menu_click(void *userdata, float anchor_x, float anchor_y)
{
    struct yetty_yetty_yetty *yetty = userdata;
    if (!yetty || !yetty->yui) {
        return;
    }
    struct yetty_ycore_void_result menu_result =
        yetty_yui_show_view_menu(yetty->yui, anchor_x, anchor_y);
    if (YETTY_IS_ERR(menu_result)) {
        yetty_ycore_error_print(stderr, "yetty: show view menu", menu_result.error);
        yetty_ycore_error_destroy(menu_result.error);
    }
    if (yetty->event_loop && yetty->event_loop->ops->request_render) {
        yetty->event_loop->ops->request_render(yetty->event_loop);
    }
}

/* Apply the kind-specific config keys (shell/command, ssh, telnet,
 * vnc/client) the same way yetty_on_yui_connect does so the next
 * terminal_create / viewer_create reads them. Returns 0 on success,
 * non-zero when validation fails (e.g. SSH/Telnet with empty host) —
 * caller surfaces a toast and aborts. */
static struct yetty_ycore_void_result yetty_apply_view_kind_to_config(
    struct yetty_yetty_yetty *yetty, enum yetty_yui_view_kind kind)
{
    struct yetty_yconfig_config *config = yetty->context.runtime->config;
    if (!config || !config->ops || !config->ops->set_string) {
        return YETTY_ERR(yetty_ycore_void, "apply_view_kind: config has no set_string");
    }
    /* Reset the exclusive flags up front so every kind has a clean slate. */
    config->ops->set_string(config, YETTY_YCONFIG_KEY_SSH, "false");
    config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET, "false");
    config->ops->set_string(config, "vnc/client", "");
    switch (kind) {
    case YETTY_YUI_VIEW_SHELL:
        config->ops->set_string(config, "shell/command", "");
        return YETTY_OK_VOID();
    case YETTY_YUI_VIEW_EXEC: {
        struct yetty_ycore_const_char_ptr_result cmd_result =
            yetty_yui_get_exec_command(yetty->yui);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cmd_result, "apply_view_kind: get exec command");
        const char *cmd = cmd_result.value;
        if (!cmd || !cmd[0]) {
            return YETTY_ERR(yetty_ycore_void, "apply_view_kind: EXEC empty command");
        }
        config->ops->set_string(config, "shell/command", cmd);
        return YETTY_OK_VOID();
    }
    case YETTY_YUI_VIEW_SSH: {
        struct yetty_ycore_const_char_ptr_result host_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, host_result, "apply_view_kind: SSH host");
        struct yetty_ycore_const_char_ptr_result port_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, port_result, "apply_view_kind: SSH port");
        struct yetty_ycore_const_char_ptr_result key_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 2);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, key_result, "apply_view_kind: SSH key");
        const char *host = host_result.value;
        const char *port = port_result.value;
        const char *key = key_result.value;
        if (!host || !host[0]) {
            return YETTY_ERR(yetty_ycore_void, "apply_view_kind: SSH empty host");
        }
        char cmd[1024];
        int n = snprintf(cmd, sizeof(cmd), "ssh");
        if (port && port[0]) {
            n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -p %s", port);
        }
        if (key && key[0]) {
            n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -i %s", key);
        }
        snprintf(cmd + n, sizeof(cmd) - (size_t)n, " %s", host);
        config->ops->set_string(config, "shell/command", cmd);
        return YETTY_OK_VOID();
    }
    case YETTY_YUI_VIEW_TELNET: {
        struct yetty_ycore_const_char_ptr_result host_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, host_result, "apply_view_kind: Telnet host");
        struct yetty_ycore_const_char_ptr_result port_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, port_result, "apply_view_kind: Telnet port");
        const char *host = host_result.value;
        const char *port = port_result.value;
        if (!host || !host[0]) {
            return YETTY_ERR(yetty_ycore_void, "apply_view_kind: Telnet empty host");
        }
        int port_i = (port && port[0]) ? atoi(port) : 23;
        if (port_i <= 0 || port_i > 65535) {
            return YETTY_ERR(yetty_ycore_void, "apply_view_kind: Telnet bad port");
        }
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port_i);
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET, "true");
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET_HOST, host);
        config->ops->set_string(config, YETTY_YCONFIG_KEY_TELNET_PORT, port_str);
        return YETTY_OK_VOID();
    }
    case YETTY_YUI_VIEW_YVNC:
        /* The yvnc dialog stashes host:port in vnc/client (TODO when
         * the dialog gets wired); for now the new pane will fall back
         * to a terminal if vnc/client is empty. */
        return YETTY_OK_VOID();
    }
    return YETTY_ERR(yetty_ycore_void, "apply_view_kind: unknown view kind");
}

/* Right-click context menu: pre-allocate the new pane + split ids and
 * post a PANE_SPLIT event. The actual tile-tree mutation happens in the
 * event handler, which uses the supplied ids so chrome's widget map and
 * the workspace tree agree on identifiers without a discovery
 * round-trip. orientation argument: 1 = horizontal divider (panes
 * side-by-side), 0 = vertical (panes stacked) — matches the labels in
 * the yui submenu. */
YETTY_EXTERNAL_CALLBACK
static void yetty_on_yui_split(void *userdata, enum yetty_yui_view_kind kind, int horizontal)
{
    struct yetty_yetty_yetty *yetty = userdata;
    if (!yetty || !yetty->tabbar) {
        return;
    }
    struct yetty_ycore_void_result apply_result = yetty_apply_view_kind_to_config(yetty, kind);
    if (YETTY_IS_ERR(apply_result)) {
        yetty_ycore_error_destroy(apply_result.error);
        ynotify(YETTY_YNOTIFY_WARN, "Split aborted: missing fields");
        return;
    }
    if (kind == YETTY_YUI_VIEW_YVNC) {
        ynotify(YETTY_YNOTIFY_INFO, "yVNC-in-split not yet wired — opened a terminal instead");
    }

    struct yetty_yui_workspace *ws = yetty_yui_tabbar_model_active_workspace(yetty->tabbar);
    if (!ws) {
        ywarn("yetty: split: no active workspace");
        return;
    }
    struct yetty_yui_tile *root = yetty_yui_workspace_root(ws);
    struct yetty_yui_tile *focused = root ? yetty_yui_tile_find_focused_pane(root) : NULL;
    if (!focused) {
        focused = yetty_yui_tile_find_first_pane(root);
    }
    if (!focused) {
        ywarn("yetty: split: no pane to split");
        return;
    }

    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_PANE_SPLIT;
    ev.pane_split.workspace_id = yetty_yui_workspace_id(ws);
    ev.pane_split.target_pane_id = yetty_yui_tile_id(focused);
    ev.pane_split.new_pane_id = yetty_ycore_next_object_id();
    ev.pane_split.new_split_id = yetty_ycore_next_object_id();
    ev.pane_split.orientation = horizontal ? 1 : 0;
    yetty_yevent_post_async(yetty->context.runtime->platform_input_pipe, &ev);
}

YETTY_EXTERNAL_CALLBACK
static void yetty_on_yui_connect(void *userdata, enum yetty_yui_view_kind kind)
{
    struct yetty_yetty_yetty *yetty = userdata;
    if (!yetty || !yetty->tabbar) {
        return;
    }
    enum yetty_yui_tabbar_model_kind tk;
    struct yetty_yconfig_config *config = yetty->context.runtime->config;
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
        struct yetty_ycore_const_char_ptr_result cmd_result =
            yetty_yui_get_exec_command(yetty->yui);
        const char *cmd = YETTY_IS_OK(cmd_result) ? cmd_result.value : NULL;
        if (YETTY_IS_ERR(cmd_result)) {
            yetty_ycore_error_destroy(cmd_result.error);
        }
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
        struct yetty_ycore_const_char_ptr_result host_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 0);
        struct yetty_ycore_const_char_ptr_result port_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 1);
        struct yetty_ycore_const_char_ptr_result key_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_SSH, 2);
        const char *host = YETTY_IS_OK(host_result) ? host_result.value : NULL;
        const char *port = YETTY_IS_OK(port_result) ? port_result.value : NULL;
        const char *key = YETTY_IS_OK(key_result) ? key_result.value : NULL;
        if (YETTY_IS_ERR(host_result)) {
            yetty_ycore_error_destroy(host_result.error);
        }
        if (YETTY_IS_ERR(port_result)) {
            yetty_ycore_error_destroy(port_result.error);
        }
        if (YETTY_IS_ERR(key_result)) {
            yetty_ycore_error_destroy(key_result.error);
        }
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
        if (port && port[0]) {
            n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -p %s", port);
        }
        if (key && key[0]) {
            n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, " -i %s", key);
        }
        snprintf(cmd + n, sizeof(cmd) - (size_t)n, " %s", host);
        config->ops->set_string(config, "shell/command", cmd);
        ydebug("yetty: SSH connect via ssh(1): %s", cmd);
        tk = YETTY_YUI_TAB_SHELL;
        break;
    }
    case YETTY_YUI_VIEW_TELNET: {
        struct yetty_ycore_const_char_ptr_result host_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 0);
        struct yetty_ycore_const_char_ptr_result port_result =
            yetty_yui_get_field_text(yetty->yui, YETTY_YUI_VIEW_TELNET, 1);
        const char *host = YETTY_IS_OK(host_result) ? host_result.value : NULL;
        const char *port = YETTY_IS_OK(port_result) ? port_result.value : NULL;
        if (YETTY_IS_ERR(host_result)) {
            yetty_ycore_error_destroy(host_result.error);
        }
        if (YETTY_IS_ERR(port_result)) {
            yetty_ycore_error_destroy(port_result.error);
        }
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
    case YETTY_YUI_VIEW_YVNC:
        tk = YETTY_YUI_TAB_YVNC;
        break;
    default:
        return;
    }
    struct yetty_ycore_void_result r =
        yetty_yui_tabbar_model_add_workspace_of_kind(yetty->tabbar, tk);
    if (YETTY_IS_ERR(r)) {
        /* Surface the failure as an in-canvas toast so the user actually
         * sees it — silent failure was the original Telnet/SSH bug. The
         * label name is the human-readable view kind. */
        static const char *kind_label[] = {
            [YETTY_YUI_VIEW_SHELL] = "Shell",   [YETTY_YUI_VIEW_SSH] = "SSH",
            [YETTY_YUI_VIEW_TELNET] = "Telnet", [YETTY_YUI_VIEW_YVNC] = "yVNC",
            [YETTY_YUI_VIEW_EXEC] = "Exec",
        };
        const char *label = ((unsigned)kind < (sizeof(kind_label) / sizeof(kind_label[0])))
                                ? kind_label[kind]
                                : "Connect";
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

struct yetty_yetty_yetty_result yetty_create(struct yetty_yframework *runtime,
                                             struct yetty_yplatform_pty_factory *pty_factory)
{
    ydebug("yetty_create: Starting...");

    if (!runtime) {
        return YETTY_ERR(yetty_yetty_yetty, "yetty_create: runtime is NULL");
    }
    if (!pty_factory) {
        return YETTY_ERR(yetty_yetty_yetty, "yetty_create: pty_factory is NULL");
    }

    struct yetty_yetty_yetty *yetty = calloc(1, sizeof(struct yetty_yetty_yetty));
    if (!yetty) {
        return YETTY_ERR(yetty_yetty_yetty, "Failed to allocate yetty");
    }
    yetty->visual_zoom_scale = 1.0f;

    /* Wire up the propagated context: runtime owns the GPU/event/RPC
     * services, pty_factory is yetty-specific, event_loop is a hot-path
     * alias. */
    yetty->runtime = runtime;
    yetty->context.runtime = runtime;
    yetty->context.pty_factory = pty_factory;
    yetty->event_loop = runtime->event_loop;
    yetty->context.event_loop = runtime->event_loop;

    struct yetty_yconfig_config *config = runtime->config;
    ydebug("yetty_create: context wired (event_loop=%p, device=%p, render_target=%p)",
           (void *)yetty->event_loop, (void *)runtime->gpu.device, (void *)runtime->render_target);

    /* Register event listeners */
    struct yetty_ycore_void_result res = register_event_listeners(yetty);
    if (!YETTY_IS_OK(res)) {
        (void)yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "failed to register event listeners");
    }

    /* Create tabbar — owns the (initially single) workspace. */
    ydebug("yetty_create: Creating tabbar...");
    struct yetty_yui_tabbar_model_ptr_result tb_res = yetty_yui_tabbar_model_create(config);
    if (!YETTY_IS_OK(tb_res)) {
        (void)yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "Failed to create tabbar");
    }
    yetty->tabbar = tb_res.value;
    ydebug("yetty_create: tabbar created");

    /* First workspace = the one defined by config; same payload the
     * pre-tabbar load_layout consumed. */
    ydebug("yetty_create: Loading initial workspace from config...");
    struct yetty_ycore_void_result layout_res =
        yetty_yui_tabbar_model_add_workspace_from_config(yetty->tabbar, config, &yetty->context);
    if (!YETTY_IS_OK(layout_res)) {
        (void)yetty_destroy(yetty);
        return YETTY_ERR(yetty_yetty_yetty, "yetty_create: load initial workspace failed",
                         layout_res);
    }
    ydebug("yetty_create: initial workspace loaded");

    /* App-level yui singleton. Sized to the initial framebuffer; cell
     * stride is a coarse 10x16 default — yui elements are absolute-pixel
     * positioned, so the scene-canvas grid is only an addressing index.
     * Non-fatal on failure: the rest of the app keeps working without
     * the top-z yui layer. */
    {
        uint32_t sw = runtime->gpu.app_gpu_context.surface_width;
        uint32_t sh = runtime->gpu.app_gpu_context.surface_height;
        if (sw == 0) {
            sw = 1;
        }
        if (sh == 0) {
            sh = 1;
        }
        struct yetty_yui_ptr_result yr = yetty_yui_create(&yetty->context, sw, sh, 10.0f, 16.0f);
        if (YETTY_IS_OK(yr)) {
            yetty->yui = yr.value;
            ydebug("yetty_create: yui created");

            /* Bridge tabbar v-button click → yui popup menu, and yui
             * Connect → tabbar add_workspace_of_kind. */
            yetty_yui_tabbar_model_set_v_menu_callback(yetty->tabbar, yetty_on_v_menu_click, yetty);
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
    bool is_temu = config->ops->get_bool(config, YETTY_YCONFIG_KEY_TEMU, 0);
    bool is_qemu = config->ops->get_bool(config, YETTY_YCONFIG_KEY_QEMU, 0);
#ifdef __EMSCRIPTEN__
    bool needs_console_telnet_pair = true;
    const char *which = "webasm";
#else
    bool needs_console_telnet_pair = is_temu || is_qemu;
    const char *which = is_temu ? "temu" : "qemu";
#endif
    if (needs_console_telnet_pair) {
        ydebug("yetty_create: %s — adding telnet tab", which);
        struct yetty_ycore_void_result tab2_res = yetty_yui_tabbar_model_add_workspace_from_config(
            yetty->tabbar, config, &yetty->context);
        if (!YETTY_IS_OK(tab2_res)) {
            yerror("yetty_create: failed to add telnet tab: %s", tab2_res.error.msg);
            yetty_ycore_error_destroy(tab2_res.error);
        } else {
            ydebug("yetty_create: telnet tab added");
        }
    }
    (void)is_temu;
    (void)is_qemu;

    /* RPC server: owned by yframework, already running by this point.
     * Apps that want yetty-specific RPC methods can register handlers
     * on yetty->runtime->rpc_server here. None today. */

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

    /* Tear down the yetty-owned chrome only. The render target, wgpu
     * await machinery, event loop, VNC, RPC, allocator, MSDF, queue,
     * device, adapter, and surface live on the runtime and are destroyed
     * by yetty_yframework_destroy — which the caller must run AFTER this
     * function returns. yui sits on top so it goes first. */

    if (yetty->yui) {
        ydebug("yetty_destroy: destroying yui");
        struct yetty_ycore_void_result yr = yetty_yui_destroy(yetty->yui);
        if (YETTY_IS_ERR(yr)) {
            first_err = yr;
        }
        yetty->yui = NULL;
    }

    /* Destroy tabbar (cascades to each workspace, its tiles, and views). */
    if (yetty->tabbar) {
        ydebug("yetty_destroy: destroying tabbar");
        (void)yetty_yui_tabbar_model_destroy(yetty->tabbar);
        yetty->tabbar = NULL;
        ydebug("yetty_destroy: tabbar destroyed");
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
