/*
 * render-target-vnc.c - VNC render target implementation
 *
 * Wraps a texture render target and sends frames to VNC clients on present().
 * For mirror mode: also presents to surface.
 * For headless mode: only sends to VNC (no surface).
 */

#include <yetty/yrender/render-target.h>
#include <yetty/yvnc/vnc-server.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>

struct yetty_yrender_render_target_vnc {
    struct yetty_ydraw_target base;
    struct yetty_ydraw_target *inner; /* Texture target for actual rendering */
    struct yetty_yvnc_server *vnc_server;
    WGPUSurface surface; /* NULL for headless, non-NULL for mirror */
};

/*=============================================================================
 * Destroy
 *===========================================================================*/

static void render_target_vnc_destroy(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;

    if (rt->inner && rt->inner->ops && rt->inner->ops->destroy) {
        rt->inner->ops->destroy(rt->inner);
    }

    free(rt);
}

/*=============================================================================
 * Delegated operations
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_vnc_clear(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    return rt->inner->ops->clear(rt->inner);
}

static struct yetty_ycore_void_result render_target_vnc_render_layer(
    struct yetty_ydraw_target *self, struct yetty_yrender_terminal_layer *layer)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    /* Propagate the outer viewport (set per-pane by pane_render) into the
     * inner texture target — its render_layer reads its own viewport for
     * setViewport+setScissorRect. */
    rt->inner->viewport = self->viewport;
    return rt->inner->ops->render_layer(rt->inner, layer);
}

static struct yetty_ycore_void_result render_target_vnc_blend(
    struct yetty_ydraw_target *self, struct yetty_ydraw_target **sources, size_t count)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    return rt->inner->ops->blend(rt->inner, sources, count);
}

static WGPUTextureView render_target_vnc_get_view(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_vnc *rt =
        (const struct yetty_yrender_render_target_vnc *)self;
    return rt->inner->ops->get_view(rt->inner);
}

static WGPUTexture render_target_vnc_get_texture(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_vnc *rt =
        (const struct yetty_yrender_render_target_vnc *)self;
    return rt->inner->ops->get_texture(rt->inner);
}

static struct yetty_ycore_void_result render_target_vnc_resize(
    struct yetty_ydraw_target *self, struct yetty_yrender_viewport viewport)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    rt->base.viewport = viewport;
    return rt->inner->ops->resize(rt->inner, viewport);
}

/*=============================================================================
 * Present - send to VNC and optionally to surface
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_vnc_present(
    struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    struct yetty_ycore_void_result res;

    /* Hand the texture to the server unconditionally: the server itself
   * gates on its consumers (TCP clients and/or active recording) and
   * early-outs cheaply when nothing wants the frame. */
    if (rt->vnc_server) {
        WGPUTexture tex = rt->inner->ops->get_texture(rt->inner);
        uint32_t w = (uint32_t)rt->base.viewport.w;
        uint32_t h = (uint32_t)rt->base.viewport.h;

        ydebug("vnc_render_target_present: sending frame %ux%u tex=%p", w, h, (void *)tex);

        res = yetty_yvnc_server_send_frame_gpu(rt->vnc_server, tex, w, h);
        if (!YETTY_IS_OK(res)) {
            ywarn("vnc render target: send_frame failed: %s", res.error.msg);
        }
    }

    /* If surface provided (mirror mode), also present to window */
    if (rt->surface) {
        res = rt->inner->ops->present(rt->inner);
        if (!YETTY_IS_OK(res)) {
            return res;
        }
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * vtable and create
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_vnc_set_visual_zoom(
    struct yetty_ydraw_target *self, float scale, float off_x, float off_y)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    if (rt->inner && rt->inner->ops && rt->inner->ops->set_visual_zoom) {
        return rt->inner->ops->set_visual_zoom(rt->inner, scale, off_x, off_y);
    }
    return YETTY_OK_VOID();
}

/* Back-pressure plumbing: if the vnc server's tile-diff engine is still
 * reading back the previous frame, skip the whole render pipeline (layer
 * renders + blend) to avoid piling up GPU handles — same fd exhaustion we
 * fixed on x11-tile. The server's on_idle hook (set in
 * yetty_vnc_server_send_frame_gpu) fires a catch-up render when it drains. */
static bool render_target_vnc_is_busy(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_vnc *rt =
        (const struct yetty_yrender_render_target_vnc *)self;
    return rt->vnc_server && yetty_yvnc_server_is_busy(rt->vnc_server);
}

static void render_target_vnc_notify_render_skipped(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_vnc *rt = (struct yetty_yrender_render_target_vnc *)self;
    if (rt->vnc_server) {
        yetty_yvnc_server_mark_redraw_pending(rt->vnc_server);
    }
}

static const struct yetty_yrender_target_ops render_target_vnc_ops = {
    .destroy = render_target_vnc_destroy,
    .clear = render_target_vnc_clear,
    .render_layer = render_target_vnc_render_layer,
    .blend = render_target_vnc_blend,
    .present = render_target_vnc_present,
    .get_view = render_target_vnc_get_view,
    .get_texture = render_target_vnc_get_texture,
    .resize = render_target_vnc_resize,
    .set_visual_zoom = render_target_vnc_set_visual_zoom,
    .is_busy = render_target_vnc_is_busy,
    .notify_render_skipped = render_target_vnc_notify_render_skipped,
};

struct yetty_yrender_target_ptr_result yetty_yrender_target_vnc_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ydraw_gpu_allocator *allocator, WGPUSurface surface,
    struct yetty_yvnc_server *vnc_server, struct yetty_yrender_viewport viewport)
{
    struct yetty_yrender_render_target_vnc *rt = calloc(1, sizeof(*rt));
    if (!rt) {
        return YETTY_ERR(yetty_yrender_target_ptr, "failed to allocate vnc render target");
    }

    rt->base.ops = &render_target_vnc_ops;
    rt->base.viewport = viewport;
    rt->vnc_server = vnc_server;
    rt->surface = surface;

    /* Create inner texture target */
    struct yetty_yrender_target_ptr_result inner_res =
        yetty_yrender_target_texture_create(device, queue, format, allocator, surface, viewport);
    if (!YETTY_IS_OK(inner_res)) {
        free(rt);
        return inner_res;
    }
    rt->inner = inner_res.value;

    ydebug("yetty_yrender_target_vnc_create: %.0fx%.0f surface=%p vnc=%p", viewport.w, viewport.h,
           (void *)surface, (void *)vnc_server);
    return YETTY_OK(yetty_yrender_target_ptr, &rt->base);
}
