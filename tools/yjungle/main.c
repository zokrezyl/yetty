/*
 * tools/yjungle/main.c — standalone animated-jungle app.
 *
 * Opens a window via yinit_run + yframework_create, builds a single
 * full-window ygrid figure, and renders the yetty_yjungle into it every
 * frame via the flat full-redraw path (yetty_yjungle_render). No terminal,
 * no ygui widget tree — the jungle drives the ygrid / compositor render
 * path directly. Modeled on tools/ymaze.
 *
 * Keys: q / ESC quit.
 */

#include <yetty/yinit/yinit.h>
#include <yetty/ychrome/chrome.h> /* YETTY_YCHROME_FLAG_* + yetty_ychrome_handle_event */
#include <yetty/ychrome/host.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/time.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yjungle/yjungle.h>
#include <yetty/ytrace/ytrace.h>
#include <webgpu/webgpu.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yjungle_app {
    int quit;
    struct yetty_context ctx;
    struct yetty_yframework *yrt;
    struct yetty_ydraw_target *target;
    struct yetty_yclass_object *root;
    struct yetty_ygrid_grid *grid;
    struct yetty_yjungle *jungle;
    struct yetty_ydraw_drawable_list *buf;
    double start_time;
    void *surface;
    uint32_t surface_w;
    uint32_t surface_h;
    /* Window chrome host (draggable/resizable titlebar + min/max/close). */
    struct yetty_ychrome_host *chrome;
};

#define RICH_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

static struct yetty_ycore_void_result push_buffer_to_grid(
    struct yetty_ygrid_grid *grid, const struct yetty_ydraw_drawable_list *buf)
{
    const uint8_t *data = (const uint8_t *)yetty_ydraw_drawable_list_data(buf);
    size_t total = yetty_ydraw_drawable_list_size(buf);
    size_t off = 0;
    while (off + sizeof(uint32_t) <= total) {
        const uint32_t *prim = (const uint32_t *)(data + off);
        size_t remaining = total - off;
        uint32_t type = prim[0];
        size_t sdf_bytes = yetty_ysdf_primitive_size(RICH_TYPE_BASE(type));
        size_t psize;
        if (sdf_bytes > 0) {
            psize = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        } else {
            if (remaining < 2 * sizeof(uint32_t)) {
                break;
            }
            psize = 2 * sizeof(uint32_t) + prim[1];
        }
        if (psize == 0 || psize > remaining) {
            break;
        }
        struct yetty_ycore_void_result r = yetty_ygrid_add_record_local(grid, data + off, psize);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "push_buffer_to_grid: add_record");
        off += psize;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result render_jungle(struct yjungle_app *app)
{
    if (!app->grid || !app->jungle || !app->buf) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result cr = yetty_ygrid_clear_local(app->grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "render_jungle: grid clear");

    uint64_t now_ms =
        (uint64_t)((yetty_yplatform_ytime_monotonic_sec() - app->start_time) * 1000.0);
    struct yetty_ycore_void_result rr = yetty_yjungle_render(app->jungle, app->buf, now_ms);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "render_jungle: yjungle_render");

    struct yetty_ycore_void_result pr = push_buffer_to_grid(app->grid, app->buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "render_jungle: push to grid");

    yetty_yfigure_figure_dirty_set(app->root, 1);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_figure(struct yjungle_app *app)
{
    struct yetty_ycore_rectangle rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h}};
    float w = rect.max.x - rect.min.x;
    float h = rect.max.y - rect.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    if (app->grid) {
        struct yetty_ycore_void_result rr =
            yetty_yfigure_container_remove_child_by_id(app->root, 1u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rebuild_figure: remove old");
        app->grid = NULL;
    }

    struct yetty_ygrid_grid_ptr_result gr = yetty_ygrid_create(rect, 32u, 16u, &app->ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "rebuild_figure: ygrid_create");
    app->grid = gr.value;

    (void)yetty_yjungle_set_scene_size(app->jungle, w, h);

    struct yetty_ycore_void_result ar =
        yetty_yfigure_container_add_child(app->root, yetty_ygrid_as_figure(app->grid), /*id=*/1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "rebuild_figure: add_child");

    struct yetty_ycore_void_result mr = render_jungle(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "rebuild_figure: render_jungle");

    yinfo("yjungle: figure rebuilt for %ux%u", app->surface_w, app->surface_h);
    return YETTY_OK_VOID();
}

static void handle_event(struct yjungle_app *app, const struct yetty_yui_event *ev)
{
    /* Window chrome gets first dibs on pointer events; anything it doesn't
     * claim (caption drag / edge resize / its buttons) falls through. */
    if (app->chrome && (ev->type == YETTY_YCORE_MOUSE_DOWN || ev->type == YETTY_YCORE_MOUSE_UP ||
                        ev->type == YETTY_YCORE_MOUSE_MOVE || ev->type == YETTY_YCORE_MOUSE_DRAG ||
                        ev->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK)) {
        struct yetty_ycore_int_result chrome_r = yetty_ychrome_host_handle_event(app->chrome, ev);
        int chrome_consumed = YETTY_IS_OK(chrome_r) && chrome_r.value;
        if (YETTY_IS_ERR(chrome_r)) {
            yetty_ycore_error_destroy(chrome_r.error);
        }
        if (chrome_consumed) {
            return;
        }
    }
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return;
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) {
            return;
        }
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result rr = yetty_yframework_reconfigure_surface(app->yrt, w, h);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result tr = app->target->ops->resize(app->target, vp);
        if (YETTY_IS_ERR(tr)) {
            yetty_ycore_error_destroy(tr.error);
        }
        struct yetty_ycore_void_result fr = rebuild_figure(app);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
        if (app->chrome) {
            struct yetty_ycore_void_result chrome_rz =
                yetty_ychrome_host_resized(app->chrome, (float)w, (float)h);
            if (YETTY_IS_ERR(chrome_rz)) {
                yetty_ycore_error_destroy(chrome_rz.error);
            }
        }
        return;
    }
    case YETTY_YCORE_KEY_DOWN:
        if (ev->key.key == 256 || ev->key.key == 81) {
            app->quit = 1;
        }
        return;
    default:
        return;
    }
}

static struct yetty_ycore_void_result yjungle_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct yjungle_app *app = user;

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    app->ctx.runtime = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop = app->yrt->event_loop;

    app->surface = rt->surface;
    app->surface_w = rt->surface_width;
    app->surface_h = rt->surface_height;

    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0, .w = (float)app->surface_w, .h = (float)app->surface_h};
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h}};
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res = yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "root container create failed");
    app->root = obj_res.value;
    yetty_yfigure_container_set_context(app->root, &app->ctx);
    yetty_yfigure_container_set_rect(app->root, root_rect);

    struct yetty_yjungle_config cfg = yetty_yjungle_config_default();
    struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, jr, "yjungle_create failed");
    app->jungle = jr.value;
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "drawable_list create failed");
    app->buf = br.value;
    app->start_time = yetty_yplatform_ytime_monotonic_sec();

    struct yetty_ycore_void_result fr = rebuild_figure(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "rebuild_figure (initial) failed");

    /* Window chrome: draggable/resizable titlebar + min/max/close, drawn with
     * SDF (no font needed). */
    {
        struct yetty_ychrome_host_ptr_result chrome_host_r = yetty_ychrome_host_create(
            app->root, NULL, &app->ctx, app->yrt->window_chrome, (float)app->surface_w,
            (float)app->surface_h, 34.0f, 8.0f, YETTY_YCHROME_FLAG_ALL);
        if (YETTY_IS_OK(chrome_host_r)) {
            app->chrome = chrome_host_r.value;
        } else {
            yetty_ycore_error_destroy(chrome_host_r.error);
        }
    }

    struct yetty_ycore_int_result fdr =
        rt->platform_input_pipe->ops->read_fd(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    while (!app->quit) {
        struct pollfd pfd = {.fd = pipe_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, 33);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    rt->platform_input_pipe->ops->read(rt->platform_input_pipe, &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) {
                    break;
                }
                handle_event(app, &ev);
            }
        }
        if (app->quit) {
            break;
        }
        if (rt->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)rt->instance);
        }

        struct yetty_ycore_void_result mrr = render_jungle(app);
        if (YETTY_IS_ERR(mrr)) {
            yetty_ycore_error_destroy(mrr.error);
        }

        struct yetty_ydraw_target *target = app->target;
        struct yetty_ycore_void_result cl = target->ops->clear(target);
        if (YETTY_IS_ERR(cl)) {
            yetty_ycore_error_destroy(cl.error);
        }
        struct yetty_ycore_void_result rrr = yetty_yfigure_render(app->root, target);
        if (YETTY_IS_ERR(rrr)) {
            yetty_ycore_error_destroy(rrr.error);
        } else {
            yetty_yfigure_figure_dirty_set(app->root, 0);
        }
        struct yetty_ycore_void_result pp = target->ops->present(target);
        if (YETTY_IS_ERR(pp)) {
            yetty_ycore_error_destroy(pp.error);
        }
    }

    {
        struct yetty_ycore_void_result dr = yetty_yfigure_destroy(app->root);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "root destroy");
    }
    if (app->chrome) {
        struct yetty_ycore_void_result chrome_dr = yetty_ychrome_host_destroy(app->chrome);
        if (YETTY_IS_ERR(chrome_dr)) {
            yetty_ycore_error_destroy(chrome_dr.error);
        }
        app->chrome = NULL;
    }
    app->root = NULL;
    app->grid = NULL;
    yetty_ydraw_drawable_list_destroy(app->buf);
    app->buf = NULL;
    yetty_yjungle_destroy(app->jungle);
    app->jungle = NULL;
    app->target->ops->destroy(app->target);
    app->target = NULL;
    yetty_yframework_destroy(app->yrt);
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct yjungle_app app = {0};
    struct yetty_ycore_int_result run_result =
        yetty_yinit_run(argc, argv, yjungle_worker, &app);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yjungle: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
