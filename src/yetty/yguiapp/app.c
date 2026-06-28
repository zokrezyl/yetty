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
 *     container, a ygui framework wired to it, and a root widget;
 *   - the widget tree is populated through the `build` virtual slot — an app
 *     subclasses yguiapp:app and overrides build(self, root); the default is a
 *     no-op so a bare host just shows an empty styled canvas.
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
#include <yetty/yapp/app.h>
#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>

#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yevent/dispatch.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/framework.h>
#include <yetty/ygui/widget.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/render-target.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_yfont_font *font;
    struct yetty_ygrid_factory_args figure_args;
    struct yetty_ydraw_target *render_target;
    struct yetty_yevent_event_listener listener;
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
 * Render / input event handler. Pumps the framework's emit into the local
 * container and routes input back into the framework. Mirrors the proven
 * demo/ygui/runner.c path, trimmed to the core events.
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
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            app->engine, (float)ev->resize.width, (float)ev->resize.height);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
        if (app->root_container) {
            struct yetty_ycore_rectangle rr = {
                .min = {0, 0}, .max = {(float)ev->resize.width, (float)ev->resize.height}};
            yetty_yfigure_figure_rect_set(app->root_container, rr);
            yetty_yfigure_figure_dirty_set(app->root_container, 1);
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        struct yetty_ycore_int_result fr = yetty_ygui_framework_feed_mouse_button(
            app->engine, ev->mouse.x, ev->mouse.y, ev->mouse.button,
            ev->type == YETTY_YCORE_MOUSE_DOWN ? 1 : 0, ev->mouse.mods);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_int_result fr =
            yetty_ygui_framework_feed_mouse_motion(app->engine, ev->mouse.x, ev->mouse.y);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
        if (loop && loop->ops->request_render) {
            loop->ops->request_render(loop);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    case YETTY_YCORE_MOUSE_SCROLL: {
        struct yetty_ycore_void_result fr = yetty_ygui_framework_feed_mouse_scroll(
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
    default:
        break;
    }
    if (loop && loop->ops->request_render) {
        loop->ops->request_render(loop);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/*===========================================================================
 * yapp:app:init — nothing to do up front; run() owns the bring-up.
 *=========================================================================*/
[[clang::annotate("override@yapp:app:init")]]
static struct yetty_ycore_void_result yguiapp_init(struct yetty_yclass_object *obj,
                                                   struct yetty_yclass_object *platform)
{
    (void)obj;
    (void)platform;
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

    /* MSDF font for the receiver-side ygrid. */
    {
        const char *fonts_dir =
            app->yframework->config->ops->get_string(app->yframework->config, "paths/fonts", "");
        const char *shaders_dir =
            app->yframework->config->ops->get_string(app->yframework->config, "paths/shaders", "");
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 "DejaVuSansMNerdFontMono");
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
        struct yetty_ydraw_composite_factory_ptr_result ffr = yetty_ydraw_composite_factory_create(
            app->yframework->gpu.device, app->yframework->gpu.queue,
            app->yframework->gpu.surface_format, app->yframework->gpu.allocator,
            app->yframework->event_loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ffr, "yguiapp:run: composite_factory_create");
        app->composite_factory = ffr.value;
        struct yetty_ydraw_concrete_factory *yplot_f = yetty_yplot_factory_create();
        if (yplot_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(app->composite_factory, yplot_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
        struct yetty_ydraw_concrete_factory *yimage_f = yetty_yimage_factory_create();
        if (yimage_f) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_composite_factory_register(app->composite_factory, yimage_f);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
    }

    /* Figure registry. */
    {
        struct yetty_yfigure_registry_ptr_result reg = yetty_yfigure_registry_create();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reg, "yguiapp:run: registry_create");
        app->figure_registry = reg.value;
        app->figure_args.default_font = app->font;
        app->figure_args.composite_factory = app->composite_factory;
        struct yetty_ycore_void_result rf =
            yetty_ygrid_register_factory(app->figure_registry, &app->figure_args);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rf, "yguiapp:run: ygrid_register_factory");
        const char *const producer_kind_names[] = {"yplot", "yimage"};
        for (size_t i = 0; i < sizeof(producer_kind_names) / sizeof(producer_kind_names[0]); ++i) {
            struct yetty_ycore_void_result kr = yetty_ygrid_register_factory_for_kind(
                app->figure_registry, yetty_yfigure_kind_token(producer_kind_names[i]),
                &app->figure_args);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, kr, "yguiapp:run: register_factory_for_kind");
        }
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
        struct yetty_ycore_void_result vr = yetty_ygui_framework_set_viewport(
            app->engine, (float)gpu->surface_width, (float)gpu->surface_height);
        if (YETTY_IS_ERR(vr)) {
            yetty_ycore_error_destroy(vr.error);
        }
    }

    /* Root widget — a stretching vbox the app builds into. */
    {
        struct yetty_yclass_object_ptr_result rr =
            yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yguiapp:run: root new");
        app->root = rr.value;
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(app->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "yguiapp:run: root layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.align = YETTY_YGUI_ALIGN_STRETCH;
        l.flex_grow = 1.0f;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(app->root, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "yguiapp:run: root layout");
        struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(app->engine, app->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yguiapp:run: set_root");
    }

    /* Hand the root to the app subclass to populate (virtual dispatch). */
    struct yetty_ycore_void_result br = yetty_yguiapp_build(obj, app->root);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yguiapp:run: build");

    app->listener.handler = yguiapp_event;
    struct yetty_ycore_void_result rel =
        yetty_yevent_register_default_listeners(app->yframework->event_loop, &app->listener);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rel, "yguiapp:run: register_default_listeners");

    yetty_yevent_post_async(input_pipe, &(struct yetty_yui_event){.type = YETTY_YCORE_RENDER});

    struct yetty_ycore_void_result run_res =
        app->yframework->event_loop->ops->start(app->yframework->event_loop);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_destroy(run_res.error);
    }

    /* Teardown — reverse build order. */
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
    if (app->composite_factory) {
        yetty_ydraw_composite_factory_destroy(app->composite_factory);
        app->composite_factory = NULL;
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

#include "app.gen.c"
