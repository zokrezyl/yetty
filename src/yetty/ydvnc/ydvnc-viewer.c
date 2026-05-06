/*
 * ydvnc-viewer.c — wraps the RFB client as a yetty view, plugged into a tile
 * pane. Mirrors the shape of yvnc/vnc-viewer.c (yetty's own protocol viewer)
 * intentionally so first-pane wiring follows the same pattern.
 *
 * Input handling for layout-respecting Dvorak: see keysyms.h for the rule —
 * KEY_DOWN/KEY_UP for non-printables, CHAR for printables, modifiers tracked
 * separately.
 */

#include <yetty/ydvnc/ydvnc-viewer.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>
#include <yetty/ycore/event.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yui/view.h>

#include "keysyms.h"
#include "rfb-client.h"

/* RFB pointer button bits (RFC 6143 §7.5.5). */
#define BTN_BIT_LEFT    0x01u
#define BTN_BIT_MIDDLE  0x02u
#define BTN_BIT_RIGHT   0x04u
#define BTN_BIT_WHEEL_UP    0x08u
#define BTN_BIT_WHEEL_DOWN  0x10u
#define BTN_BIT_WHEEL_LEFT  0x20u
#define BTN_BIT_WHEEL_RIGHT 0x40u

struct yetty_ydvnc_viewer {
    struct yetty_yterm_view view; /* MUST be first — view-cast assumes this. */
    struct yetty_ydvnc_rfb_client *client;
    struct yetty_context context;
    char *host;
    uint16_t port;

    /* Latched pointer button state — RFB sends a button mask on every
     * PointerEvent, so we maintain it across events. */
    uint8_t button_mask;
    int16_t last_x, last_y;
};

/*=============================================================================
 * Coordinate mapping: yetty pane bounds → RFB framebuffer pixels
 *===========================================================================*/

static void map_xy(const struct yetty_ydvnc_viewer *v, float screen_x, float screen_y,
                   int16_t *fb_x, int16_t *fb_y)
{
    struct yetty_yui_rect b = v->view.bounds;
    if (b.w <= 0 || b.h <= 0) {
        *fb_x = 0;
        *fb_y = 0;
        return;
    }
    uint16_t fw = yetty_ydvnc_rfb_client_width(v->client);
    uint16_t fh = yetty_ydvnc_rfb_client_height(v->client);
    if (fw == 0 || fh == 0) {
        *fb_x = 0;
        *fb_y = 0;
        return;
    }
    float rx = (screen_x - b.x) / b.w;
    float ry = (screen_y - b.y) / b.h;
    if (rx < 0.0f) rx = 0.0f;
    if (rx > 1.0f) rx = 1.0f;
    if (ry < 0.0f) ry = 0.0f;
    if (ry > 1.0f) ry = 1.0f;
    *fb_x = (int16_t)(rx * (float)fw);
    *fb_y = (int16_t)(ry * (float)fh);
}

/*=============================================================================
 * RFB client callbacks
 *===========================================================================*/

static void on_frame(void *userdata)
{
    struct yetty_ydvnc_viewer *v = userdata;
    if (v->context.event_loop && v->context.event_loop->ops->request_render) {
        v->context.event_loop->ops->request_render(v->context.event_loop);
    }
}

static void on_connected(void *userdata)
{
    struct yetty_ydvnc_viewer *v = userdata;
    yinfo("ydvnc viewer: connected to %s:%u", v->host, v->port);
}

static void on_disconnected(void *userdata, const char *reason)
{
    struct yetty_ydvnc_viewer *v = userdata;
    ywarn("ydvnc viewer: disconnected from %s:%u (%s)", v->host, v->port,
          reason ? reason : "unknown");
}

/*=============================================================================
 * View ops
 *===========================================================================*/

static struct yetty_ycore_void_result viewer_destroy(struct yetty_yterm_view *view)
{
    struct yetty_ydvnc_viewer *v = (struct yetty_ydvnc_viewer *)view;
    struct yetty_ycore_void_result inner = YETTY_OK_VOID();

    if (v->client) {
        struct yetty_ycore_void_result d = yetty_ydvnc_rfb_client_disconnect(v->client);
        if (YETTY_IS_ERR(d)) {
            inner = d;
        }
        struct yetty_ycore_void_result dd = yetty_ydvnc_rfb_client_destroy(v->client);
        if (YETTY_IS_ERR(dd)) {
            if (YETTY_IS_OK(inner)) {
                inner = dd;
            } else {
                yetty_ycore_error_destroy(dd.error);
            }
        }
    }

    free(v->host);
    free(v);

    if (YETTY_IS_ERR(inner)) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc viewer destroy failed", inner);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result viewer_render(struct yetty_yterm_view *view,
                                                    struct yetty_ypaint_core_target *render_target)
{
    struct yetty_ydvnc_viewer *v = (struct yetty_ydvnc_viewer *)view;

    if (!v->client || !yetty_ydvnc_rfb_client_is_connected(v->client)) {
        return YETTY_OK_VOID();
    }

    WGPUTextureView target_view = render_target->ops->get_view(render_target);
    if (!target_view) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc viewer: no target view");
    }

    WGPUDevice device = v->context.gpu_context.device;
    WGPUQueue queue = v->context.gpu_context.queue;

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
    if (!enc) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc viewer: createCommandEncoder failed");
    }

    WGPURenderPassColorAttachment ca = {0};
    ca.view = target_view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pd = {0};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pd);
    if (!pass) {
        wgpuCommandEncoderRelease(enc);
        return YETTY_ERR(yetty_ycore_void, "ydvnc viewer: beginRenderPass failed");
    }

    struct yetty_yui_rect b = v->view.bounds;
    wgpuRenderPassEncoderSetViewport(pass, b.x, b.y, b.w, b.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)b.x, (uint32_t)b.y, (uint32_t)b.w,
                                        (uint32_t)b.h);

    struct yetty_ycore_void_result r = yetty_ydvnc_rfb_client_render(v->client, pass);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cd = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cd);
    wgpuQueueSubmit(queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    return r;
}

static struct yetty_ycore_void_result viewer_set_bounds(struct yetty_yterm_view *view,
                                                        struct yetty_yui_rect bounds)
{
    struct yetty_ydvnc_viewer *v = (struct yetty_ydvnc_viewer *)view;
    v->view.bounds = bounds;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Input → RFB messages
 *
 * The Dvorak rule (see keysyms.h):
 *   - Non-printable / modifier keys: send keysym from GLFW key code on
 *     KEY_DOWN/KEY_UP (look up via yetty_ydvnc_keysym_from_glfw_key).
 *   - Printable chars: ignore KEY_DOWN/KEY_UP (those carry the QWERTY
 *     position, not the user's layout-resolved character). Use CHAR events
 *     instead and emit keysym down+up from the codepoint.
 *===========================================================================*/

static int is_printable_glfw_key(int k)
{
    /* GLFW reports printable keys with the ASCII-style codes 32..96.
     * Modifier keys, F-keys, etc. are >= 256 and handled via the keysym
     * table on KEY_DOWN/KEY_UP. */
    return k >= 32 && k <= 96;
}

static struct yetty_ycore_int_result viewer_on_event(struct yetty_yterm_view *view,
                                                     const struct yetty_yui_event *event)
{
    struct yetty_ydvnc_viewer *v = (struct yetty_ydvnc_viewer *)view;

    if (!v->client || !yetty_ydvnc_rfb_client_is_connected(v->client)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    int16_t fb_x = v->last_x, fb_y = v->last_y;

    switch (event->type) {
    case YETTY_YCORE_KEY_DOWN: {
        if (is_printable_glfw_key(event->key.key)) {
            return YETTY_OK(yetty_ycore_int, 1); /* wait for CHAR */
        }
        uint32_t ks = yetty_ydvnc_keysym_from_glfw_key(event->key.key);
        if (ks) {
            struct yetty_ycore_void_result r = yetty_ydvnc_rfb_client_send_key(v->client, ks, 1);
            if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_KEY_UP: {
        if (is_printable_glfw_key(event->key.key)) {
            /* Printable releases handled by the CHAR press+release pair. */
            return YETTY_OK(yetty_ycore_int, 1);
        }
        uint32_t ks = yetty_ydvnc_keysym_from_glfw_key(event->key.key);
        if (ks) {
            struct yetty_ycore_void_result r = yetty_ydvnc_rfb_client_send_key(v->client, ks, 0);
            if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_CHAR: {
        uint32_t ks = yetty_ydvnc_keysym_from_codepoint(event->chr.codepoint);
        if (ks) {
            struct yetty_ycore_void_result d = yetty_ydvnc_rfb_client_send_key(v->client, ks, 1);
            if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
            struct yetty_ycore_void_result u = yetty_ydvnc_rfb_client_send_key(v->client, ks, 0);
            if (YETTY_IS_ERR(u)) yetty_ycore_error_destroy(u.error);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_MOVE: {
        map_xy(v, event->mouse.x, event->mouse.y, &fb_x, &fb_y);
        v->last_x = fb_x;
        v->last_y = fb_y;
        struct yetty_ycore_void_result r =
            yetty_ydvnc_rfb_client_send_pointer(v->client, fb_x, fb_y, v->button_mask);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP: {
        map_xy(v, event->mouse.x, event->mouse.y, &fb_x, &fb_y);
        v->last_x = fb_x;
        v->last_y = fb_y;
        uint8_t bit = 0;
        switch (event->mouse.button) {
        case 0: bit = BTN_BIT_LEFT; break;
        case 1: bit = BTN_BIT_RIGHT; break;
        case 2: bit = BTN_BIT_MIDDLE; break;
        default: bit = 0; break;
        }
        if (event->type == YETTY_YCORE_MOUSE_DOWN) {
            v->button_mask |= bit;
        } else {
            v->button_mask &= (uint8_t)~bit;
        }
        struct yetty_ycore_void_result r =
            yetty_ydvnc_rfb_client_send_pointer(v->client, fb_x, fb_y, v->button_mask);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        return YETTY_OK(yetty_ycore_int, 1);
    }

    case YETTY_YCORE_MOUSE_SCROLL: {
        map_xy(v, event->mouse_scroll.x, event->mouse_scroll.y, &fb_x, &fb_y);
        v->last_x = fb_x;
        v->last_y = fb_y;
        /* Wheel events: press the scroll button, then immediately release. */
        uint8_t wheel = 0;
        if (event->mouse_scroll.dy > 0) wheel = BTN_BIT_WHEEL_UP;
        else if (event->mouse_scroll.dy < 0) wheel = BTN_BIT_WHEEL_DOWN;
        else if (event->mouse_scroll.dx > 0) wheel = BTN_BIT_WHEEL_RIGHT;
        else if (event->mouse_scroll.dx < 0) wheel = BTN_BIT_WHEEL_LEFT;
        if (wheel) {
            uint8_t pressed = (uint8_t)(v->button_mask | wheel);
            struct yetty_ycore_void_result r1 =
                yetty_ydvnc_rfb_client_send_pointer(v->client, fb_x, fb_y, pressed);
            if (YETTY_IS_ERR(r1)) yetty_ycore_error_destroy(r1.error);
            struct yetty_ycore_void_result r2 =
                yetty_ydvnc_rfb_client_send_pointer(v->client, fb_x, fb_y, v->button_mask);
            if (YETTY_IS_ERR(r2)) yetty_ycore_error_destroy(r2.error);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    default:
        break;
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* Wrap viewer_on_event in the int_result-returning view-op signature.
 * Some yetty modules use void-result; here we use int (1=handled, 0=not). */
static struct yetty_ycore_int_result viewer_on_event_op(struct yetty_yterm_view *view,
                                                        const struct yetty_yui_event *event)
{
    return viewer_on_event(view, event);
}

static const struct yetty_yui_view_ops VIEWER_OPS = {
    .destroy = viewer_destroy,
    .render = viewer_render,
    .set_bounds = viewer_set_bounds,
    .on_event = viewer_on_event_op,
};

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ydvnc_viewer_ptr_result yetty_ydvnc_viewer_create(
    const char *host, uint16_t port, const struct yetty_context *yetty_ctx)
{
    if (!host || !yetty_ctx) {
        return YETTY_ERR(yetty_ydvnc_viewer_ptr, "ydvnc: NULL host or context");
    }
    struct yetty_ydvnc_viewer *v = calloc(1, sizeof(*v));
    if (!v) {
        return YETTY_ERR(yetty_ydvnc_viewer_ptr, "ydvnc: alloc failed");
    }

    v->view.ops = &VIEWER_OPS;
    v->view.id = yetty_yui_view_next_id();
    v->context = *yetty_ctx;
    v->host = strdup(host);
    v->port = port;
    if (!v->host) {
        free(v);
        return YETTY_ERR(yetty_ydvnc_viewer_ptr, "ydvnc: strdup failed");
    }

    struct yetty_ydvnc_rfb_client_ptr_result cres = yetty_ydvnc_rfb_client_create(
        yetty_ctx->gpu_context.device, yetty_ctx->gpu_context.queue,
        yetty_ctx->gpu_context.surface_format, yetty_ctx->event_loop);
    if (YETTY_IS_ERR(cres)) {
        free(v->host);
        free(v);
        return YETTY_ERR(yetty_ydvnc_viewer_ptr, "ydvnc: rfb client create failed", cres);
    }
    v->client = cres.value;

    yetty_ydvnc_rfb_client_set_on_frame(v->client, on_frame, v);
    yetty_ydvnc_rfb_client_set_on_connected(v->client, on_connected, v);
    yetty_ydvnc_rfb_client_set_on_disconnected(v->client, on_disconnected, v);

    struct yetty_ycore_void_result conn = yetty_ydvnc_rfb_client_connect(v->client, host, port);
    if (YETTY_IS_ERR(conn)) {
        struct yetty_ycore_void_result d = yetty_ydvnc_rfb_client_destroy(v->client);
        if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
        free(v->host);
        free(v);
        return YETTY_ERR(yetty_ydvnc_viewer_ptr, "ydvnc: connect failed", conn);
    }

    yinfo("ydvnc viewer created, connecting to %s:%u", host, port);
    return YETTY_OK(yetty_ydvnc_viewer_ptr, v);
}

struct yetty_ycore_void_result yetty_ydvnc_viewer_destroy(struct yetty_ydvnc_viewer *v)
{
    if (!v) {
        return YETTY_OK_VOID();
    }
    return viewer_destroy(&v->view);
}

struct yetty_yterm_view *yetty_ydvnc_viewer_as_view(struct yetty_ydvnc_viewer *v)
{
    return v ? &v->view : NULL;
}
