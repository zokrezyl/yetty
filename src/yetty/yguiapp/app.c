/*
 * yguiapp/app.c — generic ygui application host.
 *
 * `yguiapp:app` is a yapp:app subclass that owns the *running environment* for
 * a ygui app and nothing GUI-specific beyond wiring the ygui framework onto a
 * yfigure container. It generalizes the per-demo bring-up that used to live in
 * demo/ygui/runner.c so apps (and language bindings) get it for free:
 *
 *   - standalone: the shared glfw_platform brings up window + GPU + event loop
 *     and drives this app's run() override, which creates a local yfigure
 *     container, a ygui framework wired to it, a styled root/body, and (when
 *     enabled) window chrome — a draggable/resizable caption strip;
 *   - the widget tree is populated through the `build` virtual slot — an app
 *     subclasses yguiapp:app and overrides build(self, body); the default is a
 *     no-op so a bare host just shows an empty styled canvas.
 *
 * The in-terminal (TERM_PROGRAM=yetty) host and the dual-mode launcher live in
 * the sibling run.c; the shared root/body construction (yetty_yguiapp_build_root_body)
 * is declared in run.h and used by both this standalone run() and run.c's
 * terminal path.
 *
 * The container belongs to the host, not to ygui: an app that renders figures
 * straight into the container (yplot, yimage, a remote yfigure tree) can sit on
 * the same base without the ygui framework at all.
 *
 * yclass: the only hand-written file is this annotated app.c; app.gen.c is
 * #included at the foot.
 */

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/api/yapp/app.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-factory/complex-factory.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/api/yfigure/container.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/api/yscene/scene.h>
#include <yetty/ygui/ygui.h>
#include "yetty/gen/impl/ygui/framework.h"
#include "yetty/gen/impl/ygui/widget.h"
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/render-target.h>
#include <yetty/api/yshadertoy/figure.h>
#include <yetty/ytrace/ytrace.h>

/* Window chrome (drag/resize/maximize the borderless OS window) — the
 * reusable, ygui/yui-independent engine. */
#include "yetty/gen/impl/ychrome/chrome.h"
#include <yetty/yplatform/ywindow-chrome/window-chrome.h>

#include "yetty/yguiapp/run.h"

#include "input-encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Caption-strip height (logical px) for chrome-enabled standalone apps. The
 * drawn strip and the engine's drag/double-click zone share this value. */
#define YGUIAPP_CHROME_CAPTION_H 34.0f

/*===========================================================================
 * Class data slice. The host owns the whole environment; the per-instance
 * pointers below are created in run() and torn down at the end of run().
 *=========================================================================*/
struct [[clang::annotate("class@yguiapp:app")]] [[clang::annotate("parent@yapp:app")]]
yetty_yguiapp_app {
    /* ygui framework (a yclass object) and the root widget the app builds into.
     * `root` is an exposed read-only member: codegen emits the accessor
     * yetty_yguiapp_app_root_get; bindings see it as a member. */
    struct yetty_yclass_object *engine;
    [[clang::annotate("property:ro")]] struct yetty_yclass_object *root;

    /* Receiver-side compositor the framework drives. Local when standalone. */
    struct yetty_yclass_object *root_container;

    /* Environment owned for the app's lifetime. */
    struct yetty_yframework *yframework;
    struct yetty_yfigure_registry *figure_registry;
    struct yetty_ydraw_complex_factory *complex_factory;
    struct yetty_yfont_font *font;
    /* Two yscene factory-args bundles: the shared "ygrid"-token chrome
     * surface carries absolute (logical-pane) coordinates; the producer
     * kinds and the retained "yscene" kind carry figure-local content. */
    struct yetty_yscene_factory_args figure_args;
    struct yetty_yscene_factory_args local_figure_args;
    struct yetty_ydraw_target *render_target;
    struct yetty_yevent_event_listener listener;

    /* ~30 fps animation pump: fires request_render so self-dirtying widgets
     * (ymaze / yzoo / yjungle) keep re-emitting. */
    struct yetty_yevent_event_listener frame_listener;
    yetty_yevent_timer_id frame_timer;

    /* Window chrome (standalone mode only). When chrome_enabled is set the host
     * draws a caption strip and routes unclaimed mouse events through `chrome`
     * to move/resize/maximize the borderless OS window. */
    int chrome_enabled;
    struct yetty_yclass_object *chrome;        /* ychrome:chrome object, or NULL */
    struct yetty_yscene_scene *chrome_caption; /* pinned figure compositing chrome's bar */
    int chrome_last_hover;                     /* last hovered control — repaint on change */
};

/* Result wrapper + codegen accessor/downcast forward-decls (this TU does not
 * include its own generated header). */
YETTY_YRESULT_DECLARE(yetty_yguiapp_app_ptr, struct yetty_yguiapp_app *);
struct yetty_yclass_ptr_result yetty_yguiapp_app_class_get(void);
struct yetty_yguiapp_app_ptr_result yetty_yguiapp_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yguiapp_app_create(struct yetty_yclass_ctx *ctx);

/* Generated method-stub for the build virtual, called from run() below. */
struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root);

/* HiDPI factor (framebuffer_px / logical_px) for the app's window. ygui + the
 * ychrome engine both author in logical px; the ygrid receiver multiplies each
 * coordinate by this at add-record time for display. Callers of chrome that
 * naturally have framebuffer dimensions (surface size, resize event, mouse
 * event) must divide by this before passing them to chrome. 1.0f fallback keeps
 * the non-HiDPI / headless path unchanged. */
static float yguiapp_content_scale(const struct yetty_yguiapp_app *app)
{
    float s = app->yframework->gpu.app_gpu_context.content_scale;
    return s > 0.0f ? s : 1.0f;
}

/*===========================================================================
 * build — the widget-tree population hook. A ygui app subclasses yguiapp:app
 * and overrides this to add widgets under `root`. Local-only: `root` is a
 * live in-process widget object, never wire-marshalled. The default is empty.
 *=========================================================================*/
[[clang::annotate("virtual@yguiapp:app:build")]] [[clang::annotate("local@yguiapp:build")]]
static struct yetty_ycore_void_result yguiapp_default_build(struct yetty_yclass_object *app,
                                                            struct yetty_yclass_object *root)
{
    (void)app;
    (void)root;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Animation pump — fires request_render each tick so animated widgets re-run
 * their emit_body (a widget's self-set dirty flag does not survive the emit
 * pass on its own).
 *=========================================================================*/
static struct yetty_ycore_int_result yguiapp_frame_tick(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *ev)
{
    (void)ev;
    struct yetty_yguiapp_app *app =
        container_of(listener, struct yetty_yguiapp_app, frame_listener);
    if (app->engine) {
        yetty_ygui_framework_mark_dirty(app->engine);
    }
    if (app->yframework && app->yframework->event_loop &&
        app->yframework->event_loop->ops->request_render) {
        app->yframework->event_loop->ops->request_render(app->yframework->event_loop);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

static int yguiapp_on_key(struct yetty_yclass_object *engine, uint32_t key, int mods,
                          void *userdata)
{
    (void)engine;
    (void)mods;
    struct yetty_yguiapp_app *app = (struct yetty_yguiapp_app *)userdata;
    if (key == 'q' || key == 'Q' || key == 0x03 || key == 0x04) {
        if (app->yframework && app->yframework->event_loop &&
            app->yframework->event_loop->ops->stop) {
            app->yframework->event_loop->ops->stop(app->yframework->event_loop);
        }
        return 1;
    }
    return 0;
}

/* Stop the app's event loop — the clean-quit path for app subclasses that
 * install their own key handler (e.g. an Esc-to-quit demo) and so bypass the
 * default 'q'/Ctrl-C quit above. A no-op when no loop is present (headless). */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yguiapp_app_quit(struct yetty_yclass_object *obj)
{
    struct yetty_yguiapp_app_ptr_result app_res = yetty_yguiapp_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yguiapp_app_quit: from");
    struct yetty_yguiapp_app *app = app_res.value;
    if (app->yframework && app->yframework->event_loop && app->yframework->event_loop->ops->stop) {
        return app->yframework->event_loop->ops->stop(app->yframework->event_loop);
    }
    return YETTY_OK_VOID();
}

/* Re-paint the chrome caption: ask ychrome to render its titlebar+buttons into
 * a drawable list (pure ydraw, no ygui), then load that record stream into the
 * pinned scene figure that composites it. Called on create + on resize. */
static void yguiapp_chrome_caption_refresh(struct yetty_yguiapp_app *app)
{
    if (!app->chrome || !app->chrome_caption) {
        return;
    }
    struct yetty_ydraw_drawable_list_result lr = yetty_ychrome_render(app->chrome);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = lr.value;
    const uint8_t *data = (const uint8_t *)yetty_ydraw_drawable_list_data(list);
    size_t size = yetty_ydraw_drawable_list_size(list);
    struct yetty_yfigure_figure *fig = yetty_yscene_as_figure(app->chrome_caption);
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

/* Create the pinned scene figure that composites chrome's self-rendered caption
 * on top of the app content. The caption pixels come entirely from ychrome
 * (ydraw), not ygui — a scene is just the framework's drawable-list→GPU figure. */
static struct yetty_ycore_void_result yguiapp_chrome_caption_create(struct yetty_yguiapp_app *app,
                                                                    const struct yetty_context *ctx,
                                                                    float width)
{
    struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f},
                                         .max = {width, YGUIAPP_CHROME_CAPTION_H}};
    struct yetty_yscene_scene_ptr_result gr = yetty_yscene_create(rect, ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "yguiapp: chrome caption scene");
    app->chrome_caption = gr.value;
    struct yetty_yfigure_figure *fig = yetty_yscene_as_figure(app->chrome_caption);
    struct yetty_yclass_object *fobj = (struct yetty_yclass_object *)(fig)-1;
    if (app->font) {
        struct yetty_ycore_void_result fr = yetty_yscene_set_default_font(fobj, app->font);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    /* Pin to the top (no scroll) and force on top of the app content. The
     * absolute-coords flag is what makes the receiving yscene scale this
     * caption's LOGICAL px up by content_scale for display — without it the
     * strip renders at logical size in framebuffer space and the window
     * buttons land mid-strip instead of flush right. */
    struct yetty_ycore_void_result ar = yetty_yfigure_figure_absolute_coords_set(fobj, 1);
    if (YETTY_IS_ERR(ar)) {
        yetty_ycore_error_destroy(ar.error);
    }
    struct yetty_ycore_void_result zr = yetty_yfigure_figure_z_set(fobj, 100000);
    if (YETTY_IS_ERR(zr)) {
        yetty_ycore_error_destroy(zr.error);
    }
    struct yetty_ycore_void_result adr =
        yetty_yfigure_container_add_child(app->root_container, fig, 0x7FFF0001u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, adr, "yguiapp: chrome caption add_child");
    yguiapp_chrome_caption_refresh(app);
    return YETTY_OK_VOID();
}

/* Forward one event to the window-chrome engine. Returns 1 if chrome claimed
 * it (caption drag/maximize, edge resize), 0 otherwise. No-op when chrome is
 * disabled (chrome create failed / chrome_enabled clear). */
static int yguiapp_chrome_handle(struct yetty_yguiapp_app *app, const struct yetty_yui_event *ev)
{
    if (!app->chrome_enabled || !app->chrome) {
        return 0;
    }
    /* Chrome speaks LOGICAL px; the event we get is in framebuffer px. Scale
     * mouse coordinates once so chrome's hit-test aligns with its rendered
     * button footprint on HiDPI. Non-mouse events pass through untouched. */
    const struct yetty_yui_event *forwarded = ev;
    struct yetty_yui_event scaled;
    float cs = yguiapp_content_scale(app);
    if (ev && cs != 1.0f) {
        switch (ev->type) {
        case YETTY_YCORE_MOUSE_DOWN:
        case YETTY_YCORE_MOUSE_UP:
        case YETTY_YCORE_MOUSE_MOVE:
        case YETTY_YCORE_MOUSE_DRAG:
        case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
            scaled = *ev;
            scaled.mouse.x = ev->mouse.x / cs;
            scaled.mouse.y = ev->mouse.y / cs;
            forwarded = &scaled;
            break;
        default:
            break;
        }
    }
    struct yetty_ycore_int_result cr = yetty_ychrome_handle_event(app->chrome, forwarded);
    int consumed = YETTY_IS_OK(cr) && cr.value;
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_destroy(cr.error);
    }
    /* Re-paint the caption when the hovered control changes (hover highlight). */
    struct yetty_ycore_int_result hr = yetty_ychrome_hover_button(app->chrome);
    int hover = YETTY_IS_OK(hr) ? hr.value : 0;
    if (YETTY_IS_ERR(hr)) {
        yetty_ycore_error_destroy(hr.error);
    }
    if (hover != app->chrome_last_hover) {
        app->chrome_last_hover = hover;
        yguiapp_chrome_caption_refresh(app);
    }
    return consumed;
}

/*===========================================================================
 * Render / input event handler. Pumps the framework's emit into the local
 * container and routes input back into the framework. Mirrors the proven
 * demo/ygui/runner.c standalone path.
 *=========================================================================*/
static struct yetty_ycore_int_result yguiapp_event(struct yetty_yevent_event_listener *listener,
                                                   const struct yetty_yui_event *ev)
{
    struct yetty_yguiapp_app *app = container_of(listener, struct yetty_yguiapp_app, listener);
    struct yetty_yevent_event_loop *loop = app->yframework ? app->yframework->event_loop : NULL;

    if (ev->type == YETTY_YCORE_WINDOW_REFRESH) {
        if (app->render_target && app->render_target->ops->refresh_full) {
            app->render_target->ops->refresh_full(app->render_target);
        }
        struct yetty_yui_event re = {.type = YETTY_YCORE_RENDER};
        return yguiapp_event(listener, &re);
    }

    if (ev->type == YETTY_YCORE_RENDER) {
        if (!app->render_target) {
            return YETTY_OK(yetty_ycore_int, 0);
        }
        if (app->render_target->ops->is_busy &&
            app->render_target->ops->is_busy(app->render_target)) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (yetty_ygui_framework_is_dirty(app->engine)) {
            struct yetty_ycore_void_result er = yetty_ygui_framework_emit(app->engine);
            if (YETTY_IS_ERR(er)) {
                yetty_ycore_error_destroy(er.error);
            }
        }
        struct yetty_ycore_void_result cl = app->render_target->ops->clear(app->render_target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        if (app->root_container) {
            struct yetty_ycore_void_result rr =
                yetty_yfigure_render(app->root_container, app->render_target);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
            yetty_yfigure_figure_dirty_set(app->root_container, 0);
        }
        struct yetty_ycore_void_result pp = app->render_target->ops->present(app->render_target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
        /* Animation pump: a self-dirtying widget re-marks itself during emit;
         * re-arm a render whenever the framework is still dirty so animated
         * demos keep a steady frame loop. Static demos never self-dirty. */
        if (yetty_ygui_framework_is_dirty(app->engine) && loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        if (loop && loop->ops->stop) {
            loop->ops->stop(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    case YETTY_YCORE_RESIZE: {
        yetty_yframework_reconfigure_surface(app->yframework, (uint32_t)ev->resize.width,
                                             (uint32_t)ev->resize.height);
        if (app->render_target && app->render_target->ops->resize) {
            struct yetty_yrender_viewport vp = {0, 0, ev->resize.width, ev->resize.height};
            app->render_target->ops->resize(app->render_target, vp);
        }
        /* ygui + chrome both author in LOGICAL px; the receiver ygrid scales
         * back up by content_scale for display. Divide the framebuffer resize
         * once so viewport, chrome and caption rect stay in one coord space. */
        float cs = yguiapp_content_scale(app);
        float logical_w = (float)ev->resize.width / cs;
        float logical_h = (float)ev->resize.height / cs;
        struct yetty_ycore_void_result vr =
            yetty_ygui_framework_set_viewport(app->engine, logical_w, logical_h);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
        if (app->root_container) {
            /* The container rect stays in framebuffer px — that is what ygrid's
             * per-figure absolute-coord scissor scales its LOGICAL child rects
             * against on the display side. Keep the container in framebuffer
             * to match how the initial rect (from gpu->surface_width/height)
             * is authored below in run(). */
            struct yetty_ycore_rectangle rr = {
                .min = {0, 0}, .max = {(float)ev->resize.width, (float)ev->resize.height}};
            yetty_yfigure_figure_rect_set(app->root_container, rr);
            yetty_yfigure_figure_dirty_set(app->root_container, 1);
        }
        /* Keep chrome's edge bands + the caption figure tracking the window. */
        if (app->chrome_enabled && app->chrome) {
            struct yetty_ycore_void_result csz =
                yetty_ychrome_set_size(app->chrome, logical_w, logical_h);
            if (YETTY_IS_ERR(csz)) {
                yetty_ycore_error_destroy(csz.error);
            }
            if (app->chrome_caption) {
                struct yetty_yfigure_figure *fig = yetty_yscene_as_figure(app->chrome_caption);
                struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f},
                                                     .max = {logical_w, YGUIAPP_CHROME_CAPTION_H}};
                struct yetty_ycore_void_result rr =
                    yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(fig)-1, rect);
                if (YETTY_IS_ERR(rr)) {
                    yetty_ycore_error_destroy(rr.error);
                }
                yguiapp_chrome_caption_refresh(app);
            }
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_CHAR: {
        /* Printable text + Ctrl+<letter> chords: the platform already mapped the
         * physical key through the OS keyboard layout (correct case + symbols).
         * Ctrl+<letter> arrives with the CONTROL mod set — fold it to its
         * control byte (Ctrl-A → 0x01, …) so shortcuts reach widgets; otherwise
         * UTF-8 encode the codepoint. Special/navigation keys come via KEY_DOWN. */
        char buf[8];
        size_t n = 0;
        uint32_t codepoint = ev->chr.codepoint;
        if ((ev->chr.mods & 0x0002 /* GLFW_MOD_CONTROL */) &&
            ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z'))) {
            buf[0] = (char)(codepoint & 0x1F);
            n = 1;
        } else if (codepoint >= 32) {
            n = yguiapp_utf8_encode(codepoint, buf);
        }
        if (n > 0) {
            struct yetty_ycore_void_result fr =
                yetty_ygui_framework_feed_input(app->engine, buf, n);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (loop && loop->ops->request_render) {
                loop->ops->request_render(loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_KEY_DOWN: {
        char scratch[8];
        size_t n = 0;
        const char *bytes =
            yguiapp_encode_key(ev->key.key, ev->key.mods, scratch, sizeof(scratch), &n);
        if (bytes && n > 0) {
            struct yetty_ycore_void_result fr =
                yetty_ygui_framework_feed_input(app->engine, bytes, n);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (loop && loop->ops->request_render) {
                loop->ops->request_render(loop);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        /* Chrome integration model: the widget tree gets first dibs on a press,
         * so caption controls (and any widget near a resize edge) keep working.
         * Chrome only arms a window gesture when no widget consumed the press.
         * On release, chrome ends an active move/resize before the widget tree. */
        if (ev->type == YETTY_YCORE_MOUSE_DOWN) {
            struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
                app->engine, ev->mouse.x, ev->mouse.y, ev->mouse.button, 1, ev->mouse.mods);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (app->chrome_enabled && app->chrome &&
                !yetty_ygui_framework_has_pressed_widget(app->engine)) {
                yguiapp_chrome_handle(app, ev);
            }
        } else {
            if (!yguiapp_chrome_handle(app, ev)) {
                struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
                    app->engine, ev->mouse.x, ev->mouse.y, ev->mouse.button, 0, ev->mouse.mods);
                if (YETTY_IS_ERR(fr)) {
                    yetty_ycore_error_destroy(fr.error);
                }
            }
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        /* While chrome owns an active move/resize it consumes motion; otherwise
         * the widget tree gets it (hover, slider drag, …). */
        if (!yguiapp_chrome_handle(app, ev)) {
            struct yetty_ycore_int_result fr =
                yetty_ygui_framework_feed_mouse_motion(app->engine, ev->mouse.x, ev->mouse.y);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_SCROLL: {
        struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_scroll(
            app->engine, ev->mouse_scroll.x, ev->mouse_scroll.y, ev->mouse_scroll.dx,
            ev->mouse_scroll.dy);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
        /* Double-click in the caption toggles maximize. Chrome gates on the
         * caption height internally, so a double-click on body content falls
         * through to the widget tree. */
        if (yguiapp_chrome_handle(app, ev)) {
            if (loop && loop->ops->request_render) {
                loop->ops->request_render(loop);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        break;
    default:
        break;
    }
    if (loop && loop->ops->request_render) {
        loop->ops->request_render(loop);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/*===========================================================================
 * yapp:app:init — set the chrome-on default before run() reads it. run() owns
 * the rest of the bring-up.
 *=========================================================================*/
[[clang::annotate("override@yapp:app:init")]]
static struct yetty_ycore_void_result yguiapp_init(struct yetty_yclass_object *obj,
                                                   struct yetty_yclass_object *platform)
{
    (void)platform;
    struct yetty_yguiapp_app_ptr_result app_res = yetty_yguiapp_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yguiapp:init: app_from");
    /* Window chrome on by default for the standalone host (borderless windows
     * benefit from a draggable/resizable caption). A future opt-out setter can
     * clear this between init() and run(). */
    app_res.value->chrome_enabled = 1;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * yapp:app:run — full standalone bring-up + the build hook + event loop.
 *=========================================================================*/
[[clang::annotate("override@yapp:app:run")]]
static struct yetty_ycore_void_result yguiapp_run(struct yetty_yclass_object *obj,
                                                  struct yetty_yclass_object *platform)
{
    struct yetty_yguiapp_app_ptr_result app_res = yetty_yguiapp_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yguiapp:run: app_from");
    struct yetty_yguiapp_app *app = app_res.value;

    struct yetty_yplatform_gpu_context_const_ptr_result gpu_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gpu_res, "yguiapp:run: gpu_context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_res.value;

    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_pipe_res, "yguiapp:run: input_pipe");
    struct yetty_ycore_xthread_event_pipe *input_pipe = input_pipe_res.value;

    if (!gpu || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "yguiapp:run: platform state not populated");
    }

    struct yetty_yframework_ptr_result frr = yetty_yframework_create(platform);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frr, "yguiapp:run: yframework_create");
    app->yframework = frr.value;
    app->render_target = app->yframework->render_target;

    /* MSDF font for the receiver-side scene figures. */
    {
        struct yetty_yconfig_config *config = app->yframework->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *cache_dir = config->ops->get_string(config, "paths/cache", "");
        char cdb_path[768];
        char shader_path[768];
        struct yetty_ycore_void_result cdb_res = yetty_yfont_msdf_resolve_cdb(
            app->yframework->gpu.msdf_generator, fonts_dir, cache_dir, "DejaVuSansMNerdFontMono",
            "-Regular", cdb_path, sizeof(cdb_path));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cdb_res, "yguiapp:run: resolve msdf cdb");
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result fr =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yguiapp_default");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "yguiapp:run: msdf_font_create");
        app->font = fr.value;
        struct yetty_ycore_void_result load = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load, "yguiapp:run: load_basic_latin");
    }

    /* Raw figure factory + producer kinds (yplot, yimage). */
    {
        struct yetty_ydraw_complex_factory_ptr_result ffr = yetty_ydraw_complex_factory_create(
            app->yframework->gpu.device, app->yframework->gpu.queue,
            app->yframework->gpu.surface_format, app->yframework->gpu.allocator,
            app->yframework->event_loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ffr, "yguiapp:run: complex_factory_create");
        app->complex_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(app->complex_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_complex_factory_register(app->complex_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
    }

    /* Figure registry — every kind renders through the retained yscene
     * engine. The legacy kind tokens stay on the wire; only the factory
     * behind them changed. */
    {
        struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reg, "yguiapp:run: registry_create");
        app->figure_registry = reg.value;
        /* The shared "ygrid"-token chrome surface: absolute (logical-pane)
         * coordinates, scaled to framebuffer by content_scale. */
        app->figure_args.default_font = app->font;
        app->figure_args.complex_factory = app->complex_factory;
        app->figure_args.absolute_coords = 1;
        struct yetty_ycore_void_result rf = yetty_yscene_register_factory_for_kind(
            app->figure_registry, yetty_yfigure_kind_token("ygrid"), &app->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "yguiapp:run: yscene chrome factory");
        /* "yscroll" is the content kind ygui producer widgets mint;
         * their bodies emit at the absolute widget rect (logical px),
         * same as the chrome surface — one absolute bundle serves both. */
        struct yetty_ycore_void_result kr = yetty_yscene_register_factory_for_kind(
            app->figure_registry, yetty_yfigure_kind_token("yscroll"), &app->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, kr, "yguiapp:run: yscroll factory");
        /* The retained "yscene" kind (scrollarea scene mode): document-
         * space content, GPU scroll. Figure-local coordinates. */
        app->local_figure_args.default_font = app->font;
        app->local_figure_args.complex_factory = app->complex_factory;
        struct yetty_ycore_void_result sceneres =
            yetty_yscene_register_factory(app->figure_registry, &app->local_figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sceneres, "yguiapp:run: yscene_register_factory");
        /* yshadertoy has its own factory + renderer (not the yscene path). */
        struct yetty_ycore_void_result sr = yetty_yshadertoy_register_factory(app->figure_registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yguiapp:run: yshadertoy_register_factory");
    }

    /* Local container the framework drives by direct in-process dispatch. */
    struct yetty_context ctx = {.runtime = app->yframework,
                                .event_loop = app->yframework->event_loop};
    {
        struct yetty_ycore_rectangle root_rect = {
            .min = {0, 0}, .max = {(float)gpu->surface_width, (float)gpu->surface_height}};
        struct yetty_yclass_ctx yclass_ctx = {0};
        struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "yguiapp:run: container_create");
        app->root_container = obj_res.value;
        yetty_yfigure_container_set_context(app->root_container, &ctx);
        yetty_yfigure_container_set_registry(app->root_container, app->figure_registry);
        yetty_yfigure_container_set_rect(app->root_container, root_rect);
    }

    /* ygui framework wired to the local container (session NULL → local). */
    {
        struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "yguiapp:run: framework_create");
        app->engine = fr.value;
        struct yetty_ycore_void_result scr =
            yetty_ygui_framework_set_container_obj(app->engine, app->root_container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scr, "yguiapp:run: set_container_obj");
        /* ygui viewport in LOGICAL px — the ygrid receiver scales chrome coords
         * back to framebuffer for display. Without this divide the widget tree
         * is laid out for a 2× canvas on HiDPI and everything overflows. */
        float cs = yguiapp_content_scale(app);
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            app->engine, (float)gpu->surface_width / cs, (float)gpu->surface_height / cs);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
        /* Hand the framework the SAME font the scene figure renders text with
         * (font_id 0), so widget carets and click hit-tests measure against real
         * glyph advances instead of the fixed per-char fallback. Borrowed — the
         * app owns app->font and destroys it in teardown. */
        yetty_ygui_framework_set_font(app->engine, app->font);
    }

    /* Two-level styled root: outer vbox owns the viewport, inner body panel
     * fills it with the brand background, padding and a column layout. The body
     * panel is what the app subclass builds into (shared with the terminal host
     * via run.c). The chrome caption overlays the top strip, so inset the body
     * by the caption height when chrome is enabled. */
    struct yetty_yclass_object *body = NULL;
    {
        float inset = app->chrome_enabled ? YGUIAPP_CHROME_CAPTION_H : 0.0f;
        struct yetty_ycore_void_result rbr =
            yetty_yguiapp_build_root_body(app->engine, inset, &app->root, &body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rbr, "yguiapp:run: build_root_body");
    }

    /* Window-chrome engine — bind it to the borderless OS window's manager so
     * the caption strip and edges drive real move/resize/maximize. Borrowed
     * window_chrome comes from yplatform via yframework; absent in headless. A
     * failure here just leaves chrome disabled rather than aborting. */
    if (app->chrome_enabled && app->yframework->window_chrome) {
        struct yetty_ycore_void_result creg = yetty_ychrome_register();
        if (YETTY_IS_ERR(creg)) {
            yetty_ycore_error_destroy(creg.error);
        }
        struct yetty_yclass_object_ptr_result cor = yetty_ychrome_chrome_create(NULL);
        if (YETTY_IS_OK(cor)) {
            app->chrome = cor.value;
            struct yetty_ycore_void_result ccfg = yetty_ychrome_configure(
                app->chrome, app->yframework->window_chrome, YGUIAPP_CHROME_CAPTION_H,
                /*edge_size=*/8.0f, YETTY_YCHROME_FLAG_ALL);
            if (YETTY_IS_ERR(ccfg)) {
                yetty_ycore_error_destroy(ccfg.error);
            }
            /* Chrome + its pinned caption figure both live in LOGICAL px. */
            float cs = yguiapp_content_scale(app);
            struct yetty_ycore_void_result csz = yetty_ychrome_set_size(
                app->chrome, (float)gpu->surface_width / cs, (float)gpu->surface_height / cs);
            if (YETTY_IS_ERR(csz)) {
                yetty_ycore_error_destroy(csz.error);
            }
            struct yetty_ycore_void_result cap =
                yguiapp_chrome_caption_create(app, &ctx, (float)gpu->surface_width / cs);
            if (YETTY_IS_ERR(cap)) {
                ywarn("yguiapp: chrome caption create failed: %s", cap.error.msg);
                yetty_ycore_error_destroy(cap.error);
            }
        } else {
            ywarn("yguiapp: chrome create failed: %s", cor.error.msg);
            yetty_ycore_error_destroy(cor.error);
        }
    }

    yetty_ygui_framework_set_key_cb(app->engine, yguiapp_on_key, app);

    /* Hand the styled body to the app subclass to populate (virtual dispatch). */
    struct yetty_ycore_void_result br = yetty_yguiapp_build(obj, body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yguiapp:run: build");

    app->listener.handler = yguiapp_event;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(app->yframework->event_loop, &app->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "yguiapp:run: register_default_listeners");

    /* Start the ~30 fps animation pump. */
    {
        struct yetty_yevent_event_loop *loop = app->yframework->event_loop;
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        if (YETTY_IS_OK(tr)) {
            app->frame_timer = tr.value;
            app->frame_listener.handler = yguiapp_frame_tick;
            struct yetty_ycore_void_result cr = loop->ops->config_timer(loop, app->frame_timer, 33);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
            struct yetty_ycore_void_result lr =
                loop->ops->register_timer_listener(loop, app->frame_timer, &app->frame_listener);
            if (YETTY_IS_ERR(lr)) {
                yetty_ycore_error_destroy(lr.error);
            }
            struct yetty_ycore_void_result st = loop->ops->start_timer(loop, app->frame_timer);
            if (YETTY_IS_ERR(st)) {
                yetty_ycore_error_destroy(st.error);
            }
        } else {
            yetty_ycore_error_destroy(tr.error);
        }
    }

    yetty_yevent_post_async(input_pipe, &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        app->yframework->event_loop->ops->start(app->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown — reverse build order. */
    if (app->chrome) {
        struct yetty_ycore_void_result dr = yetty_ychrome_destroy(app->chrome);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        app->chrome = NULL;
    }
    if (app->engine) {
        struct yetty_ycore_void_result dr = yetty_ygui_framework_destroy(app->engine);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        app->engine = NULL;
    }
    if (app->root_container) {
        struct yetty_ycore_void_result dr = yetty_yfigure_destroy(app->root_container);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        app->root_container = NULL;
    }
    if (app->figure_registry) {
        yetty_yfigure_registry_destroy(app->figure_registry);
        app->figure_registry = NULL;
    }
    if (app->complex_factory) {
        yetty_ydraw_complex_factory_destroy(app->complex_factory);
        app->complex_factory = NULL;
    }
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }
    if (app->yframework) {
        yetty_yframework_destroy(app->yframework);
        app->yframework = NULL;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * yapp:app:quit override — stop the event loop so run() returns. The root
 * widget is reachable via the generated property:ro accessor
 * yetty_yguiapp_app_root_get (declared in the public header).
 *=========================================================================*/
[[clang::annotate("override@yapp:app:quit")]]
static struct yetty_ycore_void_result yguiapp_quit(struct yetty_yclass_object *obj)
{
    struct yetty_yguiapp_app_ptr_result app_res = yetty_yguiapp_app_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, app_res, "yguiapp:quit: app_from");
    struct yetty_yguiapp_app *app = app_res.value;
    if (app->yframework && app->yframework->event_loop && app->yframework->event_loop->ops->stop) {
        app->yframework->event_loop->ops->stop(app->yframework->event_loop);
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/yguiapp/app.c"
