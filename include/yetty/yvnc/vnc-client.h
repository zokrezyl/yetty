#ifndef YETTY_YVNC_VNC_CLIENT_H
#define YETTY_YVNC_VNC_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvnc_client;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_vnc_client_ptr, struct yetty_yvnc_client *);

/* Connection stats */
struct yetty_yvnc_client_stats {
    double fps;
    double tps;
    double mbps;
    uint8_t quality;
};

/* Callbacks */
typedef void (*yetty_vnc_on_frame_fn)(void *userdata);
typedef void (*yetty_vnc_on_connected_fn)(void *userdata);
typedef void (*yetty_vnc_on_disconnected_fn)(void *userdata);

/* Create client - event_loop used for async socket I/O */
struct yetty_vnc_client_ptr_result yetty_yvnc_client_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_yplatform_event_loop *event_loop, uint16_t width, uint16_t height);

/* Destroy client (handles NULL) */
struct yetty_ycore_void_result yetty_yvnc_client_destroy(struct yetty_yvnc_client *client);

/* Connect to server */
struct yetty_ycore_void_result yetty_yvnc_client_connect(struct yetty_yvnc_client *client,
                                                        const char *host, uint16_t port);

/* Disconnect */
struct yetty_ycore_void_result yetty_yvnc_client_disconnect(struct yetty_yvnc_client *client);

/* Check connection state */
int yetty_yvnc_client_is_connected(const struct yetty_yvnc_client *client);

/* Get frame dimensions */
uint16_t yetty_yvnc_client_width(const struct yetty_yvnc_client *client);
uint16_t yetty_yvnc_client_height(const struct yetty_yvnc_client *client);

/* Update texture with received tiles (call from main thread) */
struct yetty_ycore_void_result yetty_yvnc_client_update_texture(struct yetty_yvnc_client *client);

/* Render the frame */
struct yetty_ycore_void_result yetty_yvnc_client_render(struct yetty_yvnc_client *client,
                                                       WGPURenderPassEncoder pass,
                                                       uint32_t render_target_w,
                                                       uint32_t render_target_h);

/* Get texture view for external rendering */
WGPUTextureView yetty_vnc_client_get_texture_view(const struct yetty_yvnc_client *client);

/* Input forwarding */
struct yetty_ycore_void_result yetty_yvnc_client_send_mouse_move(struct yetty_yvnc_client *client,
                                                                int16_t x, int16_t y, uint8_t mods);
struct yetty_ycore_void_result yetty_yvnc_client_send_mouse_button(struct yetty_yvnc_client *client,
                                                                  int16_t x, int16_t y,
                                                                  uint8_t button, int pressed,
                                                                  uint8_t mods);
struct yetty_ycore_void_result yetty_yvnc_client_send_mouse_scroll(struct yetty_yvnc_client *client,
                                                                  int16_t x, int16_t y,
                                                                  int16_t delta_x, int16_t delta_y,
                                                                  uint8_t mods);
struct yetty_ycore_void_result yetty_yvnc_client_send_key_down(struct yetty_yvnc_client *client,
                                                              uint32_t keycode, uint32_t scancode,
                                                              uint8_t mods);
struct yetty_ycore_void_result yetty_yvnc_client_send_key_up(struct yetty_yvnc_client *client,
                                                            uint32_t keycode, uint32_t scancode,
                                                            uint8_t mods);
struct yetty_ycore_void_result yetty_yvnc_client_send_char_with_mods(struct yetty_yvnc_client *client,
                                                                    uint32_t codepoint,
                                                                    uint8_t mods);
struct yetty_ycore_void_result yetty_yvnc_client_send_resize(struct yetty_yvnc_client *client,
                                                            uint16_t width, uint16_t height);
struct yetty_ycore_void_result yetty_yvnc_client_send_frame_ack(struct yetty_yvnc_client *client);
struct yetty_ycore_void_result yetty_vnc_client_send_compression_config(
    struct yetty_yvnc_client *client, int force_raw, uint8_t quality, int always_full,
    uint8_t codec);

/* Set callbacks */
void yetty_yvnc_client_set_on_frame(struct yetty_yvnc_client *client, yetty_vnc_on_frame_fn callback,
                                   void *userdata);
void yetty_yvnc_client_set_on_connected(struct yetty_yvnc_client *client,
                                       yetty_vnc_on_connected_fn callback, void *userdata);
void yetty_yvnc_client_set_on_disconnected(struct yetty_yvnc_client *client,
                                          yetty_vnc_on_disconnected_fn callback, void *userdata);

/* Get stats */
struct yetty_yvnc_client_stats yetty_yvnc_client_get_stats(const struct yetty_yvnc_client *client);

/* Reconnection */
void yetty_yvnc_client_set_reconnect_params(struct yetty_yvnc_client *client, const char *host,
                                           uint16_t port);
struct yetty_ycore_void_result yetty_yvnc_client_reconnect(struct yetty_yvnc_client *client);
int yetty_yvnc_client_wants_reconnect(const struct yetty_yvnc_client *client);
void yetty_yvnc_client_clear_reconnect(struct yetty_yvnc_client *client);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVNC_VNC_CLIENT_H */
