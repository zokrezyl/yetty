/*
 * render-target-x11-tile.c - present via XShmPutImage per dirty tile.
 *
 * Wraps an inner texture render target (no WebGPU surface) and replaces the
 * present step with: submit texture to the tile-diff engine, and when the
 * readback lands, XShmPutImage each dirty tile into the X window.
 *
 * The win over the standard WebGPU swap chain only materializes on a remote
 * X display (e.g. Xvnc): X damage tracking forwards just the touched tiles
 * over the wire, matching what fast terminals like alacritty do.
 */

#include <yetty/yrender/render-target-x11-tile.h>
#include <yetty/yrender-utils/tile-diff.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ytrace/ytrace.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <string.h>

/* Must match the tile size used by the compute shader in tile-diff.c. */
#define X11_TILE_SIZE 64

struct yetty_yrender_render_target_x11_tile {
    struct yetty_ydraw_target base;

    /* Inner texture target — renders offscreen, no WGPU surface. */
    struct yetty_ydraw_target *inner;

    /* Tile-diff engine (shared with other potential sinks). */
    struct yetty_yrender_utils_tile_diff_engine *diff_engine;

    /* Event loop — used only to post a catch-up render after a dropped
     * present() (see on_engine_idle). */
    struct yetty_yevent_event_loop *event_loop;

    /* X11 state. */
    Display *display;
    Window window;
    int screen;
    Visual *visual;
    int depth;
    GC gc;
    int xshm_available;

    /* Shared-memory XImage matching the current viewport size. Recreated
     * on resize. */
    XShmSegmentInfo shm;
    XImage *shm_image;
    int shm_attached;
    uint32_t shm_width;
    uint32_t shm_height;

    uint32_t tile_size;
};

/*=============================================================================
 * XShm management
 *===========================================================================*/

/* Silent X error handler used around XShmAttach — detects "bad request" on
 * displays without proper XShm support (some remote proxies). */
static int g_shm_error = 0;
static int (*g_prev_error_handler)(Display *, XErrorEvent *) = NULL;

static int xshm_error_handler(Display *dpy, XErrorEvent *ev)
{
    (void)dpy;
    (void)ev;
    g_shm_error = 1;
    return 0;
}

static void teardown_shm_image(struct yetty_yrender_render_target_x11_tile *rt)
{
    if (rt->shm_image) {
        if (rt->shm_attached) {
            XShmDetach(rt->display, &rt->shm);
            rt->shm_attached = 0;
        }
        XDestroyImage(rt->shm_image);
        rt->shm_image = NULL;
    }
    if (rt->shm.shmaddr && rt->shm.shmaddr != (char *)-1) {
        shmdt(rt->shm.shmaddr);
        rt->shm.shmaddr = NULL;
    }
    if (rt->shm.shmid > 0) {
        shmctl(rt->shm.shmid, IPC_RMID, NULL);
        rt->shm.shmid = 0;
    }
    rt->shm_width = 0;
    rt->shm_height = 0;
}

static int setup_shm_image(struct yetty_yrender_render_target_x11_tile *rt, uint32_t width,
                           uint32_t height)
{
    if (rt->shm_width == width && rt->shm_height == height && rt->shm_image) {
        return 1;
    }

    teardown_shm_image(rt);

    rt->shm_image = XShmCreateImage(rt->display, rt->visual, (unsigned)rt->depth, ZPixmap, NULL,
                                    &rt->shm, width, height);
    if (!rt->shm_image) {
        ywarn("x11_tile: XShmCreateImage failed (%ux%u)", width, height);
        return 0;
    }

    size_t seg_size = (size_t)rt->shm_image->bytes_per_line * rt->shm_image->height;
    rt->shm.shmid = shmget(IPC_PRIVATE, seg_size, IPC_CREAT | 0600);
    if (rt->shm.shmid < 0) {
        ywarn("x11_tile: shmget(%zu) failed", seg_size);
        XDestroyImage(rt->shm_image);
        rt->shm_image = NULL;
        return 0;
    }

    rt->shm.shmaddr = shmat(rt->shm.shmid, NULL, 0);
    if (rt->shm.shmaddr == (char *)-1) {
        ywarn("x11_tile: shmat failed");
        shmctl(rt->shm.shmid, IPC_RMID, NULL);
        rt->shm.shmid = 0;
        XDestroyImage(rt->shm_image);
        rt->shm_image = NULL;
        return 0;
    }
    rt->shm_image->data = rt->shm.shmaddr;
    rt->shm.readOnly = False;

    /* Mark the segment for destruction now; it'll go away once everyone
     * detaches (us + the X server). Prevents leaks on unclean exit. */
    shmctl(rt->shm.shmid, IPC_RMID, NULL);

    g_shm_error = 0;
    g_prev_error_handler = XSetErrorHandler(xshm_error_handler);
    XShmAttach(rt->display, &rt->shm);
    XSync(rt->display, False);
    XSetErrorHandler(g_prev_error_handler);

    if (g_shm_error) {
        ywarn("x11_tile: XShmAttach rejected by server — XShm unavailable");
        shmdt(rt->shm.shmaddr);
        rt->shm.shmaddr = NULL;
        XDestroyImage(rt->shm_image);
        rt->shm_image = NULL;
        rt->xshm_available = 0;
        return 0;
    }

    rt->shm_attached = 1;
    rt->shm_width = width;
    rt->shm_height = height;
    ydebug("x11_tile: XShm image %ux%u bpl=%d depth=%d", width, height,
           rt->shm_image->bytes_per_line, rt->depth);
    return 1;
}

/*=============================================================================
 * Tile sink — receives the GPU-diff'd frame and blits dirty tiles
 *===========================================================================*/

static void x11_tile_sink(void *ctx, const struct yetty_yrender_utils_tile_diff_frame *frame)
{
    struct yetty_yrender_render_target_x11_tile *rt = ctx;

    if (!setup_shm_image(rt, frame->width, frame->height)) {
        return;
    }

    /* For each dirty tile, memcpy the pixels from the mapped GPU buffer
     * (stride = aligned_bytes_per_row) into the shm image (stride =
     * shm_image->bytes_per_line), then XShmPutImage only that tile rect.
     * Everything stays BGRA8 — matches the X11 TrueColor visual on LE x86
     * hosts where R=bits 16-23, G=8-15, B=0-7. */
    const uint32_t tile_size = frame->tile_size;
    const uint32_t bytes_per_pixel = 4;
    const uint32_t aligned_src_stride = frame->aligned_bytes_per_row;
    const int dst_stride = rt->shm_image->bytes_per_line;
    uint8_t *dst_base = (uint8_t *)rt->shm_image->data;

    uint32_t blitted = 0;
    for (uint32_t ty = 0; ty < frame->tiles_y; ty++) {
        for (uint32_t tx = 0; tx < frame->tiles_x; tx++) {
            if (!frame->dirty_bitmap[ty * frame->tiles_x + tx]) {
                continue;
            }

            uint32_t px = tx * tile_size;
            uint32_t py = ty * tile_size;
            uint32_t tw = tile_size;
            uint32_t th = tile_size;
            if (px + tw > frame->width) {
                tw = frame->width - px;
            }
            if (py + th > frame->height) {
                th = frame->height - py;
            }

            /* Copy the tile region from the aligned source into the shm
             * image at the same (px, py). */
            size_t row_bytes = (size_t)tw * bytes_per_pixel;
            for (uint32_t r = 0; r < th; r++) {
                const uint8_t *src =
                    frame->pixels + (py + r) * aligned_src_stride + px * bytes_per_pixel;
                uint8_t *dst = dst_base + (py + r) * dst_stride + px * bytes_per_pixel;
                memcpy(dst, src, row_bytes);
            }

            XShmPutImage(rt->display, rt->window, rt->gc, rt->shm_image, (int)px, (int)py, (int)px,
                         (int)py, tw, th, False);
            blitted++;
        }
    }

    /* Flush so the X server (and any downstream VNC server) processes the
     * batched tiles this frame rather than next frame. */
    XFlush(rt->display);

    ydebug("x11_tile: blitted %u/%u dirty tiles (%ux%u)", blitted, frame->dirty_count, frame->width,
           frame->height);
}

/*=============================================================================
 * Target ops — most delegate to the inner texture target
 *===========================================================================*/

static struct yetty_ycore_void_result x11_tile_clear(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    return rt->inner->ops->clear(rt->inner);
}

static struct yetty_ycore_void_result x11_tile_render_layer(
    struct yetty_ydraw_target *self, struct yetty_yrender_terminal_layer *layer)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    /* Propagate the outer viewport (set per-pane by pane_render) into the
     * inner texture target — its render_layer reads its own viewport for
     * setViewport+setScissorRect, and without this every layer would draw
     * across the inner's full-surface viewport regardless of pane bounds. */
    rt->inner->viewport = self->viewport;
    return rt->inner->ops->render_layer(rt->inner, layer);
}

static struct yetty_ycore_void_result x11_tile_blend(struct yetty_ydraw_target *self,
                                                     struct yetty_ydraw_target **sources,
                                                     size_t count)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    return rt->inner->ops->blend(rt->inner, sources, count);
}

static WGPUTextureView x11_tile_get_view(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_x11_tile *rt =
        (const struct yetty_yrender_render_target_x11_tile *)self;
    return rt->inner->ops->get_view(rt->inner);
}

static WGPUTexture x11_tile_get_texture(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_x11_tile *rt =
        (const struct yetty_yrender_render_target_x11_tile *)self;
    return rt->inner->ops->get_texture(rt->inner);
}

static struct yetty_ycore_void_result x11_tile_resize(struct yetty_ydraw_target *self,
                                                      struct yetty_yrender_viewport viewport)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    rt->base.viewport = viewport;

    /* Trigger XImage recreation on next present; the engine itself will
     * pick up the new size via its own ensure_resources. */
    teardown_shm_image(rt);

    /* Full-frame the next submit so we repaint everything into the new
     * (possibly resized) X window. */
    if (rt->diff_engine) {
        yetty_yrender_utils_tile_diff_engine_force_full(rt->diff_engine);
    }

    return rt->inner->ops->resize(rt->inner, viewport);
}

static struct yetty_ycore_void_result x11_tile_set_visual_zoom(
    struct yetty_ydraw_target *self, float scale, float off_x, float off_y)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    if (rt->inner->ops->set_visual_zoom) {
        return rt->inner->ops->set_visual_zoom(rt->inner, scale, off_x, off_y);
    }
    return YETTY_OK_VOID();
}

/* Called on window-refresh / Expose: the GPU texture hasn't changed but the
 * X window contents have been clobbered (by an overlapping window, unmap/
 * map, etc.), so we have to blit every tile even if the diff would say
 * nothing changed. */
static void x11_tile_refresh_full(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    if (rt->diff_engine) {
        yetty_yrender_utils_tile_diff_engine_force_full(rt->diff_engine);
    }
}

/* Fired by the tile-diff engine on the loop thread when a submit lands and
 * at least one subsequent submit had been dropped for back-pressure. We
 * just kick a new render event — yetty's handler will re-run the pipeline
 * and engine_submit will pick up the latest inner-texture content. */
static void on_engine_idle(void *ctx)
{
    struct yetty_yrender_render_target_x11_tile *rt = ctx;
    if (rt && rt->event_loop && rt->event_loop->ops->request_render) {
        rt->event_loop->ops->request_render(rt->event_loop);
    }
}

static bool x11_tile_is_busy(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_x11_tile *rt =
        (const struct yetty_yrender_render_target_x11_tile *)self;
    return yetty_yrender_utils_tile_diff_engine_is_busy(rt->diff_engine);
}

static void x11_tile_notify_render_skipped(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;
    yetty_yrender_utils_tile_diff_engine_mark_redraw_pending(rt->diff_engine);
}

static struct yetty_ycore_void_result x11_tile_present(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;

    if (!rt->xshm_available) {
        return YETTY_ERR(yetty_ycore_void, "x11_tile: XShm not available");
    }

    WGPUTexture tex = rt->inner->ops->get_texture(rt->inner);
    if (!tex) {
        return YETTY_ERR(yetty_ycore_void, "x11_tile: inner texture is NULL");
    }

    uint32_t w = (uint32_t)rt->base.viewport.w;
    uint32_t h = (uint32_t)rt->base.viewport.h;

    return yetty_yrender_utils_tile_diff_engine_submit(rt->diff_engine, tex, w, h, x11_tile_sink,
                                                       rt);
}

static void x11_tile_destroy(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_x11_tile *rt =
        (struct yetty_yrender_render_target_x11_tile *)self;

    teardown_shm_image(rt);

    if (rt->diff_engine) {
        yetty_yrender_utils_tile_diff_engine_destroy(rt->diff_engine);
    }

    if (rt->gc) {
        XFreeGC(rt->display, rt->gc);
    }

    if (rt->inner && rt->inner->ops && rt->inner->ops->destroy) {
        rt->inner->ops->destroy(rt->inner);
    }

    free(rt);
}

static const struct yetty_yrender_target_ops x11_tile_ops = {
    .destroy = x11_tile_destroy,
    .clear = x11_tile_clear,
    .render_layer = x11_tile_render_layer,
    .blend = x11_tile_blend,
    .present = x11_tile_present,
    .get_view = x11_tile_get_view,
    .get_texture = x11_tile_get_texture,
    .resize = x11_tile_resize,
    .set_visual_zoom = x11_tile_set_visual_zoom,
    .refresh_full = x11_tile_refresh_full,
    .is_busy = x11_tile_is_busy,
    .notify_render_skipped = x11_tile_notify_render_skipped,
};

/*=============================================================================
 * create
 *===========================================================================*/

/* Forward-declared from render-target-texture.c to avoid a circular header. */
struct yetty_yrender_target_ptr_result yetty_yrender_target_texture_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ydraw_gpu_allocator *allocator, WGPUSurface surface,
    struct yetty_yrender_viewport viewport);

struct yetty_yrender_target_ptr_result yetty_yrender_target_x11_tile_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ydraw_gpu_allocator *allocator, struct yetty_yplatform_wgpu *wgpu,
    struct yetty_yevent_event_loop *event_loop, void *x11_display, unsigned long x11_window,
    struct yetty_yrender_viewport viewport)
{
    if (!x11_display || x11_window == 0) {
        return YETTY_ERR(yetty_yrender_target_ptr, "x11_tile: display/window required");
    }

    Display *display = (Display *)x11_display;

    if (!XShmQueryExtension(display)) {
        return YETTY_ERR(yetty_yrender_target_ptr, "x11_tile: XShm extension not supported");
    }

    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, (Window)x11_window, &attrs)) {
        return YETTY_ERR(yetty_yrender_target_ptr, "x11_tile: XGetWindowAttributes failed");
    }

    /* We only support 24/32 bpp TrueColor; bail early on weird displays. */
    if (attrs.depth != 24 && attrs.depth != 32) {
        return YETTY_ERR(yetty_yrender_target_ptr, "x11_tile: unsupported X visual depth");
    }

    struct yetty_yrender_render_target_x11_tile *rt = calloc(1, sizeof(*rt));
    if (!rt) {
        return YETTY_ERR(yetty_yrender_target_ptr, "x11_tile: calloc failed");
    }

    rt->base.ops = &x11_tile_ops;
    rt->base.viewport = viewport;
    rt->display = display;
    rt->window = (Window)x11_window;
    rt->screen = DefaultScreen(display);
    rt->visual = attrs.visual;
    rt->depth = attrs.depth;
    rt->xshm_available = 1;
    rt->tile_size = X11_TILE_SIZE;
    rt->event_loop = event_loop;

    rt->gc = XCreateGC(display, rt->window, 0, NULL);
    if (!rt->gc) {
        free(rt);
        return YETTY_ERR(yetty_yrender_target_ptr, "x11_tile: XCreateGC failed");
    }

    /* Inner texture target with NO surface — we own presentation. */
    struct yetty_yrender_target_ptr_result inner_res =
        yetty_yrender_target_texture_create(device, queue, format, allocator, NULL, viewport);
    if (!YETTY_IS_OK(inner_res)) {
        XFreeGC(display, rt->gc);
        free(rt);
        return inner_res;
    }
    rt->inner = inner_res.value;

    struct yetty_yrender_utils_tile_diff_engine_ptr_result eng_res =
        yetty_yrender_utils_tile_diff_engine_create(device, queue, wgpu, X11_TILE_SIZE);
    if (!YETTY_IS_OK(eng_res)) {
        rt->inner->ops->destroy(rt->inner);
        XFreeGC(display, rt->gc);
        free(rt);
        return YETTY_ERR(yetty_yrender_target_ptr, eng_res.error.msg);
    }
    rt->diff_engine = eng_res.value;

    yetty_yrender_utils_tile_diff_engine_set_on_idle(rt->diff_engine, on_engine_idle, rt);

    yinfo("x11_tile: created %.0fx%.0f, depth=%d, tile=%u", viewport.w, viewport.h, rt->depth,
          rt->tile_size);
    return YETTY_OK(yetty_yrender_target_ptr, &rt->base);
}
