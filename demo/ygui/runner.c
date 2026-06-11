/*
 * demo/ygui/runner.c — bring-up shared by every ygui demo. Dual-mode.
 *
 *   TERM_PROGRAM=yetty  → CLIENT mode. Ships OSC envelopes over stdout
 *                         to the hosting yetty; stdin feeds keystrokes.
 *                         No own window. Backed by a libuv loop.
 *   otherwise           → STANDALONE mode. Opens its own GPU window via
 *                         yinit_run + yframework, routes ygui through
 *                         an in-process wire_statemachine into a local
 *                         yfigure_container, renders that tree itself.
 *
 * From ygui's perspective the two modes are identical: there's an
 * output_pty to write OSC envelopes to, and a caller pushes input
 * bytes via framework_feed_input. The framework has no knowledge of
 * which mode it's in.
 */

#include "runner.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/terminal-detect.h>
#include <yetty/yconfig/config.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yshadertoy/figure.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yface/yface.h>
#include <yetty/ywire/wire-statemachine.h>

/* Window chrome (drag/resize/maximize the borderless OS window) — the
 * reusable, ygui/yui-independent engine. This is the POC integration other
 * apps copy. */
#include <yetty/ychrome/chrome.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/label.h>
#include <yetty/yplatform/window-manager.h>

/* Caption-strip height (px) for chrome-enabled demos. The drawn strip and the
 * engine's drag/double-click zone share this value. */
#define DEMO_CHROME_CAPTION_H 34.0f

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <termios.h>

#ifdef YETTY_YGUI_HAS_UV
#include <uv.h>
#endif

struct demo_runner {
    const char *name;
    demo_build_fn build;

    struct yetty_ygui_framework *engine;
    struct yetty_ygui_object *root;

    struct yetty_yframework *yframework;
    struct yetty_yplatform_memory_pty_pair pty_pair;
    int has_pty_pair;
    struct yetty_yclass_object *root_container;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_ywire_wire_statemachine *wire_sm;
    struct yetty_yfont_font *font;
    struct yetty_ygrid_factory_args figure_args;
    struct yetty_yevent_event_listener listener;
    /* ~30 fps animation pump: fires request_render so self-dirtying
     * widgets (ymaze / yzoo / yjungle) keep re-emitting. */
    struct yetty_yevent_event_listener frame_listener;
    yetty_yevent_timer_id frame_timer;
    struct yetty_ydraw_target *render_target;

    /* Window chrome (standalone mode only). When enable_chrome is set the
     * runner draws a caption strip and routes unclaimed mouse events through
     * `chrome` to move/resize/maximize the borderless OS window. */
    int enable_chrome;
    struct yetty_yclass_object *chrome;      /* ychrome:chrome object, or NULL */
    struct yetty_ygrid_grid *chrome_caption; /* pinned figure compositing chrome's bar */
    int chrome_last_hover;                   /* last hovered control — repaint on change */
};

static struct yetty_ycore_int_result frame_tick(struct yetty_yevent_event_listener *listener,
                                                const struct yetty_yui_event *ev)
{
    (void)ev;
    struct demo_runner *r = container_of(listener, struct demo_runner, frame_listener);
    /* Force a full re-emit each tick so animated widgets re-run their
     * emit_body (advancing their own clock) — a widget's self-set dirty
     * flag does not survive the emit pass on its own. */
    if (r->engine) {
        yetty_ygui_framework_mark_dirty(r->engine);
    }
    if (r->yframework && r->yframework->event_loop &&
        r->yframework->event_loop->ops->request_render) {
        r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

struct yetty_ygui_framework *demo_runner_engine(struct demo_runner *r)
{
    return r ? r->engine : NULL;
}

struct yetty_ygui_object *demo_runner_root(struct demo_runner *r)
{
    return r ? r->root : NULL;
}

/* GLFW-style keycode → terminal byte sequence. Same map ygreeter uses. */
static const char *encode_key(uint32_t key, char *scratch, size_t scratch_n, size_t *out_len)
{
    if (key >= 32 && key < 127) {
        scratch[0] = (char)key;
        *out_len = 1;
        return scratch;
    }
    switch (key) {
    case 256:
        scratch[0] = 0x1B;
        *out_len = 1;
        return scratch; /* ESC */
    case 257:
        scratch[0] = '\r';
        *out_len = 1;
        return scratch; /* Enter */
    case 259:
        scratch[0] = 0x7F;
        *out_len = 1;
        return scratch; /* Backspace */
    case 263:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[D");
        return scratch;
    case 262:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[C");
        return scratch;
    case 265:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[A");
        return scratch;
    case 264:
        *out_len = (size_t)snprintf(scratch, scratch_n, "\x1b[B");
        return scratch;
    default:
        *out_len = 0;
        return NULL;
    }
}

static int on_key(struct yetty_ygui_framework *engine, uint32_t key, int mods, void *userdata)
{
    (void)engine;
    (void)mods;
    struct demo_runner *r = (struct demo_runner *)userdata;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        if (r->yframework && r->yframework->event_loop && r->yframework->event_loop->ops->stop) {
            r->yframework->event_loop->ops->stop(r->yframework->event_loop);
        }
        return 1;
    }
    return 0;
}

/* Slave end of the memory-pty pair: a resize on the producer (ygui) endpoint
 * arrives here — the SIGWINCH analog — and sizes the compositor's root
 * container from the pixel extent. cols/rows are terminal-grid concepts the
 * compositor has no use for. */
static void runner_pty_resize_cb(void *userdata, uint32_t cols, uint32_t rows, uint32_t pixel_w,
                                 uint32_t pixel_h)
{
    struct demo_runner *r = userdata;
    (void)cols;
    (void)rows;
    if (!r->root_container) {
        return;
    }
    struct yetty_ycore_rectangle rr = {.min = {0, 0}, .max = {(float)pixel_w, (float)pixel_h}};
    struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(r->root_container);
    yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(rf)-1, rr);
    yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 1);
}

/* Re-paint the chrome caption: ask ychrome to render its titlebar+buttons into
 * a drawable list (pure ydraw, no ygui), then load that record stream into the
 * pinned ygrid figure that composites it. Called on create + on resize. */
static void runner_chrome_caption_refresh(struct demo_runner *r)
{
    if (!r->chrome || !r->chrome_caption) {
        return;
    }
    struct yetty_ydraw_drawable_list_result lr = yetty_ychrome_render(NULL, r->chrome);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = lr.value;
    const uint8_t *data = (const uint8_t *)yetty_ydraw_drawable_list_data(list);
    size_t size = yetty_ydraw_drawable_list_size(list);
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(r->chrome_caption);
    struct yetty_yclass_object *fobj = (struct yetty_yclass_object *)(fig)-1;
    struct yetty_ycore_void_result rc = yetty_yfigure_reset_content(NULL, fobj);
    if (YETTY_IS_ERR(rc)) {
        yetty_ycore_error_destroy(rc.error);
    }
    if (data && size > 0) {
        struct yetty_ycore_void_result pb = yetty_yfigure_process_bytes(NULL, fobj, data, size);
        if (YETTY_IS_ERR(pb)) {
            yetty_ycore_error_destroy(pb.error);
        }
    }
    struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(fobj, 1);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    yetty_ydraw_drawable_list_destroy(list);
}

/* Create the pinned ygrid figure that composites chrome's self-rendered caption
 * on top of the app content. The caption pixels come entirely from ychrome
 * (ydraw), not ygui — a ygrid is just the framework's drawable-list→GPU figure. */
static struct yetty_ycore_void_result runner_chrome_caption_create(struct demo_runner *r,
                                                                   const struct yetty_context *ctx,
                                                                   float width)
{
    struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f},
                                         .max = {width, DEMO_CHROME_CAPTION_H}};
    struct yetty_ygrid_grid_ptr_result gr = yetty_ygrid_create(rect, 1, 1, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "demo_runner: chrome caption grid");
    r->chrome_caption = gr.value;
    if (r->font) {
        struct yetty_ycore_void_result fr = yetty_ygrid_set_font(r->chrome_caption, 0, r->font);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(r->chrome_caption);
    struct yetty_yclass_object *fobj = (struct yetty_yclass_object *)(fig)-1;
    /* Pin to the top (no scroll) and force on top of the app content. */
    struct yetty_ycore_void_result ar = yetty_yfigure_figure_absolute_coords_set(fobj, 1);
    if (YETTY_IS_ERR(ar)) {
        yetty_ycore_error_destroy(ar.error);
    }
    struct yetty_ycore_void_result zr = yetty_yfigure_figure_z_set(fobj, 100000);
    if (YETTY_IS_ERR(zr)) {
        yetty_ycore_error_destroy(zr.error);
    }
    struct yetty_ycore_void_result adr =
        yetty_yfigure_container_add_child(r->root_container, fig, 0x7FFF0001u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, adr, "demo_runner: chrome caption add_child");
    runner_chrome_caption_refresh(r);
    return YETTY_OK_VOID();
}

/* Forward one event to the window-chrome engine. Returns 1 if chrome claimed
 * it (caption drag/maximize, edge resize), 0 otherwise. No-op when chrome is
 * disabled (client mode / chrome create failed). */
static int runner_chrome_handle(struct demo_runner *r, const struct yetty_yui_event *ev)
{
    if (!r->enable_chrome || !r->chrome) {
        return 0;
    }
    struct yetty_ycore_int_result cr = yetty_ychrome_handle_event(NULL, r->chrome, ev);
    int consumed = YETTY_IS_OK(cr) && cr.value;
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
    }
    /* Re-paint the caption when the hovered control changes (hover highlight). */
    struct yetty_ycore_int_result hr = yetty_ychrome_hover_button(r->chrome);
    int hover = YETTY_IS_OK(hr) ? hr.value : 0;
    if (YETTY_IS_ERR(hr)) {
        yetty_ycore_error_destroy(hr.error);
    }
    if (hover != r->chrome_last_hover) {
        r->chrome_last_hover = hover;
        runner_chrome_caption_refresh(r);
    }
    return consumed;
}

static struct yetty_ycore_int_result event_handler(struct yetty_yevent_event_listener *listener,
                                                   const struct yetty_yui_event *ev)
{
    struct demo_runner *r = container_of(listener, struct demo_runner, listener);

    if (ev->type == YETTY_YCORE_WINDOW_REFRESH) {
        if (r->render_target && r->render_target->ops->refresh_full) {
            r->render_target->ops->refresh_full(r->render_target);
        }
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return event_handler(listener, &re);
    }

    if (ev->type == YETTY_YCORE_RENDER) {
        if (!r->render_target) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        if (r->render_target->ops->is_busy && r->render_target->ops->is_busy(r->render_target)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (yetty_ygui_framework_is_dirty(r->engine)) {
            struct yetty_ycore_void_result er = yetty_ygui_framework_emit(r->engine);
            if (YETTY_IS_ERR(er)) {
                yetty_ycore_error_destroy(er.error);
            }
        }
        if (r->wire_sm) {
            struct yetty_ycore_void_result pr = yetty_ywire_wire_statemachine_process(r->wire_sm);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
            }
        }
        struct yetty_ycore_void_result cl = r->render_target->ops->clear(r->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        if (r->root_container) {
            struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(r->root_container);
            struct yetty_ycore_void_result rr =
                yetty_yfigure_render(NULL, (struct yetty_yclass_object *)rf - 1, r->render_target);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 0);
        }
        struct yetty_ycore_void_result pp = r->render_target->ops->present(r->render_target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
        /* Animation pump: an animated widget (e.g. ymaze) re-marks itself
         * dirty during emit. present() throttles to vsync, so re-arming a
         * render whenever the framework is still dirty gives a steady frame
         * loop without a separate timer. Static demos never self-dirty, so
         * this is a no-op for them. */
        if (yetty_ygui_framework_is_dirty(r->engine) && r->yframework &&
            r->yframework->event_loop && r->yframework->event_loop->ops->request_render) {
            r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (r->yframework && r->yframework->event_loop && r->yframework->event_loop->ops->stop) {
            r->yframework->event_loop->ops->stop(r->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE:
        yetty_yframework_reconfigure_surface(r->yframework, (uint32_t)ev->resize.width,
                                             (uint32_t)ev->resize.height);
        if (r->render_target && r->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, ev->resize.width, ev->resize.height};
            r->render_target->ops->resize(r->render_target, vp);
        }
        {
            struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
                r->engine, (float)ev->resize.width, (float)ev->resize.height);
            if (YETTY_IS_ERR(vr)) {
                yetty_ycore_error_destroy(vr.error);
            }
        }
        /* Drive the compositor-side rect through the pty pair (→
         * runner_pty_resize_cb) rather than poking the container directly, so
         * the resize travels the pair like a real PTY's TIOCSWINSZ. */
        if (r->pty_pair.a && r->pty_pair.a->ops->resize) {
            struct yetty_ycore_void_result rrz = r->pty_pair.a->ops->resize(
                r->pty_pair.a, 0, 0, (uint32_t)ev->resize.width, (uint32_t)ev->resize.height);
            if (YETTY_IS_ERR(rrz)) {
                yetty_ycore_error_destroy(rrz.error);
            }
        }
        /* Keep chrome's edge bands + the caption figure tracking the window. */
        if (r->enable_chrome && r->chrome) {
            struct yetty_ycore_void_result csz = yetty_ychrome_set_size(
                NULL, r->chrome, (float)ev->resize.width, (float)ev->resize.height);
            if (YETTY_IS_ERR(csz)) {
                yetty_ycore_error_destroy(csz.error);
            }
            if (r->chrome_caption) {
                struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(r->chrome_caption);
                struct yetty_ycore_rectangle rect = {
                    .min = {0.0f, 0.0f}, .max = {(float)ev->resize.width, DEMO_CHROME_CAPTION_H}};
                struct yetty_ycore_void_result rr =
                    yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(fig)-1, rect);
                if (YETTY_IS_ERR(rr)) {
                    yetty_ycore_error_destroy(rr.error);
                }
                runner_chrome_caption_refresh(r);
            }
        }
        if (r->yframework->event_loop->ops->request_render) {
            r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_KEY_DOWN: {
        char scratch[8];
        size_t n = 0;
        const char *bytes = encode_key(ev->key.key, scratch, sizeof(scratch), &n);
        if (bytes && n > 0) {
            struct yetty_ycore_void_result fr =
                yetty_ygui_framework_feed_input(r->engine, bytes, n);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (r->yframework->event_loop->ops->request_render) {
                r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        /* Chrome integration model: the widget tree gets first dibs on a
         * press, so caption controls (and any widget near a resize edge) keep
         * working. Chrome only arms a window gesture when no widget consumed
         * the press — non-interactive caption fill (panel/label) doesn't
         * capture, so a press on the empty strip falls through to chrome. On
         * release, chrome ends an active move/resize before the widget tree. */
        if (ev->type == YETTY_YCORE_MOUSE_DOWN) {
            struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
                r->engine, ev->mouse.x, ev->mouse.y, ev->mouse.button, 1, ev->mouse.mods);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (r->enable_chrome && r->chrome &&
                !yetty_ygui_framework_has_pressed_widget(r->engine)) {
                runner_chrome_handle(r, ev);
            }
        } else {
            if (!runner_chrome_handle(r, ev)) {
                struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
                    r->engine, ev->mouse.x, ev->mouse.y, ev->mouse.button, 0, ev->mouse.mods);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
            }
        }
        if (r->yframework->event_loop->ops->request_render) {
            r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_SCROLL: {
        struct yetty_ycore_void_result fr = yetty_ygui_framework_feed_mouse_scroll(
            r->engine, ev->mouse_scroll.x, ev->mouse_scroll.y, ev->mouse_scroll.dx,
            ev->mouse_scroll.dy);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
        if (r->yframework->event_loop->ops->request_render) {
            r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        /* While chrome owns an active move/resize it consumes motion; otherwise
         * the widget tree gets it (hover, slider drag, …). */
        if (!runner_chrome_handle(r, ev)) {
            struct yetty_ycore_int_result fr =
                yetty_ygui_framework_feed_mouse_motion(r->engine, ev->mouse.x, ev->mouse.y);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
        }
        /* Motion can flip hover state — request a render so the next
         * emit cycle picks up the new hovered widget and repaints it
         * with the hover variant. Without this, the engine stays dirty
         * but the loop won't tick until another event arrives, so the
         * hover frame appears only on the next click. */
        if (r->yframework && r->yframework->event_loop &&
            r->yframework->event_loop->ops->request_render) {
            r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
        /* Double-click in the caption toggles maximize. Chrome gates on the
         * caption height internally, so a double-click on body content (below
         * the strip) is ignored here and falls through to the widget tree. */
        if (runner_chrome_handle(r, ev)) {
            if (r->yframework->event_loop->ops->request_render) {
                r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        break;
    default:
        break;
    }
    if (r->yframework && r->yframework->event_loop &&
        r->yframework->event_loop->ops->request_render) {
        r->yframework->event_loop->ops->request_render(r->yframework->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

static struct yetty_ycore_void_result worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct demo_runner *r = (struct demo_runner *)user;

    struct yetty_yframework_ptr_result frr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frr, "demo_runner: yframework_create");
    r->yframework = frr.value;
    r->render_target = r->yframework->render_target;

    /* MSDF font for the receiver-side ygrid. */
    {
        const char *fonts_dir =
            r->yframework->config->ops->get_string(r->yframework->config, "paths/fonts", "");
        const char *shaders_dir =
            r->yframework->config->ops->get_string(r->yframework->config, "paths/shaders", "");
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 "DejaVuSansMNerdFontMono");
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result fr =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "demo_runner_default");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "demo_runner: msdf_font_create");
        r->font = fr.value;
        struct yetty_ycore_void_result load = r->font->ops->load_basic_latin(r->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load, "demo_runner: load_basic_latin");
    }

    /* Raw figure factory + producer kinds (yplot, yimage). */
    {
        struct yetty_ydraw_composite_factory_ptr_result ffr = yetty_ydraw_composite_factory_create(
            r->yframework->gpu.device, r->yframework->gpu.queue, r->yframework->gpu.surface_format,
            r->yframework->gpu.allocator, r->yframework->event_loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ffr, "demo_runner: raw_composite_factory_create");
        r->composite_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(r->composite_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(r->composite_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
    }

    /* Figure registry. */
    {
        struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reg, "demo_runner: registry_create");
        r->figure_registry = reg.value;
        r->figure_args.default_font = r->font;
        r->figure_args.composite_factory = r->composite_factory;
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(r->figure_registry, &r->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "demo_runner: ygrid_register_factory");
        const uint32_t producer_kinds[] = {
            YETTY_YFIGURE_KIND_YPLOT,
            YETTY_YFIGURE_KIND_YIMAGE,
        };
        for (size_t i = 0; i < sizeof(producer_kinds) / sizeof(producer_kinds[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                r->figure_registry, producer_kinds[i], &r->figure_args);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, kr,
                                "demo_runner: ygrid_register_factory_for_kind");
        }
        /* yshadertoy has its own factory + renderer (not the ygrid path). */
        struct yetty_ycore_void_result sr = yetty_yshadertoy_register_factory(r->figure_registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "demo_runner: yshadertoy_register_factory");
    }

    /* Local container. */
    struct yetty_context ctx = {.runtime = r->yframework, .event_loop = r->yframework->event_loop};
    {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0, 0}, .max = {(float)rt->surface_width, (float)rt->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "demo_runner: container_create");
        r->root_container = obj_res.value;
        yetty_yfigure_container_set_context(r->root_container, &ctx);
        yetty_yfigure_container_set_registry(r->root_container, r->figure_registry);
        yetty_yfigure_container_set_rect(r->root_container, root_rect);
    }

    /* Memory pty pair → wire SM → container. */
    {
        struct yetty_yplatform_memory_pty_pair_result pr =
            yetty_yplatform_memory_pty_pair_create(0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "demo_runner: memory_pty_pair_create");
        r->pty_pair = pr.value;
        r->has_pty_pair = 1;
    }
    {
        struct yetty_ywire_wire_statemachine_ptr_result sr =
            yetty_ywire_wire_statemachine_create(r->pty_pair.b);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "demo_runner: wire_sm_create");
        r->wire_sm = sr.value;
        struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register(
            r->wire_sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YCOMPOSITOR_BIN, /*has_args=*/1,
            yetty_yfigure_container_process_input, r->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "demo_runner: wire_sm register");
    }

    /* ygui framework on producer end. */
    {
        struct yetty_ygui_framework_ptr_result fr = yetty_ygui_framework_create(r->pty_pair.a);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "demo_runner: framework_create");
        r->engine = fr.value;
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            r->engine, (float)rt->surface_width, (float)rt->surface_height);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
    }

    /* Two-level root: an outer vbox owns the viewport; an inner body
     * panel fills it with the brand background and provides padding +
     * a column layout. Demos receive the body panel as their `root`
     * argument, so they get a styled, padded canvas instead of a bare
     * window with a black void below their widgets. */
    struct yetty_ygui_object *body = NULL;
    {
        struct yetty_ygui_object_ptr_result rr =
            yetty_ygui_add(yetty_ygui_vbox_class_get().value, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "demo_runner: root add");
        r->root = rr.value;
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(r->root);
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(r->root, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "demo_runner: root layout");
        struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(r->engine, r->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "demo_runner: set_root");

        /* The chrome caption is NOT a ygui widget — it's painted by ychrome via
         * ydraw and composited as a pinned ygrid figure (runner_chrome_caption_*
         * below), so it stays independent of ygui. */

        struct yetty_ygui_object_ptr_result br =
            yetty_ygui_add(yetty_ygui_panel_class_get().value, r->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "demo_runner: body add");
        body = br.value;
        /* Panel defaults to ROW direction; flip to COLUMN so demos can
         * stack widgets top-to-bottom without first reshaping the root. */
        struct yetty_ygui_layout bl = *yetty_ygui_widget_layout_get(body);
        bl.direction = YETTY_YGUI_FLEX_COLUMN;
        bl.align = YETTY_YGUI_ALIGN_STRETCH;
        bl.flex_grow = 1.0f;
        bl.padding_top = bl.padding_bottom = 24;
        bl.padding_left = bl.padding_right = 24;
        /* The caption figure overlays the top strip; inset the body so its
         * content isn't hidden behind it. */
        if (r->enable_chrome) {
            bl.padding_top += DEMO_CHROME_CAPTION_H;
        }
        bl.gap = 12;
        /* Center the demo's content vertically when it doesn't fill the
         * available height — small demos (single slider, single button)
         * otherwise sit awkwardly anchored to the top with a large void
         * below. Demos that DO want to fill the height set flex_grow on
         * their own children, which makes the justify a no-op. */
        bl.justify = YETTY_YGUI_JUSTIFY_CENTER;
        struct yetty_ycore_void_result blr = yetty_ygui_widget_layout_set(body, &bl);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, blr, "demo_runner: body layout");
    }

    /* Window-chrome engine — bind it to the borderless OS window's manager so
     * the caption strip and edges drive real move/resize/maximize. Borrowed
     * window_manager comes from yinit via yframework; absent in headless. A
     * failure here just leaves chrome disabled (window stays static) rather
     * than aborting the demo. */
    if (r->enable_chrome && r->yframework->window_manager) {
        struct yetty_ycore_void_result creg = yetty_ychrome_register();
        if (YETTY_IS_ERR(creg)) {
            yetty_ycore_error_destroy(creg.error);
        }
        struct yetty_yclass_object_ptr_result cor = yetty_ychrome_chrome_create(NULL);
        if (YETTY_IS_OK(cor)) {
            r->chrome = cor.value;
            struct yetty_ycore_void_result ccfg = yetty_ychrome_configure(
                NULL, r->chrome, r->yframework->window_manager, DEMO_CHROME_CAPTION_H,
                /*edge_size=*/8.0f, YETTY_YCHROME_FLAG_ALL);
            if (YETTY_IS_ERR(ccfg)) {
                yetty_ycore_error_destroy(ccfg.error);
            }
            struct yetty_ycore_void_result csz = yetty_ychrome_set_size(
                NULL, r->chrome, (float)rt->surface_width, (float)rt->surface_height);
            if (YETTY_IS_ERR(csz)) {
                yetty_ycore_error_destroy(csz.error);
            }
            /* Composite the chrome-painted caption as a pinned top figure. */
            struct yetty_ycore_void_result cap =
                runner_chrome_caption_create(r, &ctx, (float)rt->surface_width);
            if (YETTY_IS_ERR(cap)) {
                ywarn("demo_runner: chrome caption create failed: %s", cap.error.msg);
                yetty_ycore_error_destroy(cap.error);
            }
        } else {
            ywarn("demo_runner: chrome create failed: %s", cor.error.msg);
            yetty_ycore_error_destroy(cor.error);
        }
    }

    yetty_ygui_framework_set_key_cb(r->engine, on_key, r);

    /* Demo-supplied tree build — into the styled body panel, not the
     * raw framework root. */
    if (r->build) {
        struct yetty_ycore_void_result br = r->build(r, body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "demo_runner: build");
    }

    /* Wake → request_render so producer writes drive the event loop. */
    yetty_yplatform_memory_pty_set_wake(
        r->pty_pair.b,
        (yetty_yplatform_memory_pty_wake_fn)r->yframework->event_loop->ops->request_render,
        r->yframework->event_loop);

    /* Window-size changes reach the compositor end through the pair, the same
     * way bytes do. */
    yetty_yplatform_memory_pty_set_resize(r->pty_pair.b, runner_pty_resize_cb, r);

    r->listener.handler = event_handler;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(r->yframework->event_loop, &r->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "demo_runner: register_default_listeners");

    /* Start the ~30 fps animation pump. */
    {
        struct yetty_yevent_event_loop *loop = r->yframework->event_loop;
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        if (YETTY_IS_OK(tr)) {
            r->frame_timer = tr.value;
            r->frame_listener.handler = frame_tick;
            struct yetty_ycore_void_result cr = loop->ops->config_timer(loop, r->frame_timer, 33);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
            struct yetty_ycore_void_result lr =
                loop->ops->register_timer_listener(loop, r->frame_timer, &r->frame_listener);
            if (YETTY_IS_ERR(lr)) {
                yetty_ycore_error_destroy(lr.error);
            }
            struct yetty_ycore_void_result st = loop->ops->start_timer(loop, r->frame_timer);
            if (YETTY_IS_ERR(st)) {
                yetty_ycore_error_destroy(st.error);
            }
        } else {
            yetty_ycore_error_destroy(tr.error);
        }
    }

    yetty_yevent_post_async(rt->platform_input_pipe,
                            &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        r->yframework->event_loop->ops->start(r->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown — mirror ygreeter's order. */
    if (r->chrome) {
        struct yetty_ycore_void_result dr = yetty_ychrome_destroy(NULL, r->chrome);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        r->chrome = NULL;
    }
    if (r->engine) {
        struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(r->engine);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        r->engine = NULL;
    }
    if (r->wire_sm) {
        struct yetty_ycore_void_result dr = yetty_ywire_wire_statemachine_destroy(r->wire_sm);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        r->wire_sm = NULL;
    }
    if (r->has_pty_pair) {
        if (r->pty_pair.a && r->pty_pair.a->ops->destroy) {
            struct yetty_ycore_void_result dr = r->pty_pair.a->ops->destroy(r->pty_pair.a);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
        }
        if (r->pty_pair.b && r->pty_pair.b->ops->destroy) {
            struct yetty_ycore_void_result dr = r->pty_pair.b->ops->destroy(r->pty_pair.b);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
        }
        r->has_pty_pair = 0;
    }
    if (r->root_container) {
        struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(r->root_container);
        struct yetty_ycore_void_result dr =
            yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)rf - 1);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        r->root_container = NULL;
    }
    if (r->figure_registry) {
        yetty_yfigure_registry_destroy(r->figure_registry);
        r->figure_registry = NULL;
    }
    if (r->composite_factory) {
        yetty_ydraw_composite_factory_destroy(r->composite_factory);
        r->composite_factory = NULL;
    }
    if (r->font) {
        r->font->ops->destroy(r->font);
        r->font = NULL;
    }
    if (r->yframework) {
        yetty_yframework_destroy(r->yframework);
        r->yframework = NULL;
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Client mode — used when TERM_PROGRAM=yetty. Mirrors the same path
 * tools/ygreeter/main.c uses: a uv loop polling stdin into framework
 * input, watching SIGWINCH for viewport changes, draining ygui's emit
 * on each prepare tick out to STDOUT (which the hosting yetty consumes).
 *===========================================================================*/

#ifdef YETTY_YGUI_HAS_UV

/*-----------------------------------------------------------------------------
 * STDIN raw-mode — when the hosting yetty forwards mouse events to us as
 * OSC 700000 envelopes written to the PTY master, the slave's tty driver
 * in cooked/ECHO mode echoes those bytes back to the master. yetty re-reads
 * them, sees ESC pretty-printed as "^[" (caret-notation, two bytes), can't
 * parse it as an envelope, and the text-layer renders the garbage on
 * screen. Putting STDIN in raw (no ECHO, no canonical) breaks the loop.
 *
 * The static here is the same pattern tools/ybrowser/main.c uses — atexit
 * needs to find the saved termios without taking an argument.
 *---------------------------------------------------------------------------*/
static struct termios runner_saved_tty;
static int runner_tty_active = 0;

static void runner_tty_restore(void)
{
    if (runner_tty_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &runner_saved_tty);
        runner_tty_active = 0;
    }
}

static int runner_tty_raw(void)
{
    if (!isatty(STDIN_FILENO)) {
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &runner_saved_tty) < 0) {
        return -1;
    }
    struct termios raw = runner_saved_tty;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        return -1;
    }
    runner_tty_active = 1;
    atexit(runner_tty_restore);
    return 0;
}

struct stdout_pty {
    struct yetty_platform_pty base;
    uv_pipe_t pipe;
};

struct stdout_write {
    uv_write_t req;
    char *data;
};

static void on_write_done(uv_write_t *req, int status)
{
    struct stdout_write *w = (struct stdout_write *)req;
    if (status != 0) {
        fprintf(stderr, "demo_runner client: uv_write status=%d (%s)\n", status,
                uv_strerror(status));
    }
    free(w->data);
    free(w);
}

static struct yetty_ycore_size_result stdout_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct stdout_pty *p = (struct stdout_pty *)self;
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    struct stdout_write *w = calloc(1, sizeof(*w));
    if (!w) {
        return YETTY_ERR(yetty_ycore_size, "stdout_pty_write: calloc req");
    }
    w->data = malloc(len);
    if (!w->data) {
        free(w);
        return YETTY_ERR(yetty_ycore_size, "stdout_pty_write: malloc data");
    }
    memcpy(w->data, data, len);
    uv_buf_t buf = uv_buf_init(w->data, (unsigned)len);
    int rc = uv_write(&w->req, (uv_stream_t *)&p->pipe, &buf, 1, on_write_done);
    if (rc != 0) {
        free(w->data);
        free(w);
        return YETTY_ERR(yetty_ycore_size, "stdout_pty_write: uv_write submit");
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result stdout_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    (void)self;
    (void)buf;
    (void)max_len;
    return YETTY_OK(yetty_ycore_size, 0);
}

static struct yetty_ycore_void_result stdout_pty_no_op(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stdout_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows, uint32_t pw,
                                                        uint32_t ph)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)pw;
    (void)ph;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *stdout_pty_pipe_source(struct yetty_platform_pty *s)
{
    (void)s;
    return NULL;
}

static const struct yetty_platform_pty_ops *stdout_pty_ops_get(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = stdout_pty_no_op,
        .read = stdout_pty_read,
        .write = stdout_pty_write,
        .resize = stdout_pty_resize,
        .stop = stdout_pty_no_op,
        .pipe_source = stdout_pty_pipe_source,
    };
    return &ops;
}

struct client_state {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    uv_timer_t frame_timer; /* ~30 fps animation pump */
    struct stdout_pty out;
    /* Async-delivery PTY shim for stdin — bytes arrive via the libuv
     * poll callback and get fed into the wire SM with _feed; the SM
     * itself never reads through this PTY. */
    struct yetty_platform_pty stdin_pty;
    struct yetty_ywire_wire_statemachine *wire_sm;
    struct demo_runner *runner;
    int running;
};

static int client_on_key(struct yetty_ygui_framework *engine, uint32_t key, int mods,
                         void *userdata)
{
    (void)engine;
    (void)mods;
    struct client_state *cs = (struct client_state *)userdata;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        cs->running = 0;
        return 1;
    }
    return 0;
}

/*-----------------------------------------------------------------------------
 * Stdin PTY shim — placeholder ops the wire SM holds. All async-delivery:
 * bytes arrive via yetty_ywire_wire_statemachine_feed() from libuv's
 * stdin poll. The SM never calls .read.
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_size_result stdin_pty_read(struct yetty_platform_pty *self, char *buf,
                                                     size_t n)
{
    (void)self;
    (void)buf;
    (void)n;
    return YETTY_OK(yetty_ycore_size, 0);
}
static struct yetty_ycore_size_result stdin_pty_write(struct yetty_platform_pty *self,
                                                      const char *data, size_t n)
{
    (void)self;
    (void)data;
    return YETTY_OK(yetty_ycore_size, n);
}
static struct yetty_ycore_void_result stdin_pty_noop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}
static struct yetty_ycore_void_result stdin_pty_resize(struct yetty_platform_pty *self,
                                                       uint32_t cols, uint32_t rows, uint32_t pw,
                                                       uint32_t ph)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)pw;
    (void)ph;
    return YETTY_OK_VOID();
}
static struct yetty_platform_pty_pipe_source *stdin_pty_pipe_source(struct yetty_platform_pty *s)
{
    (void)s;
    return NULL;
}
static const struct yetty_platform_pty_ops *stdin_pty_ops_get(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = stdin_pty_noop,
        .read = stdin_pty_read,
        .write = stdin_pty_write,
        .resize = stdin_pty_resize,
        .stop = stdin_pty_noop,
        .pipe_source = stdin_pty_pipe_source,
    };
    return &ops;
}

/*-----------------------------------------------------------------------------
 * Wire SM default sink — bytes outside any OSC envelope are real
 * keyboard input from the controlling terminal. Forward them to ygui's
 * input decoder verbatim (it understands CSI arrow sequences + ASCII).
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result client_default_sink(void *userdata,
                                                          struct yetty_ywire_wire_statemachine *sm)
{
    struct client_state *cs = (struct client_state *)userdata;
    uint8_t buf[256];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, buf, sizeof(buf));
        if (YETTY_IS_ERR(rr)) {
            return YETTY_ERR(yetty_ycore_void, "default_sink: read", rr);
        }
        if (rr.value == 0) {
            /* Empty read in SCAN_RAW means _read yielded internally and
             * came back with nothing — just loop and try again. The SM
             * expects this coro to loop forever. */
            continue;
        }
        struct yetty_ycore_void_result fr =
            yetty_ygui_framework_feed_input(cs->runner->engine, (const char *)buf, rr.value);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
}

/*-----------------------------------------------------------------------------
 * Mouse handler — yetty forwards pointer events to the inferior as
 * OSC 700010 (pane-wide; SUB-subscribed) carrying a yetty_client_input_mouse.
 * Drain the envelope body, sanity-check the magic, dispatch to ygui's
 * framework_feed_mouse_* entry points (same path standalone uses).
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result client_mouse_handler(void *userdata,
                                                           struct yetty_ywire_wire_statemachine *sm)
{
    /* userdata is cs.runner (distinct from set_default's &cs — see
     * the comment at the registration site). */
    struct demo_runner *runner = (struct demo_runner *)userdata;
    for (;;) {
        struct yetty_client_input_mouse msg;
        size_t got = 0;
        while (got < sizeof(msg)) {
            struct yetty_ycore_size_result rr =
                yetty_ywire_wire_statemachine_read(sm, ((uint8_t *)&msg) + got, sizeof(msg) - got);
            if (YETTY_IS_ERR(rr)) {
                return YETTY_ERR(yetty_ycore_void, "mouse: read", rr);
            }
            if (rr.value == 0) {
                /* Envelope ended (terminator + no more bytes) — bail out
                 * of inner read, drop any partial struct, loop back to
                 * wait for the next envelope. MUST NOT return here — the
                 * SM expects the handler coro to loop forever; returning
                 * makes it warn "returned cleanly" and lose the next
                 * envelope's body. */
                break;
            }
            got += rr.value;
        }
        if (got == sizeof(msg) && msg.magic == YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
            switch (msg.kind) {
            case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
                struct yetty_ycore_int_result r = yetty_ygui_framework_feed_mouse_button(
                    runner->engine, msg.x, msg.y, msg.button, msg.pressed, 0);
                if (YETTY_IS_ERR(r)) {
                    yetty_ycore_error_destroy(r.error);
                }
                break;
            }
            case YETTY_YMGUI_INPUT_MOUSE_POS: {
                struct yetty_ycore_int_result r =
                    yetty_ygui_framework_feed_mouse_motion(runner->engine, msg.x, msg.y);
                if (YETTY_IS_ERR(r)) {
                    yetty_ycore_error_destroy(r.error);
                }
                break;
            }
            default:
                break;
            }
        }
        /* Yield after each envelope — _read at EOE returns 0 without
         * yielding internally, so without an explicit yield here the
         * handler coro spins and the SM scanner never gets control to
         * transition out of OSC_BODY for the next envelope. */
        yetty_yplatform_coro_yield();
    }
}

/*-----------------------------------------------------------------------------
 * Tell the hosting yetty to forward mouse events to our stdin. yetty
 * watches for DEC private modes 1500 (button) and 1501 (move) via the
 * text-layer's libvterm hook — when both are set, mouse events on the
 * card under the cursor get re-emitted as YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE.
 *---------------------------------------------------------------------------*/
static struct yetty_ycore_void_result client_enable_mouse_forwarding(struct client_state *cs)
{
    static const char enable[] = "\033[?1500h\033[?1501h";
    /* tmux-wrap so the card-mouse enable survives a multiplexer. */
    struct yetty_ycore_buffer seq = {0};
    struct yetty_ycore_void_result wrap = yetty_ywire_tmux_wrap(enable, sizeof(enable) - 1, &seq);
    if (YETTY_IS_ERR(wrap)) {
        yetty_ycore_buffer_destroy(&seq);
        return YETTY_ERR(yetty_ycore_void, "client_enable_mouse_forwarding: wrap", wrap);
    }
    struct yetty_ycore_size_result wr =
        cs->out.base.ops->write(&cs->out.base, (const char *)seq.data, seq.size);
    yetty_ycore_buffer_destroy(&seq);
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void, "client_enable_mouse_forwarding: write", wr);
    }
    return YETTY_OK_VOID();
}

static void client_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char buf[1024];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        /* Push bytes into the wire SM. Envelopes (mouse / key / resize
         * forwarded by the hosting yetty) dispatch to their registered
         * handlers; non-envelope bytes (real keystrokes typed at the
         * tty) flow through the default sink to framework_feed_input. */
        if (cs->wire_sm) {
            yetty_ywire_wire_statemachine_feed(cs->wire_sm, buf, (size_t)n);
            struct yetty_ycore_void_result pr = yetty_ywire_wire_statemachine_process(cs->wire_sm);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
            }
        } else {
            struct yetty_ycore_void_result r =
                yetty_ygui_framework_feed_input(cs->runner->engine, buf, (size_t)n);
            if (YETTY_IS_ERR(r)) {
                yetty_ycore_error_destroy(r.error);
            }
        }
    } else if (n == 0 && !isatty(STDIN_FILENO)) {
        /* Real EOF — only treat n==0 as terminal when stdin is NOT a
         * tty. With raw mode (VMIN=0 VTIME=0) read() returns 0 routinely
         * when no data is available, not just at EOF. */
        cs->running = 0;
    }
}

static void client_pickup_winsz(struct client_state *cs)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_set_viewport(
            cs->runner->engine, (float)ws.ws_xpixel, (float)ws.ws_ypixel);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
}

static void client_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    client_pickup_winsz((struct client_state *)handle->data);
}

/* Animation pump: force a re-emit each tick so self-animating widgets
 * (ymaze / yzoo / yjungle) re-run their emit_body and ship a fresh frame to
 * the hosting yetty. */
static void client_frame_cb(uv_timer_t *handle)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (cs->runner && cs->runner->engine) {
        yetty_ygui_framework_mark_dirty(cs->runner->engine);
    }
}

static void client_prep_cb(uv_prepare_t *handle)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (yetty_ygui_framework_is_dirty(cs->runner->engine)) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_emit(cs->runner->engine);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
    if (!cs->running) {
        uv_stop(handle->loop);
    }
}

static void client_close_cb(uv_handle_t *h)
{
    (void)h;
}

static int run_client_mode(const char *name, demo_build_fn build, int enable_chrome)
{
    (void)name;
    struct client_state cs = {0};
    /* Must run BEFORE any OSC write — the very first thing the host
     * yetty might echo back is the ?1500/?1501 enable, and if STDIN
     * still has ECHO on at that moment the echo loops through libvterm. */
    runner_tty_raw();
    if (uv_loop_init(&cs.loop) != 0) {
        fprintf(stderr, "demo_runner client: uv_loop_init failed\n");
        return 1;
    }
    cs.out.base.ops = stdout_pty_ops_get();
    if (uv_pipe_init(&cs.loop, &cs.out.pipe, 0) != 0 ||
        uv_pipe_open(&cs.out.pipe, STDOUT_FILENO) != 0) {
        fprintf(stderr, "demo_runner client: stdout pipe init failed\n");
        return 1;
    }

    struct yetty_ygui_framework_ptr_result fr = yetty_ygui_framework_create(&cs.out.base);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_print(stderr, "demo_runner client: framework_create", fr.error);
        yetty_ycore_error_destroy(fr.error);
        return 1;
    }

    struct demo_runner r = {
        .name = name, .build = build, .engine = fr.value, .enable_chrome = enable_chrome};
    cs.runner = &r;
    cs.running = 1;
    yetty_ygui_framework_set_key_cb(r.engine, client_on_key, &cs);

    /* Same two-level root the standalone path uses: outer vbox owning
     * the viewport, inner body panel with the brand background and
     * column-direction layout. */
    struct yetty_ygui_object *body = NULL;
    {
        struct yetty_ygui_object_ptr_result rr =
            yetty_ygui_add(yetty_ygui_vbox_class_get().value, NULL);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: root add", rr.error);
            yetty_ycore_error_destroy(rr.error);
            return 1;
        }
        r.root = rr.value;
        struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(r.root);
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(r.root, &l);
        if (YETTY_IS_ERR(lr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: root layout", lr.error);
            yetty_ycore_error_destroy(lr.error);
            return 1;
        }
        struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(r.engine, r.root);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: set_root", sr.error);
            yetty_ycore_error_destroy(sr.error);
            return 1;
        }
        /* NOTE: in-terminal (client) mode renders into the host yetty's
         * compositor via OSC, not a local container, so the chrome caption
         * figure isn't composited here yet — the in-terminal window manager will
         * own that path. Standalone mode shows the full chrome. */
        struct yetty_ygui_object_ptr_result br =
            yetty_ygui_add(yetty_ygui_panel_class_get().value, r.root);
        if (YETTY_IS_ERR(br)) {
            yetty_ycore_error_print(stderr, "demo_runner client: body add", br.error);
            yetty_ycore_error_destroy(br.error);
            return 1;
        }
        body = br.value;
        struct yetty_ygui_layout bl = *yetty_ygui_widget_layout_get(body);
        bl.direction = YETTY_YGUI_FLEX_COLUMN;
        bl.align = YETTY_YGUI_ALIGN_STRETCH;
        bl.flex_grow = 1.0f;
        bl.padding_top = bl.padding_bottom = 24;
        bl.padding_left = bl.padding_right = 24;
        bl.gap = 12;
        bl.justify = YETTY_YGUI_JUSTIFY_CENTER;
        struct yetty_ycore_void_result blr = yetty_ygui_widget_layout_set(body, &bl);
        if (YETTY_IS_ERR(blr)) {
            yetty_ycore_error_destroy(blr.error);
        }
    }

    if (r.build) {
        struct yetty_ycore_void_result rb = r.build(&r, body);
        if (YETTY_IS_ERR(rb)) {
            yetty_ycore_error_print(stderr, "demo_runner client: build", rb.error);
            yetty_ycore_error_destroy(rb.error);
        }
    }

    /* Wire SM on stdin — parses OSC envelopes the hosting yetty pushes
     * us (mouse/key/resize), with a default sink for verbatim keystrokes. */
    cs.stdin_pty.ops = stdin_pty_ops_get();
    struct yetty_ywire_wire_statemachine_ptr_result smr =
        yetty_ywire_wire_statemachine_create(&cs.stdin_pty);
    if (YETTY_IS_ERR(smr)) {
        yetty_ycore_error_print(stderr, "demo_runner client: wire_sm_create", smr.error);
        yetty_ycore_error_destroy(smr.error);
        return 1;
    }
    cs.wire_sm = smr.value;
    {
        struct yetty_ycore_void_result rd =
            yetty_ywire_wire_statemachine_set_default(cs.wire_sm, client_default_sink, &cs);
        if (YETTY_IS_ERR(rd)) {
            yetty_ycore_error_destroy(rd.error);
        }
    }
    {
        /* MUST pass distinct userdata from set_default — the wire SM
         * dedupes handler coros by userdata pointer, so reusing &cs
         * makes both registrations share a single coro running whichever
         * fn was registered first. Pass cs.runner (a different pointer
         * value than &cs) so the mouse handler gets its own coro. */
        struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register(
            cs.wire_sm, YETTY_YWIRE_ENVELOPE_OSC, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE,
            /*has_args=*/1, client_mouse_handler, cs.runner);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
    }
    /* Tell the host yetty to forward pointer events to our stdin. */
    {
        struct yetty_ycore_void_result sr = client_enable_mouse_forwarding(&cs);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: input subscribe", sr.error);
            yetty_ycore_error_destroy(sr.error);
        }
    }

    client_pickup_winsz(&cs);
    if (uv_poll_init(&cs.loop, &cs.stdin_poll, STDIN_FILENO) == 0) {
        cs.stdin_poll.data = &cs;
        uv_poll_start(&cs.stdin_poll, UV_READABLE, client_stdin_cb);
    }
    if (uv_signal_init(&cs.loop, &cs.sigwinch) == 0) {
        cs.sigwinch.data = &cs;
        uv_signal_start(&cs.sigwinch, client_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&cs.loop, &cs.prep) == 0) {
        cs.prep.data = &cs;
        uv_prepare_start(&cs.prep, client_prep_cb);
    }
    if (uv_timer_init(&cs.loop, &cs.frame_timer) == 0) {
        cs.frame_timer.data = &cs;
        uv_timer_start(&cs.frame_timer, client_frame_cb, 33, 33);
    }

    uv_run(&cs.loop, UV_RUN_DEFAULT);

    uv_poll_stop(&cs.stdin_poll);
    uv_signal_stop(&cs.sigwinch);
    uv_prepare_stop(&cs.prep);
    uv_close((uv_handle_t *)&cs.stdin_poll, client_close_cb);
    uv_close((uv_handle_t *)&cs.sigwinch, client_close_cb);
    uv_close((uv_handle_t *)&cs.prep, client_close_cb);
    uv_close((uv_handle_t *)&cs.out.pipe, client_close_cb);
    uv_run(&cs.loop, UV_RUN_NOWAIT);

    if (cs.wire_sm) {
        struct yetty_ycore_void_result wr = yetty_ywire_wire_statemachine_destroy(cs.wire_sm);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_error_destroy(wr.error);
        }
        cs.wire_sm = NULL;
    }

    struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(r.engine);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    uv_loop_close(&cs.loop);
    runner_tty_restore();
    return 0;
}

#endif /* YETTY_YGUI_HAS_UV */

static int in_yetty_terminal(void)
{
    return yetty_running_under_yetty();
}

static int demo_runner_run_impl(int argc, char **argv, const char *name, demo_build_fn build,
                                int enable_chrome)
{
    ytrace_init();
#ifdef YETTY_YGUI_HAS_UV
    if (in_yetty_terminal()) {
        /* Client mode runs inside a host yetty pane — there's no borderless OS
         * window to drag, so chrome is irrelevant here. */
        return run_client_mode(name, build, enable_chrome);
    }
#endif
    struct demo_runner r = {.name = name, .build = build, .enable_chrome = enable_chrome};
    struct yetty_yinit_app_config cfg = {.extract_assets_fn = yetty_platform_extract_assets};
    struct yetty_ycore_int_result run_result = yetty_yinit_run(argc, argv, &cfg, worker, &r);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "demo-runner: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}

int demo_runner_run(int argc, char **argv, const char *name, demo_build_fn build)
{
    /* Window chrome is on for every demo now — they're all standalone windows
     * that benefit from a draggable/resizable titlebar. */
    return demo_runner_run_impl(argc, argv, name, build, /*enable_chrome=*/1);
}

int demo_runner_run_chrome(int argc, char **argv, const char *name, demo_build_fn build)
{
    return demo_runner_run_impl(argc, argv, name, build, /*enable_chrome=*/1);
}
