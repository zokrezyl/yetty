/*
 * tools/ycompositor-ygui/main.c — ygui rendered via the new
 * ycompositor pipeline (no scene-canvas, no PTY, no OSC).
 *
 * Mirrors the platform setup of tools/ycompositor (glfw + texture
 * render target + compositor render loop) but instead of a hand-built
 * ygrid full of SDF primitives this tool spins up a headless ygui
 * engine, builds a tiny widget tree, and pushes the engine's
 * draw_list bytes through yetty_ycompositor_process_records.
 *
 * On every RESIZE event the surface is reconfigured, the engine's
 * display pixel size is updated, and the ygui scene is re-emitted
 * (CMD_ZERO + fresh CMD_GROUPs) so the compositor's per-widget
 * yfigure_group + ygrid figures track the new geometry.
 */

#include <yetty/yinit/yinit.h>
#include <yetty/yruntime/yruntime.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ycompositor/compositor.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygui/ygui.h>
/* ygui_internal.h exposes the headless allocator (used by yui) and the
 * full engine struct so we can read engine->buffer post-rebuild. Same
 * pattern src/yetty/yui/yui.c uses. */
#include "yetty/ygui/ygui_internal.h"
#include <yetty/ytrace/ytrace.h>
#include <webgpu/webgpu.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* yetty_ygui_engine_rebuild is a global in ygui_engine.c but lacks a
 * header declaration. Forward-declare to call it here. */
struct yetty_ycore_void_result yetty_ygui_engine_rebuild(struct yetty_ygui_engine *engine);

/* Brand palette (rules/08-branding.md) — used directly on widget
 * properties since the default theme uses a darker surface that's hard
 * to see against the brand-bg backdrop. */
#define COL_BG_LIFTED   0xFF141A1Fu
#define COL_BORDER      0xFF364A47u
#define COL_TEXT_PRI    0xFFE0E5E4u
#define COL_ACCENT      0xFF6BA892u
#define COL_ACCENT_HI   0xFF74C5A5u

struct ycomp_ygui_app {
    int quit;
    struct yetty_context       ctx;
    struct yetty_yruntime     *yrt;
    struct yetty_ydraw_target *target;
    struct yetty_ycompositor  *comp;
    struct yetty_ygui_engine  *ygui;
    /* Borrowed pointer to the outer window so we can resize it to the
     * canvas dims on every push (ygui doesn't auto-fill via CSS in
     * the absence of a parent context — see ygreeter's on_resize). */
    struct yetty_ygui_widget  *win;
    /* Default font handed to the compositor; every per-group ygrid the
     * compositor creates borrows this at slot 0 so TEXT_SPAN records
     * (button labels, etc.) expand into renderable glyphs. */
    struct yetty_ydraw_font   *font;
    void                       *surface;
    uint32_t                    surface_w;
    uint32_t                    surface_h;
};

/* Build the widget tree once. A window holds a vbox body containing a
 * header label, a row of action buttons, a panel with checkboxes /
 * progress, and a list. Default theme drives all styling so we see
 * exactly what ygui produces.
 *
 * Sizes / positions are picked for a typical desktop window size; the
 * window auto-fills via flex CSS so the actual canvas dim doesn't
 * matter much. */
static void build_scene(struct ycomp_ygui_app *app)
{
    /* Outer window — placeholder size; push_ygui_scene resizes it to
     * the live surface dims on every push (same pattern ygreeter's
     * on_resize uses). */
    struct yetty_ygui_widget *win = yetty_ygui_engine_window(
        app->ygui, "win", 0.0f, 0.0f, 100.0f, 100.0f, "ycompositor-ygui demo");
    if (!win)
        return;
    app->win = win;
    struct yetty_ygui_widget *body = yetty_ygui_widget_window_body(win);

    /* Menubar with one menu so the chrome looks real. */
    struct yetty_ygui_widget *mb =
        yetty_ygui_engine_menubar(app->ygui, "mb", 0.0f, 0.0f, 100.0f, 26.0f);
    struct yetty_ygui_widget *mf =
        yetty_ygui_engine_popup_menu(app->ygui, "mf", 0.0f, 0.0f, 180.0f);
    yetty_ygui_widget_popup_menu_add_item(mf, "New tab", NULL, NULL);
    yetty_ygui_widget_popup_menu_add_item(mf, "Reload", NULL, NULL);
    yetty_ygui_widget_popup_menu_add_separator(mf);
    yetty_ygui_widget_popup_menu_add_item(mf, "Quit", NULL, NULL);
    yetty_ygui_widget_menubar_add(mb, "File", mf);
    yetty_ygui_widget_window_set_menubar(win, mb);

    /* Statusbar at the bottom. */
    struct yetty_ygui_widget *sb = yetty_ygui_engine_statusbar(
        app->ygui, "sb", 0.0f, 0.0f, 100.0f, 22.0f,
        "ycompositor-ygui — ygui via process_records");
    yetty_ygui_widget_statusbar_set_right(sb, "v0.1");
    yetty_ygui_widget_window_set_statusbar(win, sb);

    /* Body: vertical flex. Header label, action row, panel, list. */
    yetty_ygui_widget_apply_css(body,
        "display: flex; flex-direction: column; padding: 16px; gap: 12px;");

    struct yetty_ygui_widget *hdr = yetty_ygui_engine_label(
        app->ygui, "hdr", 0.0f, 0.0f,
        "ygui scene rendered through ycompositor");
    yetty_ygui_widget_add_child(body, hdr);

    /* Action row — horizontal flex with several buttons. */
    struct yetty_ygui_widget *actions =
        yetty_ygui_engine_vbox(app->ygui, "actions", 0.0f, 0.0f, 100.0f, 48.0f);
    yetty_ygui_widget_apply_css(actions,
        "display: flex; flex-direction: row; gap: 8px; height: 48px;");
    struct yetty_ygui_widget *a1 = yetty_ygui_engine_button(
        app->ygui, "act_new", 0.0f, 0.0f, 120.0f, 36.0f, "New");
    struct yetty_ygui_widget *a2 = yetty_ygui_engine_button(
        app->ygui, "act_open", 0.0f, 0.0f, 120.0f, 36.0f, "Open");
    struct yetty_ygui_widget *a3 = yetty_ygui_engine_button(
        app->ygui, "act_save", 0.0f, 0.0f, 120.0f, 36.0f, "Save");
    struct yetty_ygui_widget *a4 = yetty_ygui_engine_button(
        app->ygui, "act_del", 0.0f, 0.0f, 140.0f, 36.0f, "Delete");
    yetty_ygui_widget_add_child(actions, a1);
    yetty_ygui_widget_add_child(actions, a2);
    yetty_ygui_widget_add_child(actions, a3);
    yetty_ygui_widget_add_child(actions, a4);
    yetty_ygui_widget_add_child(body, actions);

    /* Settings panel with several checkboxes + a slider. */
    struct yetty_ygui_widget *panel =
        yetty_ygui_engine_panel(app->ygui, "panel", 0.0f, 0.0f, 100.0f, 220.0f);
    yetty_ygui_widget_apply_css(panel,
        "display: flex; flex-direction: column; padding: 12px; gap: 8px;");
    struct yetty_ygui_widget *plabel =
        yetty_ygui_engine_label(app->ygui, "plabel", 0.0f, 0.0f, "Settings");
    struct yetty_ygui_widget *cb1 = yetty_ygui_engine_checkbox(
        app->ygui, "cb1", 0.0f, 0.0f, 220.0f, 26.0f, "Enable animations", 1);
    struct yetty_ygui_widget *cb2 = yetty_ygui_engine_checkbox(
        app->ygui, "cb2", 0.0f, 0.0f, 220.0f, 26.0f, "Reduce motion", 0);
    struct yetty_ygui_widget *cb3 = yetty_ygui_engine_checkbox(
        app->ygui, "cb3", 0.0f, 0.0f, 220.0f, 26.0f, "Show debug overlay", 0);
    struct yetty_ygui_widget *slabel =
        yetty_ygui_engine_label(app->ygui, "slabel", 0.0f, 0.0f, "Brightness");
    struct yetty_ygui_widget *slider = yetty_ygui_engine_slider(
        app->ygui, "sl", 0.0f, 0.0f, 320.0f, 24.0f, 0.0f, 100.0f, 65.0f);
    yetty_ygui_widget_add_child(panel, plabel);
    yetty_ygui_widget_add_child(panel, cb1);
    yetty_ygui_widget_add_child(panel, cb2);
    yetty_ygui_widget_add_child(panel, cb3);
    yetty_ygui_widget_add_child(panel, slabel);
    yetty_ygui_widget_add_child(panel, slider);
    yetty_ygui_widget_add_child(body, panel);

    /* Trailing button row to confirm flex layout stops where it
     * should and the panel takes its share. */
    struct yetty_ygui_widget *footer = yetty_ygui_engine_button(
        app->ygui, "footer_btn", 0.0f, 0.0f, 160.0f, 38.0f, "Apply");
    yetty_ygui_widget_add_child(body, footer);
}

/* Push the current ygui scene through the compositor. Called from
 * worker startup and from every RESIZE event so the compositor's
 * per-widget figures track the live surface geometry.
 *
 * engine_rebuild itself doesn't clear the draw_list or emit CMD_ZERO
 * — that's engine_render's job, and engine_render also tries to ship
 * over OSC which we don't want in-process. So we wipe + lead with
 * CMD_ZERO manually here. */
static struct yetty_ycore_void_result push_ygui_scene(struct ycomp_ygui_app *app)
{
    if (!app->ygui)
        return YETTY_OK_VOID();
    yetty_ygui_engine_set_display_pixel_size(
        app->ygui, (float)app->surface_w, (float)app->surface_h);
    /* Resize the outer window to fill the surface — ygui doesn't
     * auto-stretch the root widget to the canvas. */
    if (app->win) {
        yetty_ygui_widget_set_size(
            app->win, (float)app->surface_w, (float)app->surface_h);
    }
    yetty_ydraw_draw_list_clear(app->ygui->buffer);
    struct yetty_ycore_void_result zr =
        yetty_ydraw_draw_list_add_cmd_zero(app->ygui->buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, zr, "push_ygui_scene: add CMD_ZERO");
    struct yetty_ycore_void_result rr = yetty_ygui_engine_rebuild(app->ygui);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "push_ygui_scene: engine_rebuild");
    const uint8_t *bytes =
        (const uint8_t *)yetty_ydraw_draw_list_data(app->ygui->buffer);
    size_t size = yetty_ydraw_draw_list_size(app->ygui->buffer);
    yinfo("ycompositor-ygui: feeding %zu ygui wire bytes through process_records",
          size);
    return yetty_ycompositor_process_records(app->comp, bytes, size);
}

static void handle_event(struct ycomp_ygui_app *app, const struct yetty_yui_event *ev)
{
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return;
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) return;
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result rr =
            yetty_yruntime_reconfigure_surface(app->yrt, w, h);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor-ygui: reconfigure_surface failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result tr =
            app->target->ops->resize(app->target, vp);
        if (YETTY_IS_ERR(tr)) {
            yerror("ycompositor-ygui: render_target resize failed: %s", tr.error.msg);
            yetty_ycore_error_destroy(tr.error);
        }
        /* Compositor sized in cells of 1 px each — cell sizing only
         * matters for the OSC wire path, which we bypass via direct
         * process_records calls. resize() here just damages every
         * existing figure so the next frame redraws them. */
        struct yetty_ycore_grid_size gs = {.cols = w, .rows = h};
        struct yetty_ycore_pixel_size cs = {.width = 1.0f, .height = 1.0f};
        struct yetty_ycore_void_result cr = yetty_ycompositor_resize(app->comp, gs, cs);
        if (YETTY_IS_ERR(cr)) {
            yerror("ycompositor-ygui: compositor resize failed: %s", cr.error.msg);
            yetty_ycore_error_destroy(cr.error);
        }
        struct yetty_ycore_void_result pr = push_ygui_scene(app);
        if (YETTY_IS_ERR(pr)) {
            yerror("ycompositor-ygui: push_ygui_scene (resize) failed: %s", pr.error.msg);
            yetty_ycore_error_destroy(pr.error);
        }
        return;
    }
    case YETTY_YCORE_KEY_DOWN:
        if (ev->key.key == 256 || ev->key.key == 81) {
            app->quit = 1;
        }
        return;
    case YETTY_YCORE_MOUSE_DOWN:
        if (app->ygui) {
            yetty_ygui_engine_mouse_down(app->ygui, ev->mouse.x, ev->mouse.y,
                                         ev->mouse.button);
        }
        /* Mouse input may have set HOVER/PRESSED flags or fired a
         * widget callback that mutated state; re-emit the scene so
         * the visual catches up. The CMD_ZERO at the head of every
         * push wipes the previous frame's groups before re-adding. */
        {
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_down) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    case YETTY_YCORE_MOUSE_UP:
        if (app->ygui) {
            yetty_ygui_engine_mouse_up(app->ygui, ev->mouse.x, ev->mouse.y,
                                       ev->mouse.button);
        }
        {
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_up) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
        if (app->ygui) {
            yetty_ygui_engine_mouse_move(app->ygui, ev->mouse.x, ev->mouse.y);
        }
        /* Re-emit only if ygui flagged a hover-state change; for now
         * always re-emit since dirty tracking inside the engine
         * already gates per-widget. */
        {
            struct yetty_ycore_void_result pr = push_ygui_scene(app);
            if (YETTY_IS_ERR(pr)) {
                yerror("ycompositor-ygui: push (mouse_move) failed: %s", pr.error.msg);
                yetty_ycore_error_destroy(pr.error);
            }
        }
        return;
    default:
        return;
    }
}

static struct yetty_ycore_void_result
ycomp_ygui_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct ycomp_ygui_app *app = user;

    struct yetty_yruntime_ptr_result yr = yetty_yruntime_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yruntime_create failed");
    app->yrt = yr.value;

    app->ctx.runtime     = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop  = app->yrt->event_loop;

    app->surface   = rt->surface;
    app->surface_w = rt->surface_width;
    app->surface_h = rt->surface_height;

    /* Replace yruntime's render target with a texture target that
     * blits to the GLFW surface on present — same setup as
     * tools/ycompositor. */
    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0,
        .w = (float)app->surface_w, .h = (float)app->surface_h,
    };
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    struct yetty_ycompositor_ptr_result cr = yetty_ycompositor_create(
        app->surface_w, app->surface_h, 1.0f, 1.0f, &app->ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "ycompositor_create failed");
    app->comp = cr.value;

    /* Load the default MSDF font once and pre-cache basic-latin so the
     * compositor can hand it to every per-group ygrid (slot 0) it
     * creates. Without this the button labels (TEXT_SPAN records)
     * silently drop every glyph and we see only the button chrome. */
    {
        struct yetty_yconfig_config *config = app->yrt->config;
        const char *fonts_dir   = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path),
                 "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir, font_family);
        snprintf(shader_path, sizeof(shader_path),
                 "%s/msdf-font.wgsl", shaders_dir);
        yinfo("ycompositor-ygui: loading font cdb='%s'", cdb_path);
        struct yetty_font_font_result font_result =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "ycompositor_ygui");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
        app->font = font_result.value;
        struct yetty_ycore_void_result load_result =
            app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load_result, "font load_basic_latin failed");
        yetty_ycompositor_set_default_font(app->comp, app->font);
    }

    /* Headless ygui engine — same path yui uses (no libuv loop, no
     * PTY handshake). Built once at startup; the widget tree stays
     * across resizes, only the canvas size changes. */
    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_internal_alloc_for_yui("ycompositor-ygui", /*theme=*/NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, eng_r, "ygui engine alloc failed");
    app->ygui = eng_r.value;
    build_scene(app);

    struct yetty_ycore_void_result pr0 = push_ygui_scene(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr0, "initial push_ygui_scene failed");

    /* Event-driven render loop — same shape as tools/ycompositor. */
    struct yetty_ycore_int_result fdr =
        rt->platform_input_pipe->ops->read_fd(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    int needs_render = 1;
    while (!app->quit) {
        struct pollfd pfd = {.fd = pipe_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, needs_render ? 0 : -1);
        int had_events = 0;
        if (pr > 0 && (pfd.revents & POLLIN)) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    rt->platform_input_pipe->ops->read(rt->platform_input_pipe,
                                                       &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) break;
                handle_event(app, &ev);
                had_events = 1;
            }
        }
        if (rt->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)rt->instance);
        }
        if (!(needs_render || had_events ||
              yetty_ycompositor_is_dirty(app->comp))) {
            continue;
        }

        struct yetty_ydraw_target *target = app->target;
        struct yetty_ycore_void_result cl = target->ops->clear(target);
        if (YETTY_IS_ERR(cl)) {
            yerror("ycompositor-ygui: clear failed: %s", cl.error.msg);
            yetty_ycore_error_destroy(cl.error);
        }
        struct yetty_ycore_int_result rr =
            yetty_ycompositor_render(app->comp, target, /*force=*/1);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor-ygui: compositor render failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_ycore_void_result pp = target->ops->present(target);
        if (YETTY_IS_ERR(pp)) {
            yerror("ycompositor-ygui: present failed: %s", pp.error.msg);
            yetty_ycore_error_destroy(pp.error);
        }
        needs_render = 0;
    }

    /* Teardown — compositor first so any pending GPU work bound to
     * the runtime's device flushes before yruntime_destroy. */
    struct yetty_ycore_void_result dr = yetty_ycompositor_destroy(app->comp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "compositor destroy");
    app->comp = NULL;

    app->target->ops->destroy(app->target);
    app->target = NULL;

    if (app->ygui) {
        struct yetty_ycore_void_result er = yetty_ygui_engine_destroy(app->ygui);
        if (YETTY_IS_ERR(er)) {
            yerror("ycompositor-ygui: ygui engine destroy failed: %s", er.error.msg);
            yetty_ycore_error_destroy(er.error);
        }
        app->ygui = NULL;
    }

    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }

    yetty_yruntime_destroy(app->yrt);
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct ycomp_ygui_app app = {0};
    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    return yetty_yinit_run(argc, argv, &cfg, ycomp_ygui_worker, &app);
}
