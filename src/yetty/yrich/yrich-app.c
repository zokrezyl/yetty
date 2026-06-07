/*
 * yrich-app.c — standalone window host for the ygui-decorated yrich
 * editors.
 *
 * Mirrors tools/ycompositor-ygui (headless ygui path) and yaudio: a
 * window via yinit_run + yframework, a texture render target blitting to
 * the GLFW surface, an in-process yfigure container fed by a ygui
 * framework through the yclass slot path (set_container_obj — no PTY, no
 * OSC). The widget tree is the decorated editor shell for the requested
 * document kind; window input is fed straight into the framework.
 */

#include <yetty/yrich/yrich-app.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yevent/event.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/rpc.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widgets/vbox.h>
#include <yetty/ygui/widgets/yrich_view.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-shell.h>
#include <yetty/yetty/yetty.h>
#include <yetty/ytrace/ytrace.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

static inline void destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

struct yrich_app {
    int quit;
    enum yetty_yrich_app_kind kind;
    struct yetty_yrich_document *doc; /* borrowed until handed to the view */

    struct yetty_context ctx;
    struct yetty_yframework *yrt;
    struct yetty_ydraw_target *target;
    struct yetty_yfigure_container *root;
    struct yetty_yclass_object *container_obj;
    struct yetty_yfigure_registry *registry;
    struct yetty_ygui_framework *ygui;
    struct yetty_ygui_object *win; /* framework root widget */
    struct yetty_yfont_font *font;
    struct yetty_ygrid_factory_args figure_args;
    void *surface;
    uint32_t surface_w;
    uint32_t surface_h;
};

/* Build the decorated editor tree. The engine root is created and
 * registered FIRST so that figure widgets in the shell (the scrollarea)
 * resolve an engine and get wire ids when added underneath it. */
static struct yetty_ycore_void_result build_editor(struct yrich_app *app)
{
    struct yetty_ygui_object_ptr_result rootr = yetty_ygui_add(
        yetty_ygui_class_expect(yetty_ygui_vbox_class_get(), "yetty_ygui_vbox_class_get"), NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rootr, "build_editor: root add");
    app->win = rootr.value;
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(app->win);
    l.align = YETTY_YGUI_ALIGN_STRETCH;
    destroy_safe(yetty_ygui_widget_layout_set(app->win, &l));
    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(app->ygui, app->win);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "build_editor: set_root");

    struct yetty_yrich_editor editor;
    struct yetty_ycore_void_result er;
    switch (app->kind) {
    case YETTY_YRICH_APP_YSHEET:
        er = yetty_yrich_ysheet_editor_create(app->win, &editor);
        break;
    case YETTY_YRICH_APP_YSLIDE:
        er = yetty_yrich_yslide_editor_create(app->win, &editor);
        break;
    case YETTY_YRICH_APP_YDOC:
    default:
        er = yetty_yrich_ydoc_editor_create(app->win, &editor);
        break;
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "build_editor: shell create");

    /* Swap the shell's default document for the caller's (own=1 →
     * destroyed with the view), then drop our borrowed pointer. */
    struct yetty_ycore_void_result dr =
        yetty_ygui_yrich_view_set_document(editor.view, app->doc, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "build_editor: set_document");
    app->doc = NULL;
    return yetty_yrich_editor_refresh(&editor);
}

/* Sync the viewport to the surface and ship the scene into the container
 * over the in-process yclass slot path. */
static struct yetty_ycore_void_result push_scene(struct yrich_app *app)
{
    if (!app->ygui) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result vr =
        yetty_ygui_framework_set_viewport(app->ygui, (float)app->surface_w, (float)app->surface_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "push_scene: set_viewport");
    yetty_ygui_framework_mark_dirty(app->ygui);
    return yetty_ygui_framework_emit(app->ygui);
}

static struct yetty_ycore_void_result handle_event(struct yrich_app *app,
                                                   const struct yetty_yui_event *ev)
{
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return YETTY_OK_VOID();
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) {
            return YETTY_OK_VOID();
        }
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result reconf_r =
            yetty_yframework_reconfigure_surface(app->yrt, w, h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reconf_r, "yrich: reconfigure surface");
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result resize_r = app->target->ops->resize(app->target, vp);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resize_r, "yrich: target resize");
        struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(app->root);
        struct yetty_ycore_void_result rect_r = yetty_yfigure_figure_rect_set(
            (struct yetty_yclass_object *)(rf)-1, (struct yetty_ycore_rectangle){
                                                      .min = {.x = 0.0f, .y = 0.0f},
                                                      .max = {.x = (float)w, .y = (float)h},
                                                  });
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_r, "yrich: root rect");
        struct yetty_ycore_void_result dirty_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dirty_r, "yrich: root dirty");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_KEY_DOWN:
        /* Esc / 'q' quit. */
        if (ev->key.key == 256 || ev->key.key == 81) {
            app->quit = 1;
        }
        return YETTY_OK_VOID();
    case YETTY_YCORE_MOUSE_DOWN: {
        struct yetty_ycore_void_result feed_r = yetty_ygui_framework_feed_mouse_button(
            app->ygui, ev->mouse.x, ev->mouse.y, ev->mouse.button, 1, ev->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse down");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_MOUSE_UP: {
        struct yetty_ycore_void_result feed_r = yetty_ygui_framework_feed_mouse_button(
            app->ygui, ev->mouse.x, ev->mouse.y, ev->mouse.button, 0, ev->mouse.mods);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse up");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG: {
        struct yetty_ycore_void_result feed_r =
            yetty_ygui_framework_feed_mouse_motion(app->ygui, ev->mouse.x, ev->mouse.y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_r, "yrich: mouse move");
        struct yetty_ycore_void_result scene_r = push_scene(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, scene_r, "yrich: push scene");
        return YETTY_OK_VOID();
    }
    default:
        return YETTY_OK_VOID();
    }
}

static struct yetty_ycore_void_result yrich_app_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct yrich_app *app = user;

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    app->ctx.runtime = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop = app->yrt->event_loop;

    app->surface = rt->surface;
    app->surface_w = rt->surface_width;
    app->surface_h = rt->surface_height;

    /* Texture target that blits to the GLFW surface on present. */
    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0, .w = (float)app->surface_w, .h = (float)app->surface_h};
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    /* Font for the ygrid factory (text spans → glyphs). */
    {
        struct yetty_yconfig_config *config = app->yrt->config;
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = "DejaVuSansMNerdFontMono";
        char cdb_path[768];
        char shader_path[768];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 font_family);
        snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", shaders_dir);
        struct yetty_font_font_result font_result =
            yetty_yfont_msdf_font_create(cdb_path, shader_path, "yrich_app");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
        app->font = font_result.value;
        struct yetty_ycore_void_result result_224 = app->font->ops->load_basic_latin(app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_224, "font load_basic_latin failed");
    }

    /* Registry + ygrid factory + in-process root container. */
    struct yetty_yfigure_registry_ptr_result reg_r = yetty_yfigure_registry_create();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reg_r, "yfigure_registry_create failed");
    app->registry = reg_r.value;
    app->figure_args.default_font = app->font;
    app->figure_args.composite_factory = NULL;
    struct yetty_ycore_void_result result_234 =
        yetty_ygrid_register_factory(app->registry, &app->figure_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_234, "ygrid_register_factory failed");

    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h},
    };
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "root_container create failed");
    app->container_obj = obj_res.value;
    app->root = yetty_yfigure_container_from(obj_res.value);
    yetty_yfigure_container_set_context(app->root, &app->ctx);
    yetty_yfigure_container_set_registry(app->root, app->registry);
    yetty_yfigure_container_set_rect(app->root, root_rect);

    /* ygui framework → container over the in-process yclass slot path. */
    struct yetty_ygui_framework_ptr_result eng_r = yetty_ygui_framework_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, eng_r, "ygui framework alloc failed");
    app->ygui = eng_r.value;
    struct yetty_ycore_void_result result_255 =
        yetty_ygui_framework_set_container_obj(app->ygui, app->container_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_255, "framework set_container_obj failed");

    struct yetty_ycore_void_result result_259 = build_editor(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_259, "build_editor failed");
    struct yetty_ycore_void_result result_260 = push_scene(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_260, "initial push_scene failed");

    struct yetty_ycore_int_result fdr =
        rt->platform_input_pipe->ops->read_fd(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    int needs_render = 1;
    while (!app->quit) {
        struct pollfd pfd = {.fd = pipe_fd, .events = POLLIN, .revents = 0};
        int pr = poll(&pfd, 1, needs_render ? 0 : -1);
        int had_events = 0;
        if (pr > 0 && (pfd.revents & POLLIN)) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    rt->platform_input_pipe->ops->read(rt->platform_input_pipe, &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) {
                    break;
                }
                struct yetty_ycore_void_result ev_r = handle_event(app, &ev);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ev_r, "yrich worker: handle_event");
                had_events = 1;
            }
        }
        if (rt->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)rt->instance);
        }
        struct yetty_yfigure_figure *rrf = yetty_yfigure_container_as_figure(app->root);
        if (!(needs_render || had_events ||
              yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(rrf)-1).value)) {
            continue;
        }
        destroy_safe(app->target->ops->clear(app->target));
        struct yetty_ycore_void_result rr =
            yetty_yfigure_render(NULL, (struct yetty_yclass_object *)rrf - 1, app->target);
        if (YETTY_IS_ERR(rr)) {
            yerror("yrich-app: root render failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        } else {
            {
                struct yetty_ycore_void_result drop_r =
                    yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rrf)-1, 0);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r,
                                    "drop: yetty_yfigure_figure_dirty_set");
            }
        }
        destroy_safe(app->target->ops->present(app->target));
        needs_render = 0;
    }

    /* Teardown — container first so pending GPU work flushes. */
    {
        struct yetty_yfigure_figure *rrf = yetty_yfigure_container_as_figure(app->root);
        destroy_safe(yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)rrf - 1));
    }
    app->root = NULL;
    if (app->registry) {
        destroy_safe(yetty_yfigure_registry_destroy(app->registry));
        app->registry = NULL;
    }
    app->target->ops->destroy(app->target);
    app->target = NULL;
    if (app->ygui) {
        destroy_safe(yetty_ygui_framework_destroy(app->ygui));
        app->ygui = NULL;
    }
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }
    (void)yetty_yframework_destroy(app->yrt);
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yrich_app_run(int argc, char **argv,
                                                  struct yetty_yrich_document *doc,
                                                  enum yetty_yrich_app_kind kind)
{
    if (!doc) {
        return YETTY_OK(yetty_ycore_int, 2);
    }
    struct yrich_app app = {0};
    app.doc = doc;
    app.kind = kind;
    struct yetty_yinit_app_config cfg = {.extract_assets_fn = yetty_platform_extract_assets};
    return yetty_yinit_run(argc, argv, &cfg, yrich_app_worker, &app);
}
