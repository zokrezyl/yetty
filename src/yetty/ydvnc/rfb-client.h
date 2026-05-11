#ifndef YETTY_YDVNC_RFB_CLIENT_H
#define YETTY_YDVNC_RFB_CLIENT_H

/*
 * Internal RFB client — connection state machine and framebuffer pixel
 * delivery. The viewer (ydvnc-viewer.c) wraps this and surfaces it as a
 * yetty_yui_view.
 */

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydvnc_rfb_client;

YETTY_YRESULT_DECLARE(yetty_ydvnc_rfb_client_ptr, struct yetty_ydvnc_rfb_client *);

typedef void (*yetty_ydvnc_on_frame_fn)(void *userdata);
typedef void (*yetty_ydvnc_on_connected_fn)(void *userdata);
typedef void (*yetty_ydvnc_on_disconnected_fn)(void *userdata, const char *reason);

struct yetty_ydvnc_rfb_client_ptr_result yetty_ydvnc_rfb_client_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_yevent_event_loop *event_loop);

/* Optional VNC-auth password. NULL means no password — the client will
 * decline servers that don't offer security type None. Up to 8 chars are
 * meaningful; longer passwords are silently truncated, per the legacy VNC
 * protocol. Must be called before yetty_ydvnc_rfb_client_connect(). */
void yetty_ydvnc_rfb_client_set_password(struct yetty_ydvnc_rfb_client *client,
                                         const char *password);

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_destroy(
    struct yetty_ydvnc_rfb_client *client);

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_connect(struct yetty_ydvnc_rfb_client *client,
                                                              const char *host, uint16_t port);

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_disconnect(
    struct yetty_ydvnc_rfb_client *client);

int yetty_ydvnc_rfb_client_is_connected(const struct yetty_ydvnc_rfb_client *client);

uint16_t yetty_ydvnc_rfb_client_width(const struct yetty_ydvnc_rfb_client *client);
uint16_t yetty_ydvnc_rfb_client_height(const struct yetty_ydvnc_rfb_client *client);

/* Render the current framebuffer texture onto the supplied render pass.
 * The pass viewport / scissor must already be set by the caller. */
struct yetty_ycore_void_result yetty_ydvnc_rfb_client_render(struct yetty_ydvnc_rfb_client *client,
                                                             WGPURenderPassEncoder pass);

/* Callbacks */
void yetty_ydvnc_rfb_client_set_on_frame(struct yetty_ydvnc_rfb_client *client,
                                         yetty_ydvnc_on_frame_fn cb, void *userdata);
void yetty_ydvnc_rfb_client_set_on_connected(struct yetty_ydvnc_rfb_client *client,
                                             yetty_ydvnc_on_connected_fn cb, void *userdata);
void yetty_ydvnc_rfb_client_set_on_disconnected(struct yetty_ydvnc_rfb_client *client,
                                                yetty_ydvnc_on_disconnected_fn cb, void *userdata);

/* Input forwarding — coordinates are in framebuffer-pixel space. */
struct yetty_ycore_void_result yetty_ydvnc_rfb_client_send_pointer(
    struct yetty_ydvnc_rfb_client *client, int16_t x, int16_t y, uint8_t button_mask);

/* RFB key events use X11 keysyms. The viewer is responsible for translating
 * yetty key/char events into keysyms via the keysym map. */
struct yetty_ycore_void_result yetty_ydvnc_rfb_client_send_key(
    struct yetty_ydvnc_rfb_client *client, uint32_t keysym, int down);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDVNC_RFB_CLIENT_H */
