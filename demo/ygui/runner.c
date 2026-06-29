/*
 * demo/ygui/runner.c — bring-up shared by every ygui demo. Dual-mode.
 *
 *   TERM_PROGRAM=yetty  → CLIENT mode. Ships OSC envelopes over stdout
 *                         to the hosting yetty; stdin feeds keystrokes.
 *                         No own window. Backed by a libuv loop.
 *   otherwise           → STANDALONE mode. Opens its own GPU window via
 *                         the yplatform bootstrap + yframework, routes ygui through
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
#include <yetty/yapp/app.h>
#include <yetty/yclass/class.h>
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
#include <yetty/yshadertoy/figure.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
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
#include <yetty/yclass/transport-pty.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

/* Window chrome (drag/resize/maximize the borderless OS window) — the
 * reusable, ygui/yui-independent engine. This is the POC integration other
 * apps copy. */
#include <yetty/ychrome/chrome.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/widgets/button.h>
#include <yetty/ygui/widgets/label.h>
#include <yetty/yplatform/ywindow-chrome/window-chrome.h>

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

    struct yetty_yclass_object *engine;
    struct yetty_yclass_object *root;

    struct yetty_yframework *yframework;
    struct yetty_yclass_object *root_container;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_composite_factory *composite_factory;
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

/*
 * yclass app wrapper. The standalone bring-up is a yapp:app subclass: the
 * shared glfw_platform brings up the window/surface/GPU/channels and drives the
 * run override below. The heavy per-demo state stays in `struct demo_runner`
 * (the build_fn API the numbered demos depend on); the app data block just
 * embeds it. demo_runner_run creates the app, stamps name/build/enable_chrome
 * onto the embedded runner, then runs the platform sequence.
 *
 * yclass: the only hand-written file is this annotated runner.c; runner.gen.c is
 * #included at the foot.
 */
struct [[clang::annotate("class@demoygui:app")]] [[clang::annotate("parent@yapp:app")]]
yetty_demoygui_app {
    struct demo_runner runner;
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_demoygui_app_ptr, struct yetty_demoygui_app *);
struct yetty_yclass_ptr_result yetty_demoygui_app_class_get(void);
struct yetty_demoygui_app_ptr_result yetty_demoygui_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_app_create(struct yetty_yclass_ctx *ctx);

/* Platform bring-up sequence symbols (provided by the yplatform layer). The
 * demo runner owns its own entry (demo_runner_run), so it drives this sequence
 * directly rather than via the shared ymain/glfw.c. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *app,
                                                            int argc, char **argv);

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

struct yetty_yclass_object *demo_runner_engine(struct demo_runner *r)
{
    return r ? r->engine : NULL;
}

struct yetty_yclass_object *demo_runner_root(struct demo_runner *r)
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

static int on_key(struct yetty_yclass_object *engine, uint32_t key, int mods, void *userdata)
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

/* Re-paint the chrome caption: ask ychrome to render its titlebar+buttons into
 * a drawable list (pure ydraw, no ygui), then load that record stream into the
 * pinned ygrid figure that composites it. Called on create + on resize. */
static void runner_chrome_caption_refresh(struct demo_runner *r)
{
    if (!r->chrome || !r->chrome_caption) {
        return;
    }
    struct yetty_ydraw_drawable_list_result lr = yetty_ychrome_render(r->chrome);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = lr.value;
    const uint8_t *data = (const uint8_t *)yetty_ydraw_drawable_list_data(list);
    size_t size = yetty_ydraw_drawable_list_size(list);
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(r->chrome_caption);
    struct yetty_yclass_object *fobj = (struct yetty_yclass_object *)(fig)-1;
    struct yetty_ycore_void_result rc = yetty_yfigure_reset_content(fobj);
    if (YETTY_IS_ERR(rc)) {
        yetty_ycore_error_destroy(rc.error);
    }
    if (data && size > 0) {
        struct yetty_ycore_void_result pb = yetty_yfigure_process_bytes(fobj, data, size);
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
    struct yetty_ycore_int_result cr = yetty_ychrome_handle_event(r->chrome, ev);
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
            /* Direct dispatch: emit applies the figure tree straight into the
             * local root container (no wire statemachine to pump afterward). */
            struct yetty_ycore_void_result er = yetty_ygui_framework_emit(r->engine);
            if (YETTY_IS_ERR(er)) {
                yetty_ycore_error_destroy(er.error);
            }
        }
        struct yetty_ycore_void_result cl = r->render_target->ops->clear(r->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        if (r->root_container) {
            struct yetty_ycore_void_result rr =
                yetty_yfigure_render(r->root_container, r->render_target);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
            yetty_yfigure_figure_dirty_set(r->root_container, 0);
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
        /* Size the compositor's root container directly. (The legacy route
         * travelled this through the memory-pty pair so it reached the receiver
         * like a real PTY's TIOCSWINSZ; with direct in-process dispatch the
         * container is local, so we set its rect here.) */
        if (r->root_container) {
            struct yetty_ycore_rectangle rr = {
                .min = {0, 0}, .max = {(float)ev->resize.width, (float)ev->resize.height}};
            yetty_yfigure_figure_rect_set(r->root_container, rr);
            yetty_yfigure_figure_dirty_set(r->root_container, 1);
        }
        /* Keep chrome's edge bands + the caption figure tracking the window. */
        if (r->enable_chrome && r->chrome) {
            struct yetty_ycore_void_result csz = yetty_ychrome_set_size(
                r->chrome, (float)ev->resize.width, (float)ev->resize.height);
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

[[clang::annotate("override@yapp:app:init")]]
static struct yetty_ycore_void_result demoygui_app_init(struct yetty_yclass_object *obj,
                                                        struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yapp:app:run")]]
static struct yetty_ycore_void_result demoygui_app_run(struct yetty_yclass_object *obj,
                                                       struct yetty_yclass_object *platform)
{
    struct yetty_demoygui_app_ptr_result app_res = yetty_demoygui_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "demoygui:app:run: app_from");
    struct demo_runner *r = &app_res.value->runner;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "demoygui:app:run: gpu_context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "demoygui:app:run: input_pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;

    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "demoygui:app:run: platform state not populated");
    }

    struct yetty_yframework_ptr_result frr = yetty_yframework_create(platform);
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
        const char *const producer_kind_names[] = {"yplot", "yimage"};
        for (size_t i = 0; i < sizeof(producer_kind_names) / sizeof(producer_kind_names[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                r->figure_registry, yetty_yfigure_kind_token(producer_kind_names[i]),
                &r->figure_args);
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
            .min = {0, 0}, .max = {(float)gpu->surface_width, (float)gpu->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "demo_runner: container_create");
        r->root_container = obj_res.value;
        yetty_yfigure_container_set_context(r->root_container, &ctx);
        yetty_yfigure_container_set_registry(r->root_container, r->figure_registry);
        yetty_yfigure_container_set_rect(r->root_container, root_rect);
    }

    /* ygui framework, driven into the local root container by DIRECT
     * in-process yclass dispatch.
     *
     * Producer (the ygui framework) and receiver (the root figure container)
     * live in this one process on a single thread, so there is no
     * out-of-process transport: framework_create(NULL) leaves the output pty
     * unset, and set_container_obj wires the framework's typed yfigure_* stubs
     * straight at the local container object. framework_emit then applies every
     * figure-tree mutation inline via those stubs (session NULL → local
     * dispatch, zero wire).
     *
     * The deleted legacy route shipped a one-way figure-tree record stream over
     * an in-process memory-pty into a wire statemachine that decoded it into the
     * container. Direct dispatch removes the serialize/parse round and the
     * memory-pty + wire-statemachine entirely. */
    {
        struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "demo_runner: framework_create");
        r->engine = fr.value;
        struct yetty_ycore_void_result scr =
            yetty_ygui_framework_set_container_obj(r->engine, r->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scr, "demo_runner: set_container_obj");
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            r->engine, (float)gpu->surface_width, (float)gpu->surface_height);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
    }

    /* Two-level root: an outer vbox owns the viewport; an inner body
     * panel fills it with the brand background and provides padding +
     * a column layout. Demos receive the body panel as their `root`
     * argument, so they get a styled, padded canvas instead of a bare
     * window with a black void below their widgets. */
    struct yetty_yclass_object *body = NULL;
    {
        struct yetty_yclass_object_ptr_result rr =
            yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "demo_runner: root add");
        r->root = rr.value;
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(r->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "demo_runner: root layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(r->root, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "demo_runner: root layout");
        struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(r->engine, r->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "demo_runner: set_root");

        /* The chrome caption is NOT a ygui widget — it's painted by ychrome via
         * ydraw and composited as a pinned ygrid figure (runner_chrome_caption_*
         * below), so it stays independent of ygui. */

        struct yetty_yclass_object_ptr_result br =
            yetty_ygui_widget_add(r->root, yetty_ygui_panel_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "demo_runner: body add");
        body = br.value;
        /* Panel defaults to ROW direction; flip to COLUMN so demos can
         * stack widgets top-to-bottom without first reshaping the root. */
        struct yetty_ygui_layout_const_ptr_result body_layout_res =
            yetty_ygui_widget_layout_get(body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, body_layout_res, "demo_runner: body layout_get");
        struct yetty_ygui_layout bl = *body_layout_res.value;
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
     * window_chrome comes from yplatform via yframework; absent in headless. A
     * failure here just leaves chrome disabled (window stays static) rather
     * than aborting the demo. */
    if (r->enable_chrome && r->yframework->window_chrome) {
        struct yetty_ycore_void_result creg = yetty_ychrome_register();
        if (YETTY_IS_ERR(creg)) {
            yetty_ycore_error_destroy(creg.error);
        }
        struct yetty_yclass_object_ptr_result cor = yetty_ychrome_chrome_create(NULL);
        if (YETTY_IS_OK(cor)) {
            r->chrome = cor.value;
            struct yetty_ycore_void_result ccfg = yetty_ychrome_configure(
                r->chrome, r->yframework->window_chrome, DEMO_CHROME_CAPTION_H,
                /*edge_size=*/8.0f, YETTY_YCHROME_FLAG_ALL);
            if (YETTY_IS_ERR(ccfg)) {
                yetty_ycore_error_destroy(ccfg.error);
            }
            struct yetty_ycore_void_result csz = yetty_ychrome_set_size(
                r->chrome, (float)gpu->surface_width, (float)gpu->surface_height);
            if (YETTY_IS_ERR(csz)) {
                yetty_ycore_error_destroy(csz.error);
            }
            /* Composite the chrome-painted caption as a pinned top figure. */
            struct yetty_ycore_void_result cap =
                runner_chrome_caption_create(r, &ctx, (float)gpu->surface_width);
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

    /* No memory-pty wake/resize wiring: with direct in-process dispatch the
     * framework applies into the root container synchronously inside the render
     * loop's framework_emit call, and the resize event handler sizes the root
     * container directly (see YETTY_YCORE_RESIZE above). */

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

    yetty_yevent_post_async(input_pipe, &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        r->yframework->event_loop->ops->start(r->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown — mirror ygreeter's order. */
    if (r->chrome) {
        struct yetty_ycore_void_result dr = yetty_ychrome_destroy(r->chrome);
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
    if (r->root_container) {
        struct yetty_ycore_void_result dr = yetty_yfigure_destroy(r->root_container);
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
 * Client mode rides the multiplexed wire link (yetty_ywire_connection) over the
 * single PTY stream. The connection's transport (yetty_yclass_transport_pty)
 * is the sole owner of STDIN/STDOUT: it owns the terminal raw-mode (the
 * echo-loop fix — a cooked tty would echo our OSC writes back), the
 * non-blocking writer, and the fd()/pump() reactor seam. The framework's figure
 * output rides the rpc channel; forwarded mouse events arrive on the input
 * channel; raw keystrokes on the raw channel.
 *---------------------------------------------------------------------------*/
struct client_state {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    uv_timer_t frame_timer; /* keeps the loop ticking so prep drains output */
    struct yetty_yclass_transport_pty *transport; /* sole STDIN/STDOUT owner */
    struct yetty_ywire_connection *conn;          /* multiplexed link */
    struct demo_runner *runner;
    int running;
};

static int client_on_key(struct yetty_yclass_object *engine, uint32_t key, int mods,
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
 * Raw channel sink — bytes outside any envelope are real keyboard input from
 * the controlling terminal. Forward them to ygui's input decoder verbatim (it
 * understands CSI arrow sequences + ASCII). The connection's demux already
 * separates these from the OSC/DCS envelopes, so no per-handler coroutine.
 *---------------------------------------------------------------------------*/
static void client_raw_sink(void *user, const uint8_t *bytes, size_t n)
{
    struct client_state *cs = (struct client_state *)user;
    struct yetty_ycore_void_result fr =
        yetty_ygui_framework_feed_input(cs->runner->engine, (const char *)bytes, n);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
}

/*-----------------------------------------------------------------------------
 * Input channel sink — yetty forwards pointer events to the inferior as the
 * client-input OSC codes carrying a yetty_client_input_mouse. The connection
 * hands us the fully-decoded payload; dispatch to ygui's framework_feed_mouse_*
 * entry points (the same path standalone uses).
 *---------------------------------------------------------------------------*/
static void client_input_sink(void *user, int wire_code, const uint8_t *args, size_t args_len,
                              const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct client_state *cs = (struct client_state *)user;
    if (wire_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE &&
        wire_code != YETTY_OSC_SC_CLIENT_INPUT_MOUSE) {
        return;
    }
    if (payload_len < sizeof(struct yetty_client_input_mouse)) {
        return;
    }
    const struct yetty_client_input_mouse *msg = (const struct yetty_client_input_mouse *)payload;
    if (msg->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return;
    }
    switch (msg->kind) {
    case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
        struct yetty_ycore_int_result r = yetty_ygui_framework_feed_mouse_button(
            cs->runner->engine, msg->x, msg->y, msg->button, msg->pressed, 0);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        break;
    }
    case YETTY_YMGUI_INPUT_MOUSE_POS: {
        struct yetty_ycore_int_result r =
            yetty_ygui_framework_feed_mouse_motion(cs->runner->engine, msg->x, msg->y);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        break;
    }
    default:
        break;
    }
}

/*-----------------------------------------------------------------------------
 * Resize callback — the connection reads TIOCGWINSZ and hands us the geometry;
 * inject it as the framework viewport.
 *---------------------------------------------------------------------------*/
static void client_resize_cb(void *user, int width_px, int height_px, int cols, int rows)
{
    (void)cols;
    (void)rows;
    struct client_state *cs = (struct client_state *)user;
    if (width_px > 0 && height_px > 0) {
        struct yetty_ycore_void_result r = yetty_ygui_framework_set_viewport(
            cs->runner->engine, (float)width_px, (float)height_px);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
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
    struct yetty_ywire_channel *raw =
        yetty_ywire_connection_channel(cs->conn, YETTY_YWIRE_CHANNEL_RAW);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "client_enable_mouse_forwarding: no raw channel");
    }
    static const char enable[] = "\033[?1500h\033[?1501h";
    struct yetty_ycore_size_result wr = yetty_ywire_channel_write(raw, enable, sizeof(enable) - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "client_enable_mouse_forwarding: write");
    /* flush emits verbatim, tmux-wrapped so the enable survives a multiplexer. */
    return yetty_ywire_channel_flush(raw);
}

/* Drain any queued outbound bytes (non-blocking). */
static void client_pump_out(struct client_state *cs)
{
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_writable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static void client_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct client_state *cs = (struct client_state *)handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    /* The connection is the single reader: it reads the fd, demuxes, and routes
     * each lane to its channel — rpc bytes buffer for the transport adapter,
     * forwarded mouse fires the input sink, raw keystrokes fire the raw sink. */
    struct yetty_ycore_size_result r = yetty_ywire_connection_pump_readable(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
    client_pump_out(cs);
    if (yetty_ywire_connection_is_eof(cs->conn)) {
        cs->running = 0;
    }
}

static void client_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    struct client_state *cs = (struct client_state *)handle->data;
    struct yetty_ycore_void_result r = yetty_ywire_connection_pickup_winsize(cs->conn);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Animation pump: force a re-emit each tick so self-animating widgets
 * (ymaze / yzoo / yjungle) re-run their emit_body and ship a fresh frame to
 * the hosting yetty. */
static void client_frame_cb(uv_timer_t *handle)
{
    /* Deliberately does NOT force a redraw. The client renders on demand:
     * real input and state changes mark the framework dirty (feed_mouse_*,
     * key dispatch, widget callbacks), and client_prep_cb ships a frame only
     * when dirty. Forcing dirty every tick re-shipped the full ygrid body at
     * ~30 fps over the wire even when nothing changed — a constant flood whose
     * synchronous write stalls the uv loop (and input handling) for seconds
     * whenever the host falls behind. The timer is kept only to keep the loop
     * ticking so client_prep_cb runs promptly after a change. */
    (void)handle;
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
    /* Drain whatever emit queued (and any backpressured tail from earlier). */
    client_pump_out(cs);
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
    /* Cleanup state declared up front so every error path can `goto fail` and
     * the single teardown restores whatever was set up — crucially raw mode,
     * which once enabled MUST be restored (by destroying the transport) on every
     * exit. uv handles are tracked individually so teardown only touches the
     * ones that were actually initialized. */
    struct client_state cs = {0};
    struct demo_runner r = {.name = name, .build = build, .enable_chrome = enable_chrome};
    struct yetty_yclass_object *body = NULL;
    int loop_inited = 0, stdin_inited = 0, sigwinch_inited = 0, prep_inited = 0, timer_inited = 0;
    int rc = 1;
    cs.runner = &r;
    cs.running = 1;

    /* The connection's transport owns STDIN/STDOUT. Raw mode runs BEFORE any
     * OSC write — the first thing the host might echo back is the ?1500/?1501
     * enable, and a cooked tty would loop that echo through libvterm. */
    struct yetty_yclass_transport_pty_ptr_result tr =
        yetty_yclass_transport_pty_create(STDIN_FILENO, STDOUT_FILENO);
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_print(stderr, "demo_runner client: transport_pty_create", tr.error);
        yetty_ycore_error_destroy(tr.error);
        return 1; /* nothing acquired yet */
    }
    cs.transport = tr.value;

    /* Raw-mode failure is fatal: without it the cooked-tty echo loop corrupts
     * the stream. Fall through to cleanup, which restores the fd flags. */
    {
        struct yetty_ycore_void_result rr = yetty_yclass_transport_pty_enable_raw_mode(cs.transport);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: enable_raw_mode", rr.error);
            yetty_ycore_error_destroy(rr.error);
            goto fail;
        }
    }

    struct yetty_ywire_connection_ptr_result cr = yetty_ywire_connection_create(
        yetty_yclass_transport_pty_reactor(cs.transport), /*compressed=*/1);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_print(stderr, "demo_runner client: connection_create", cr.error);
        yetty_ycore_error_destroy(cr.error);
        goto fail;
    }
    cs.conn = cr.value;

    if (uv_loop_init(&cs.loop) != 0) {
        fprintf(stderr, "demo_runner client: uv_loop_init failed\n");
        goto fail;
    }
    loop_inited = 1;

    struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_print(stderr, "demo_runner client: framework_create", fr.error);
        yetty_ycore_error_destroy(fr.error);
        goto fail;
    }
    r.engine = fr.value;
    yetty_ygui_framework_set_key_cb(r.engine, client_on_key, &cs);

    /* Bind the framework's figure output to the connection's rpc channel: the
     * RPC requests ride a yetty_ywire_channel transport adapter instead of the
     * framework opening its own DCS transport on the fd. The get_root handshake
     * reads stdin synchronously here (channel_recv_blocking → the connection's
     * blocking pump) and MUST complete before the uv loop below takes the fd.
     * framework_destroy tears the session (and the adapter) down. */
    {
        struct yetty_ywire_channel *rpc_channel =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RPC);
        struct yetty_yclass_transport_ptr_result rpc_transport =
            yetty_ywire_channel_transport(rpc_channel);
        if (YETTY_IS_ERR(rpc_transport)) {
            yetty_ycore_error_print(stderr, "demo_runner client: rpc channel transport",
                                    rpc_transport.error);
            yetty_ycore_error_destroy(rpc_transport.error);
            goto fail;
        }
        struct yetty_ycore_void_result attach_res =
            yetty_ygui_framework_attach_transport(r.engine, rpc_transport.value);
        if (YETTY_IS_ERR(attach_res)) {
            yetty_ycore_error_print(stderr, "demo_runner client: framework_attach_transport",
                                    attach_res.error);
            yetty_ycore_error_destroy(attach_res.error);
            goto fail;
        }
    }

    /* Same two-level root the standalone path uses: outer vbox owning
     * the viewport, inner body panel with the brand background and
     * column-direction layout. */
    {
        struct yetty_yclass_object_ptr_result rr =
            yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: root add", rr.error);
            yetty_ycore_error_destroy(rr.error);
            goto fail;
        }
        r.root = rr.value;
        struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(r.root);
        if (YETTY_IS_ERR(layout_res)) {
            yetty_ycore_error_print(stderr, "demo_runner client: root layout_get",
                                    layout_res.error);
            yetty_ycore_error_destroy(layout_res.error);
            goto fail;
        }
        struct yetty_ygui_layout l = *layout_res.value;
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(r.root, &l);
        if (YETTY_IS_ERR(lr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: root layout", lr.error);
            yetty_ycore_error_destroy(lr.error);
            goto fail;
        }
        struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(r.engine, r.root);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_print(stderr, "demo_runner client: set_root", sr.error);
            yetty_ycore_error_destroy(sr.error);
            goto fail;
        }
        /* NOTE: in-terminal (client) mode renders into the host yetty's
         * compositor via OSC, not a local container, so the chrome caption
         * figure isn't composited here yet — the in-terminal window manager will
         * own that path. Standalone mode shows the full chrome. */
        struct yetty_yclass_object_ptr_result br =
            yetty_ygui_widget_add(r.root, yetty_ygui_panel_class_get().value);
        if (YETTY_IS_ERR(br)) {
            yetty_ycore_error_print(stderr, "demo_runner client: body add", br.error);
            yetty_ycore_error_destroy(br.error);
            goto fail;
        }
        body = br.value;
        struct yetty_ygui_layout_const_ptr_result body_layout_res =
            yetty_ygui_widget_layout_get(body);
        if (YETTY_IS_ERR(body_layout_res)) {
            yetty_ycore_error_print(stderr, "demo_runner client: body layout_get",
                                    body_layout_res.error);
            yetty_ycore_error_destroy(body_layout_res.error);
            goto fail;
        }
        struct yetty_ygui_layout bl = *body_layout_res.value;
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

    /* Route the connection's inbound lanes: raw keystrokes → the framework's
     * input decoder; forwarded mouse events → framework_feed_mouse_*; resize
     * (TIOCGWINSZ) → the framework viewport. The connection owns the demux. */
    {
        struct yetty_ywire_channel *raw =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_RAW);
        struct yetty_ywire_channel *input =
            yetty_ywire_connection_channel(cs.conn, YETTY_YWIRE_CHANNEL_INPUT);
        struct yetty_ycore_void_result rs =
            yetty_ywire_channel_set_raw_sink(raw, client_raw_sink, &cs);
        if (YETTY_IS_ERR(rs)) {
            yetty_ycore_error_destroy(rs.error);
        }
        struct yetty_ycore_void_result is =
            yetty_ywire_channel_set_envelope_sink(input, client_input_sink, &cs);
        if (YETTY_IS_ERR(is)) {
            yetty_ycore_error_destroy(is.error);
        }
    }
    {
        struct yetty_ycore_void_result resize_res =
            yetty_ywire_connection_set_resize_cb(cs.conn, client_resize_cb, &cs);
        if (YETTY_IS_ERR(resize_res)) {
            yetty_ycore_error_destroy(resize_res.error);
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

    {
        struct yetty_ycore_void_result wr = yetty_ywire_connection_pickup_winsize(cs.conn);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_error_destroy(wr.error);
        }
    }
    if (uv_poll_init(&cs.loop, &cs.stdin_poll, yetty_ywire_connection_fd(cs.conn)) == 0) {
        stdin_inited = 1;
        cs.stdin_poll.data = &cs;
        uv_poll_start(&cs.stdin_poll, UV_READABLE, client_stdin_cb);
    }
    if (uv_signal_init(&cs.loop, &cs.sigwinch) == 0) {
        sigwinch_inited = 1;
        cs.sigwinch.data = &cs;
        uv_signal_start(&cs.sigwinch, client_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&cs.loop, &cs.prep) == 0) {
        prep_inited = 1;
        cs.prep.data = &cs;
        uv_prepare_start(&cs.prep, client_prep_cb);
    }
    if (uv_timer_init(&cs.loop, &cs.frame_timer) == 0) {
        timer_inited = 1;
        cs.frame_timer.data = &cs;
        uv_timer_start(&cs.frame_timer, client_frame_cb, 33, 33);
    }

    uv_run(&cs.loop, UV_RUN_DEFAULT);
    rc = 0;

fail:
    /* Stop + close only the uv handles that were initialized, then drain their
     * close callbacks. */
    if (loop_inited) {
        if (stdin_inited) {
            uv_poll_stop(&cs.stdin_poll);
            uv_close((uv_handle_t *)&cs.stdin_poll, client_close_cb);
        }
        if (sigwinch_inited) {
            uv_signal_stop(&cs.sigwinch);
            uv_close((uv_handle_t *)&cs.sigwinch, client_close_cb);
        }
        if (prep_inited) {
            uv_prepare_stop(&cs.prep);
            uv_close((uv_handle_t *)&cs.prep, client_close_cb);
        }
        if (timer_inited) {
            uv_timer_stop(&cs.frame_timer);
            uv_close((uv_handle_t *)&cs.frame_timer, client_close_cb);
        }
        uv_run(&cs.loop, UV_RUN_NOWAIT);
    }

    /* Tear down in order: the framework first (it detaches the producer session,
     * which destroys the rpc channel transport adapter), then the connection
     * (state machine + channels), then the transport (restores raw mode). Each
     * is skipped if it was never created. */
    if (r.engine) {
        struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(r.engine);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }
    if (cs.conn) {
        struct yetty_ycore_void_result cd = yetty_ywire_connection_destroy(cs.conn);
        if (YETTY_IS_ERR(cd)) {
            yetty_ycore_error_destroy(cd.error);
        }
    }
    if (cs.transport) {
        struct yetty_ycore_void_result td = yetty_yclass_transport_pty_destroy(cs.transport);
        if (YETTY_IS_ERR(td)) {
            yetty_ycore_error_destroy(td.error);
        }
    }
    if (loop_inited) {
        uv_loop_close(&cs.loop);
    }
    return rc;
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
    /* Standalone: drive the platform bring-up sequence directly. The concrete
     * app is this runner's demoygui:app; the platform calls its init/run. */
    struct yetty_ycore_void_result platform_reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(platform_reg)) {
        yetty_ycore_error_print(stderr, "demo-runner: platform register", platform_reg.error);
        yetty_ycore_error_destroy(platform_reg.error);
        return 1;
    }
    struct yetty_ycore_void_result yapp_reg = yetty_yapp_register();
    if (YETTY_IS_ERR(yapp_reg)) {
        yetty_ycore_error_print(stderr, "demo-runner: yapp register", yapp_reg.error);
        yetty_ycore_error_destroy(yapp_reg.error);
        return 1;
    }

    struct yetty_yclass_object_ptr_result app_res = yetty_demoygui_app_create(NULL);
    if (YETTY_IS_ERR(app_res)) {
        yetty_ycore_error_print(stderr, "demo-runner: app create", app_res.error);
        yetty_ycore_error_destroy(app_res.error);
        return 1;
    }
    struct yetty_demoygui_app_ptr_result app_data = yetty_demoygui_app_from(app_res.value);
    if (YETTY_IS_ERR(app_data)) {
        yetty_ycore_error_print(stderr, "demo-runner: app data", app_data.error);
        yetty_ycore_error_destroy(app_data.error);
        return 1;
    }
    app_data.value->runner.name = name;
    app_data.value->runner.build = build;
    app_data.value->runner.enable_chrome = enable_chrome;

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_glfw_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yetty_ycore_error_print(stderr, "demo-runner: platform create", platform_res.error);
        yetty_ycore_error_destroy(platform_res.error);
        return 1;
    }

    /* Single step — run() calls init(app, argc, argv) internally. */
    struct yetty_ycore_void_result run_result =
        yetty_yplatform_platform_run(platform_res.value, app_res.value, argc, argv);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "demo-runner: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return 0;
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

#include "runner.gen.c"
