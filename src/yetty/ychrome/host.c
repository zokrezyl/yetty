/*
 * host.c — reusable window-chrome glue (see host.h). Plain C, no yclass
 * annotations: it wires a ychrome:chrome engine to a pinned ygrid figure that
 * composites the chrome's self-painted caption. Extracted from the ygui demo
 * runner so every app shares one integration instead of copy-pasting it.
 */
#include <yetty/ychrome/host.h>

#include <yetty/ychrome/chrome.h>
#include <yetty/ychrome/methods.h>
#include <yetty/ychrome/rpc.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/methods.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>

struct yetty_ychrome_host {
    struct yetty_yclass_object *chrome; /* ychrome:chrome engine */
    struct yetty_ygrid_grid *caption;   /* pinned figure compositing its bar */
    float caption_height;
};

/* The figure's yclass object sits one slot before the figure pointer (the data
 * slice follows the object header) — same accessor the runner/figure code uses. */
static struct yetty_yclass_object *caption_figure_obj(struct yetty_ychrome_host *host)
{
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(host->caption);
    return (struct yetty_yclass_object *)(fig) - 1;
}

/* Repaint: ychrome renders its caption to a drawable list (pure ydraw); load
 * that record stream into the ygrid figure. */
static void host_caption_refresh(struct yetty_ychrome_host *host)
{
    struct yetty_ydraw_drawable_list_result lr = yetty_ychrome_render(NULL, host->chrome);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = lr.value;
    const uint8_t *data = (const uint8_t *)yetty_ydraw_drawable_list_data(list);
    size_t size = yetty_ydraw_drawable_list_size(list);
    struct yetty_yclass_object *fobj = caption_figure_obj(host);
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

struct yetty_ychrome_host_ptr_result yetty_ychrome_host_create(
    struct yetty_yfigure_container *container, struct yetty_yfont_font *font,
    const struct yetty_context *ctx, struct yetty_yclass_object *window_manager, float width,
    float height, float caption_height, float edge_size, unsigned int flags)
{
    if (!container) {
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host_create: container required");
    }

    struct yetty_ycore_void_result reg = yetty_ychrome_register();
    if (YETTY_IS_ERR(reg)) {
        yetty_ycore_error_destroy(reg.error);
    }
    struct yetty_yclass_object_ptr_result chrome_r = yetty_ychrome_chrome_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ychrome_host_ptr, chrome_r, "ychrome_host_create: chrome");

    struct yetty_ychrome_host *host = calloc(1, sizeof(*host));
    if (!host) {
        struct yetty_ycore_void_result d = yetty_ychrome_destroy(NULL, chrome_r.value);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host_create: alloc");
    }
    host->chrome = chrome_r.value;
    host->caption_height = caption_height;

    struct yetty_ycore_void_result cfg = yetty_ychrome_configure(
        NULL, host->chrome, window_manager, caption_height, edge_size, flags);
    if (YETTY_IS_ERR(cfg)) {
        yetty_ycore_error_destroy(cfg.error);
    }
    struct yetty_ycore_void_result sz =
        yetty_ychrome_set_size(NULL, host->chrome, width, height);
    if (YETTY_IS_ERR(sz)) {
        yetty_ycore_error_destroy(sz.error);
    }

    struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f},
                                         .max = {width, caption_height}};
    struct yetty_ygrid_grid_ptr_result gr = yetty_ygrid_create(rect, 1, 1, ctx);
    if (YETTY_IS_ERR(gr)) {
        free(host);
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host_create: grid", gr);
    }
    host->caption = gr.value;
    if (font) {
        struct yetty_ycore_void_result fr = yetty_ygrid_set_font(host->caption, 0, font);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    struct yetty_yfigure_figure *fig = yetty_ygrid_as_figure(host->caption);
    struct yetty_yclass_object *fobj = (struct yetty_yclass_object *)(fig) - 1;
    struct yetty_ycore_void_result ar = yetty_yfigure_figure_absolute_coords_set(fobj, 1);
    if (YETTY_IS_ERR(ar)) {
        yetty_ycore_error_destroy(ar.error);
    }
    struct yetty_ycore_void_result zr = yetty_yfigure_figure_z_set(fobj, 100000);
    if (YETTY_IS_ERR(zr)) {
        yetty_ycore_error_destroy(zr.error);
    }
    struct yetty_ycore_void_result adr =
        yetty_yfigure_container_add_child(container, fig, 0x7FFF0001u);
    if (YETTY_IS_ERR(adr)) {
        free(host);
        return YETTY_ERR(yetty_ychrome_host_ptr, "ychrome_host_create: add_child", adr);
    }
    host_caption_refresh(host);
    return YETTY_OK(yetty_ychrome_host_ptr, host);
}

struct yetty_yclass_object *yetty_ychrome_host_chrome(struct yetty_ychrome_host *host)
{
    return host ? host->chrome : NULL;
}

struct yetty_ycore_void_result yetty_ychrome_host_resized(struct yetty_ychrome_host *host,
                                                          float width, float height)
{
    if (!host) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result sz =
        yetty_ychrome_set_size(NULL, host->chrome, width, height);
    if (YETTY_IS_ERR(sz)) {
        yetty_ycore_error_destroy(sz.error);
    }
    struct yetty_ycore_rectangle rect = {.min = {0.0f, 0.0f},
                                         .max = {width, host->caption_height}};
    struct yetty_ycore_void_result rr = yetty_yfigure_figure_rect_set(caption_figure_obj(host), rect);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
    }
    host_caption_refresh(host);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychrome_host_destroy(struct yetty_ychrome_host *host)
{
    if (!host) {
        return YETTY_OK_VOID();
    }
    /* The caption figure is owned by the container (added via add_child); it's
     * destroyed with the container. Only the chrome engine + host are ours. */
    if (host->chrome) {
        struct yetty_ycore_void_result d = yetty_ychrome_destroy(NULL, host->chrome);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
    }
    free(host);
    return YETTY_OK_VOID();
}
