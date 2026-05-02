#ifndef YETTY_YVNC_VNC_SERVER_H
#define YETTY_YVNC_VNC_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvnc_server;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_vnc_server_ptr, struct yetty_yvnc_server *);

/* Frame stats */
struct yetty_yvnc_server_stats {
    uint32_t tiles_sent;
    uint32_t tiles_jpeg;
    uint32_t tiles_raw;
    uint32_t avg_tile_size;
    uint32_t full_updates;
    uint32_t frames;
    uint64_t bytes_per_sec;
};

/* Input callbacks */
typedef void (*yetty_vnc_on_mouse_move_fn)(int16_t x, int16_t y, uint8_t mods, void *userdata);
typedef void (*yetty_vnc_on_mouse_button_fn)(int16_t x, int16_t y, uint8_t button, int pressed,
                                             uint8_t mods, void *userdata);
typedef void (*yetty_vnc_on_mouse_scroll_fn)(int16_t x, int16_t y, int16_t delta_x, int16_t delta_y,
                                             uint8_t mods, void *userdata);
typedef void (*yetty_vnc_on_key_down_fn)(uint32_t keycode, uint32_t scancode, uint8_t mods,
                                         void *userdata);
typedef void (*yetty_vnc_on_key_up_fn)(uint32_t keycode, uint32_t scancode, uint8_t mods,
                                       void *userdata);
typedef void (*yetty_vnc_on_resize_fn)(uint16_t width, uint16_t height, void *userdata);
typedef void (*yetty_vnc_on_char_with_mods_fn)(uint32_t codepoint, uint8_t mods, void *userdata);

struct yetty_yplatform_wgpu;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yconfig_config;

/* Create server. wgpu provides the coroutine-aware GPU await machinery used
 * by the readback path; must be non-NULL.
 *
 * `hid_pipe` (optional, may be NULL) is the platform HID pipe into which
 * remote-client input (mouse/keyboard/char/resize) will be translated and
 * posted as struct yetty_yui_event records — the same pipe the platform
 * (e.g. GLFW) writes into, making remote input indistinguishable from local
 * input downstream. When NULL, no input translation is wired and callers
 * may still install their own handlers via the public set_on_* setters. */
struct yetty_vnc_server_ptr_result yetty_yvnc_server_create(
    WGPUInstance instance, WGPUDevice device, WGPUQueue queue,
    struct yetty_yplatform_event_loop *event_loop, struct yetty_yplatform_wgpu *wgpu,
    struct yetty_ycore_xthread_event_pipe *hid_pipe, const struct yetty_yconfig_config *config);

/* Destroy server (handles NULL) */
struct yetty_ycore_void_result yetty_yvnc_server_destroy(struct yetty_yvnc_server *server);

/* Start listening on port */
struct yetty_ycore_void_result yetty_yvnc_server_start(struct yetty_yvnc_server *server,
                                                       uint16_t port);

/* Stop server */
struct yetty_ycore_void_result yetty_yvnc_server_stop(struct yetty_yvnc_server *server);

/* Check server state */
int yetty_yvnc_server_is_running(const struct yetty_yvnc_server *server);
int yetty_yvnc_server_has_clients(const struct yetty_yvnc_server *server);
int yetty_yvnc_server_is_awaiting_ack(const struct yetty_yvnc_server *server);
int yetty_yvnc_server_is_ready_for_frame(const struct yetty_yvnc_server *server);

/* Force full frame refresh */
void yetty_yvnc_server_force_full_frame(struct yetty_yvnc_server *server);

/* Tile-diff back-pressure hooks for callers implementing the render-target
 * is_busy / notify_render_skipped ops. `is_busy` is true while the async
 * readback coroutine is still in flight; `mark_redraw_pending` tells the
 * engine it owes one catch-up render once the current submit lands. Both
 * are safe to call before the first frame (when the engine is still NULL)
 * — they return false / no-op in that case. */
int yetty_yvnc_server_is_busy(const struct yetty_yvnc_server *server);
void yetty_yvnc_server_mark_redraw_pending(struct yetty_yvnc_server *server);

/*---------------------------------------------------------------------------
 * H.264 tuning knobs. Applied at encoder creation time — calling a setter
 * mid-session stages the value for the next encoder instance (size change,
 * or an explicit reset). 0 (or <= 0 for floats) means "use default".
 *-------------------------------------------------------------------------*/
void yetty_yvnc_server_set_h264_bitrate(struct yetty_yvnc_server *server, uint32_t bps);
void yetty_yvnc_server_set_h264_framerate(struct yetty_yvnc_server *server, float fps);
void yetty_yvnc_server_set_h264_idr_interval(struct yetty_yvnc_server *server, uint32_t frames);
void yetty_yvnc_server_set_h264_screen_content(struct yetty_yvnc_server *server, int on);

/* Rectangle merging */
void yetty_yvnc_server_set_merge_rectangles(struct yetty_yvnc_server *server, int enable);
int yetty_yvnc_server_get_merge_rectangles(const struct yetty_yvnc_server *server);

/* Compression settings */
void yetty_yvnc_server_set_force_raw(struct yetty_yvnc_server *server, int enable);
int yetty_yvnc_server_get_force_raw(const struct yetty_yvnc_server *server);
void yetty_yvnc_server_set_jpeg_quality(struct yetty_yvnc_server *server, uint8_t quality);
uint8_t yetty_yvnc_server_get_jpeg_quality(const struct yetty_yvnc_server *server);
void yetty_yvnc_server_set_always_full_frame(struct yetty_yvnc_server *server, int enable);
int yetty_yvnc_server_get_always_full_frame(const struct yetty_yvnc_server *server);

/* H.264 encoding */
void yetty_yvnc_server_set_use_h264(struct yetty_yvnc_server *server, int enable);
int yetty_yvnc_server_get_use_h264(const struct yetty_yvnc_server *server);
void yetty_yvnc_server_force_h264_idr(struct yetty_yvnc_server *server);

/* Send frame to all clients */
struct yetty_ycore_void_result yetty_yvnc_server_send_frame(struct yetty_yvnc_server *server,
                                                            WGPUTexture texture,
                                                            const uint8_t *cpu_pixels,
                                                            uint32_t width, uint32_t height);

/* Send frame (GPU-only, will read back dirty tiles) */
struct yetty_ycore_void_result yetty_yvnc_server_send_frame_gpu(struct yetty_yvnc_server *server,
                                                                WGPUTexture texture, uint32_t width,
                                                                uint32_t height);

/* Check for pending input */
int yetty_vnc_server_has_pending_input(const struct yetty_yvnc_server *server);

/* Process pending input events */
struct yetty_ycore_void_result yetty_vnc_server_process_input(struct yetty_yvnc_server *server);

/* Get stats */
struct yetty_yvnc_server_stats yetty_yvnc_server_get_stats(const struct yetty_yvnc_server *server);

/* HID input (mouse/keyboard/char/resize) is dispatched directly into the
 * hid_pipe passed to yetty_yvnc_server_create. No callback setters are
 * exposed. */

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVNC_VNC_SERVER_H */
