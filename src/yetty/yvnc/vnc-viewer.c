/*
 * vnc-viewer.c - VNC viewer implementation as a view
 *
 * Displays frames received from a VNC server in a tile pane.
 */

#include <yetty/yvnc/vnc-viewer.h>
#include <yetty/yvnc/vnc-client.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yetty/yetty.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

/* VNC viewer structure */
struct yetty_yvnc_viewer {
    struct yetty_yui_view view; /* MUST be first - allows cast to view */
    struct yetty_yvnc_client *client;
    struct yetty_context context;
    char *host;
    uint16_t port;
};

/*=============================================================================
 * Forward declarations for view ops
 *===========================================================================*/

static struct yetty_ycore_void_result vnc_viewer_view_destroy(struct yetty_yui_view *view);
static struct yetty_ycore_void_result vnc_viewer_view_render(
    struct yetty_yui_view *view, struct yetty_ydraw_core_target *render_target);
static struct yetty_ycore_void_result vnc_viewer_view_set_bounds(struct yetty_yui_view *view,
                                                                 struct yetty_yui_rect bounds);
static struct yetty_ycore_int_result vnc_viewer_view_on_event(struct yetty_yui_view *view,
                                                              const struct yetty_yui_event *event);

static const struct yetty_yui_view_ops vnc_viewer_view_ops = {
    .destroy = vnc_viewer_view_destroy,
    .render = vnc_viewer_view_render,
    .set_bounds = vnc_viewer_view_set_bounds,
    .on_event = vnc_viewer_view_on_event,
};

/*=============================================================================
 * Callbacks
 *===========================================================================*/

static void on_frame_callback(void *userdata)
{
    struct yetty_yvnc_viewer *viewer = userdata;
    /* Request re-render when we receive a frame */
    if (viewer->context.event_loop && viewer->context.event_loop->ops->request_render) {
        viewer->context.event_loop->ops->request_render(viewer->context.event_loop);
    }
}

static void on_connected_callback(void *userdata)
{
    struct yetty_yvnc_viewer *viewer = userdata;
    yinfo("VNC viewer: connected to %s:%d", viewer->host, viewer->port);
}

static void on_disconnected_callback(void *userdata)
{
    struct yetty_yvnc_viewer *viewer = userdata;
    ywarn("VNC viewer: disconnected from %s:%d", viewer->host, viewer->port);
}

/*=============================================================================
 * View ops implementation
 *===========================================================================*/

static struct yetty_ycore_void_result vnc_viewer_view_destroy(struct yetty_yui_view *view)
{
    struct yetty_yvnc_viewer *viewer = (struct yetty_yvnc_viewer *)view;
    struct yetty_ycore_void_result inner_err = YETTY_OK_VOID();

    if (viewer->client) {
        struct yetty_ycore_void_result disc_r = yetty_yvnc_client_disconnect(viewer->client);
        if (YETTY_IS_ERR(disc_r)) {
            inner_err = disc_r;
        }
        struct yetty_ycore_void_result dest_r = yetty_yvnc_client_destroy(viewer->client);
        if (YETTY_IS_ERR(dest_r)) {
            if (YETTY_IS_OK(inner_err)) {
                inner_err = dest_r;
            } else {
                yetty_ycore_error_destroy(dest_r.error);
            }
        }
    }

    free(viewer->host);
    free(viewer);

    if (YETTY_IS_ERR(inner_err)) {
        return YETTY_ERR(yetty_ycore_void, "vnc_viewer_view_destroy failed", inner_err);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vnc_viewer_view_render(
    struct yetty_yui_view *view, struct yetty_ydraw_core_target *render_target)
{
    struct yetty_yvnc_viewer *viewer = (struct yetty_yvnc_viewer *)view;

    ydebug("vnc_viewer_render: client=%p", (void *)viewer->client);

    if (!viewer->client) {
        return YETTY_ERR(yetty_ycore_void, "no VNC client");
    }

    int is_connected = yetty_yvnc_client_is_connected(viewer->client);
    ydebug("vnc_viewer_render: is_connected=%d", is_connected);

    if (!is_connected) {
        return YETTY_OK_VOID();
    }

    /* Update texture with any received tiles */
    struct yetty_ycore_void_result res = yetty_yvnc_client_update_texture(viewer->client);
    if (!YETTY_IS_OK(res)) {
        ydebug("vnc_viewer: update_texture failed: %s", res.error.msg);
    }

    uint16_t client_w = yetty_yvnc_client_width(viewer->client);
    uint16_t client_h = yetty_yvnc_client_height(viewer->client);
    ydebug("vnc_viewer_render: client dimensions %ux%u", client_w, client_h);

    /* Get big render target's view */
    WGPUTextureView target_view = render_target->ops->get_view(render_target);
    if (!target_view) {
        return YETTY_ERR(yetty_ycore_void, "no target view");
    }

    /* Create command encoder */
    WGPUDevice device = viewer->context.gpu_context.device;
    WGPUQueue queue = viewer->context.gpu_context.queue;

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");
    }

    /* Create render pass targeting the big render target */
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = target_view;
    color_attachment.loadOp = WGPULoadOp_Load; /* Preserve existing content */
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }

    /* Set viewport to viewer bounds within the big target */
    struct yetty_yui_rect b = viewer->view.bounds;
    ydebug("vnc_viewer_render: bounds x=%.0f y=%.0f w=%.0f h=%.0f", b.x, b.y, b.w, b.h);

    wgpuRenderPassEncoderSetViewport(pass, b.x, b.y, b.w, b.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)b.x, (uint32_t)b.y, (uint32_t)b.w,
                                        (uint32_t)b.h);

    /* Render VNC client texture directly to the pass - zero copy! */
    ydebug("vnc_viewer_render: calling yetty_vnc_client_render");
    res = yetty_yvnc_client_render(viewer->client, pass, (uint32_t)b.w, (uint32_t)b.h);
    ydebug("vnc_viewer_render: client_render result ok=%d", YETTY_IS_OK(res));

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    /* Submit */
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* No ack here: the protocol layer (process_tile_data/process_rect_data)
     * already sends one ack per fully-decoded frame, carrying that frame's
     * seq. Sending another ack from the render path would either be a stale
     * duplicate (if a frame just landed) or a premature ack(0) (on the very
     * first render before any frame has been received) — both confuse the
     * server's per-client back-pressure. */

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vnc_viewer_view_set_bounds(struct yetty_yui_view *view,
                                                                 struct yetty_yui_rect bounds)
{
    struct yetty_yvnc_viewer *viewer = (struct yetty_yvnc_viewer *)view;
    viewer->view.bounds = bounds;

    /* Resize VNC if connected */
    if (viewer->client && yetty_yvnc_client_is_connected(viewer->client)) {
        struct yetty_ycore_void_result rr =
            yetty_yvnc_client_send_resize(viewer->client, (uint16_t)bounds.w, (uint16_t)bounds.h);
        if (YETTY_IS_ERR(rr)) {
            return YETTY_ERR(yetty_ycore_void, "vnc_viewer_view_set_bounds: send_resize failed",
                             rr);
        }
    }

    ydebug("vnc_viewer: set_bounds %.0fx%.0f", bounds.w, bounds.h);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result vnc_viewer_view_on_event(struct yetty_yui_view *view,
                                                              const struct yetty_yui_event *event)
{
    struct yetty_yvnc_viewer *viewer = (struct yetty_yvnc_viewer *)view;

    ydebug("vnc_viewer_view_on_event: type=%d connected=%d", (int)event->type,
           viewer->client ? yetty_yvnc_client_is_connected(viewer->client) : -1);

    if (!viewer->client || !yetty_yvnc_client_is_connected(viewer->client)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Transform coordinates relative to view bounds */
    float rel_x = 0, rel_y = 0;
    struct yetty_yui_rect b = viewer->view.bounds;

    switch (event->type) {
    case YETTY_YCORE_KEY_DOWN:
        yetty_yvnc_client_send_key_down(viewer->client, event->key.key, event->key.scancode,
                                        event->key.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_KEY_UP:
        yetty_yvnc_client_send_key_up(viewer->client, event->key.key, event->key.scancode,
                                      event->key.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_CHAR:
        yetty_yvnc_client_send_char_with_mods(viewer->client, event->chr.codepoint,
                                              (uint8_t)event->chr.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_MOUSE_MOVE:
        rel_x = event->mouse.x - b.x;
        rel_y = event->mouse.y - b.y;
        yetty_yvnc_client_send_mouse_move(viewer->client, (int16_t)rel_x, (int16_t)rel_y,
                                          (uint8_t)event->mouse.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_MOUSE_DOWN:
        rel_x = event->mouse.x - b.x;
        rel_y = event->mouse.y - b.y;
        yetty_yvnc_client_send_mouse_button(viewer->client, (int16_t)rel_x, (int16_t)rel_y,
                                            (uint8_t)event->mouse.button, 1,
                                            (uint8_t)event->mouse.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_MOUSE_UP:
        rel_x = event->mouse.x - b.x;
        rel_y = event->mouse.y - b.y;
        yetty_yvnc_client_send_mouse_button(viewer->client, (int16_t)rel_x, (int16_t)rel_y,
                                            (uint8_t)event->mouse.button, 0,
                                            (uint8_t)event->mouse.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    case YETTY_YCORE_MOUSE_SCROLL:
        rel_x = event->mouse_scroll.x - b.x;
        rel_y = event->mouse_scroll.y - b.y;
        yetty_yvnc_client_send_mouse_scroll(
            viewer->client, (int16_t)rel_x, (int16_t)rel_y, (int16_t)event->mouse_scroll.dx,
            (int16_t)event->mouse_scroll.dy, (uint8_t)event->mouse_scroll.mods);
        return YETTY_OK(yetty_ycore_int, 1);

    default:
        break;
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_vnc_viewer_ptr_result yetty_yvnc_viewer_create(const char *host, uint16_t port,
                                                            const struct yetty_context *yetty_ctx)
{
    struct yetty_yvnc_viewer *viewer;

    if (!host) {
        return YETTY_ERR(yetty_vnc_viewer_ptr, "host is NULL");
    }
    if (!yetty_ctx) {
        return YETTY_ERR(yetty_vnc_viewer_ptr, "yetty_ctx is NULL");
    }

    viewer = calloc(1, sizeof(struct yetty_yvnc_viewer));
    if (!viewer) {
        return YETTY_ERR(yetty_vnc_viewer_ptr, "allocation failed");
    }

    /* Initialize view base */
    viewer->view.ops = &vnc_viewer_view_ops;
    viewer->view.id = yetty_yui_view_next_id();

    /* Store context and connection info */
    viewer->context = *yetty_ctx;
    viewer->host = strdup(host);
    viewer->port = port;

    if (!viewer->host) {
        free(viewer);
        return YETTY_ERR(yetty_vnc_viewer_ptr, "strdup failed");
    }

    /* Create VNC client */
    WGPUDevice device = yetty_ctx->gpu_context.device;
    WGPUQueue queue = yetty_ctx->gpu_context.queue;
    WGPUTextureFormat format = yetty_ctx->gpu_context.surface_format;

    /* Use initial size - will be resized on set_bounds */
    uint16_t initial_w = 800;
    uint16_t initial_h = 600;

    struct yetty_vnc_client_ptr_result client_res = yetty_yvnc_client_create(
        device, queue, format, yetty_ctx->event_loop, initial_w, initial_h);
    if (!YETTY_IS_OK(client_res)) {
        free(viewer->host);
        free(viewer);
        return YETTY_ERR(yetty_vnc_viewer_ptr, client_res.error.msg);
    }
    viewer->client = client_res.value;

    /* Set callbacks */
    yetty_yvnc_client_set_on_frame(viewer->client, on_frame_callback, viewer);
    yetty_yvnc_client_set_on_connected(viewer->client, on_connected_callback, viewer);
    yetty_yvnc_client_set_on_disconnected(viewer->client, on_disconnected_callback, viewer);

    /* Connect to server */
    struct yetty_ycore_void_result conn_res = yetty_yvnc_client_connect(viewer->client, host, port);
    if (!YETTY_IS_OK(conn_res)) {
        yetty_yvnc_client_destroy(viewer->client);
        free(viewer->host);
        free(viewer);
        return YETTY_ERR(yetty_vnc_viewer_ptr, conn_res.error.msg);
    }

    yinfo("VNC viewer created, connecting to %s:%d", host, port);
    return YETTY_OK(yetty_vnc_viewer_ptr, viewer);
}

struct yetty_ycore_void_result yetty_yvnc_viewer_destroy(struct yetty_yvnc_viewer *viewer)
{
    if (!viewer) {
        return YETTY_ERR(yetty_ycore_void, "yetty_vnc_viewer_destroy: NULL viewer");
    }
    return vnc_viewer_view_destroy(&viewer->view);
}

struct yetty_yui_view *yetty_yvnc_viewer_as_view(struct yetty_yvnc_viewer *viewer)
{
    return viewer ? &viewer->view : NULL;
}
