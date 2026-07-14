/*
 * vnc-server.c - VNC server using libuv TCP via event loop
 */

#include <yetty/yvnc/vnc-server.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/time.h>
#include <yetty/webgpu/error.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yplatform/ywebgpu.h>
#include <yetty/yrender-utils/tile-diff.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yconfig/config.h>
#include "protocol.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

#ifdef YETTY_HAS_YVCODEC
#include <yetty/yvcodec/encoder.h>
/* minimp4 implementation lives in the dedicated minimp4 static lib —
 * see build-tools/yetty/minimp4.cmake. Just pull the header here. */
#include <minimp4.h>
#endif

#ifdef YETTY_HAS_YAUDIO_RECORD
/* --record-audio path: mic capture via the yplatform audio abstraction
 * (miniaudio underneath), fdk-aac AAC-LC encoder, second mp4 track on
 * the same MP4E_mux_t the video already writes to. */
#include <yetty/yplatform/audio.h>
/* The CPM-vendored fdk-aac source tree exports libAACenc/include etc.
 * directly, so the header has no fdk-aac/ prefix (that prefix only
 * exists in installed copies of the library). */
#include <aacenc_lib.h>

/* AAC-LC parameters. 48 kHz stereo @ 128 kbps matches the poc recorder
 * and every common consumer of the resulting .mp4. 1024 samples per
 * frame is the AAC-LC frame size (fixed by the codec). */
#define YETTY_YVNC_RECORD_AUDIO_SAMPLE_RATE 48000u
#define YETTY_YVNC_RECORD_AUDIO_CHANNELS 2u
#define YETTY_YVNC_RECORD_AUDIO_BITRATE 128000u
#define YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE 1024u
#endif

#define MAX_CLIENTS 16
#define FULL_REFRESH_INTERVAL 300
#define RECV_BUFFER_SIZE 65536
/* If a client takes longer than this to ack a frame, assume it's dead /
 * unresponsive and drop the TCP connection. TCP itself can't surface a stuck
 * receiver any other way (data is buffered all the way up to the kernel send
 * window). */
#define ACK_TIMEOUT_SEC 5.0

/* Per-client context. Holds the wire-protocol state for one TCP peer plus the
 * per-client back-pressure book-keeping (owed_tiles bitmap, awaiting_ack /
 * awaiting_seq, next_seq, last_send_time). Each client paces independently —
 * a slow viewer does not throttle fast ones. */
struct yetty_yvnc_vnc_client_ctx {
    struct yetty_yvnc_server *server;
    struct yetty_yevent_conn *conn;
    int slot;

    /* Input buffer */
    uint8_t *recv_buffer;
    size_t recv_buffer_capacity;
    size_t recv_offset;
    size_t recv_needed;
    int reading_header;
    struct yetty_yvnc_vnc_input_header header;

    /* Stop-and-wait flow control. While awaiting_ack is set, no frame_header
     * is sent to this client; new dirty tiles accumulate into owed_tiles
     * instead. Cleared when the client's FRAME_ACK arrives with seq matching
     * awaiting_seq. */
    int awaiting_ack;
    uint32_t awaiting_seq;
    uint32_t next_seq;
    double last_send_time;

    /* Tiles this client is missing since its last ack. Sized for the current
     * frame's tiles_x*tiles_y (server-wide). One byte per tile (used as a
     * bool — keeping it byte-sized lets us memset/cheap-OR with the engine's
     * dirty bitmap). Lazily (re)allocated by ensure_client_owed. */
    uint8_t *owed_tiles;
    uint32_t owed_tiles_count;

    /* Forces the next flush to mark every tile owed regardless of the diff
     * bitmap. Set on connect and after an ack-timeout reset. */
    int need_full_frame;
};

struct yetty_yvnc_server {
    WGPUInstance instance;
    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_yplatform_wgpu *wgpu;

    /* Event loop for async I/O */
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_tcp_server_id tcp_server_id;

    /* Optional HID pipe: when non-NULL, remote-client input is translated
     * here into struct yetty_yui_event records (same path as local GLFW
     * input). Set via yetty_yvnc_server_create. */
    struct yetty_ycore_xthread_event_pipe *hid_pipe;

    /* Server state */
    uint16_t port;
    int running;

    /* Connected clients */
    struct yetty_yvnc_vnc_client_ctx *clients[MAX_CLIENTS];
    size_t client_count;

    /* Frame dimensions */
    uint32_t last_width;
    uint32_t last_height;

    /* Delta + readback pipeline. Owns the prev-frame texture, the diff
   * compute pipeline, and the readback buffers. See
   * include/yetty/yrender-utils/tile-diff.h. */
    struct yetty_yrender_utils_tile_diff_engine *diff_engine;

    /* CPU framebuffer path (send_frame_cpu). */
    const uint8_t *cpu_pixels;
    uint32_t cpu_pixels_size;

    /* Packed (width*4 stride) copy of the most recent GPU readback, used
   * by encode_tile. The engine hands us a row-aligned mapped range; we
   * pack it here so encode_tile can keep using last_width*4 as the row
   * pitch. */
    uint8_t *gpu_readback_pixels;
    size_t gpu_readback_pixels_size;

    /* Dirty tile tracking */
    int *dirty_tiles;
    uint16_t tiles_x;
    uint16_t tiles_y;

    /* Settings */
    int merge_rectangles;
    int force_raw;
    uint8_t jpeg_quality;
    int always_full_frame;
    int use_h264;
    volatile int force_full_frame;

    /* JPEG compression */
    tjhandle jpeg_compressor;

#ifdef YETTY_HAS_YVCODEC
    /* H.264 encoding state — allocated lazily on first H.264 frame, torn
   * down on resolution change. yuv_buf is a single heap block holding the
   * three planes back-to-back at the strides below (16-byte aligned). */
    struct yetty_yvcodec_encoder *h264_encoder;
    uint8_t *yuv_buf;
    size_t yuv_buf_size;
    uint32_t yuv_y_stride;
    uint32_t yuv_uv_stride;
    uint32_t h264_enc_width;
    uint32_t h264_enc_height;

    /* User-facing H.264 tuning knobs. Applied at encoder creation time;
   * zero / negative = "leave defaults alone". Set from config/CLI via
   * yetty_vnc_server_set_h264_*. */
    uint32_t h264_cfg_bitrate;      /* 0 = auto from resolution */
    float h264_cfg_framerate;       /* <= 0 = default (30 fps) */
    uint32_t h264_cfg_idr_interval; /* 0 = default (60 frames) */
    int h264_cfg_screen_content;    /* -1 = default (1); 0/1 = explicit */

    /* MP4 recording. When record_path is set, every encoded H.264 frame is
   * also written to this file. The writer is opened lazily on the first
   * frame so we know the resolution. record_prev_ts_ms is the previous
   * frame's timestamp; sample duration is timestamp[i] - timestamp[i-1]. */
    char *record_path;
    FILE *record_file;
    MP4E_mux_t *record_mux;
    mp4_h26x_writer_t record_writer;
    int record_writer_initialized;
    uint32_t record_frames;
    uint32_t record_prev_ts_ms;
#endif

#ifdef YETTY_HAS_YAUDIO_RECORD
    /* --record-audio state. Set up by config_vnc_server when both
     * vnc/record-file and vnc/record-audio are on. Capture pushes s16
     * frames into its own ring; the encode pump (audio_pump) drains
     * 1024-sample chunks, hands them to fdk-aac, and writes each AAC
     * frame to record_audio_track_id on record_mux. */
    struct yetty_yplatform_audio_capture *record_audio_capture;
    HANDLE_AACENCODER record_audio_encoder;
    int record_audio_track_id;
    /* Leftover s16 samples that didn't yet fill an AAC frame. Grows to
     * AAC_FRAME_SIZE * channels at most; carried across pump calls. */
    int16_t *record_audio_carry;
    size_t record_audio_carry_frames;
    /* Scratch drained-per-pump buffer. Sized once (1s worth of frames)
     * so pump doesn't allocate on the hot path. */
    int16_t *record_audio_scratch;
    size_t record_audio_scratch_frames;
    uint32_t record_audio_frames_written;
#endif

    /* Stats */
    struct yetty_yvnc_server_stats stats;
    struct {
        uint32_t tiles_sent;
        uint32_t tiles_jpeg;
        uint32_t tiles_raw;
        uint64_t bytes_sent;
        uint64_t bytes_jpeg;
        uint64_t bytes_raw;
        uint32_t full_updates;
        uint32_t frames;
        double last_report_time;
    } current_stats;

    uint32_t frames_since_full_refresh;
};

/* Forward declarations */
static void dispatch_input(struct yetty_yvnc_server *server,
                           struct yetty_yvnc_vnc_client_ctx *client_ctx,
                           const struct yetty_yvnc_vnc_input_header *hdr, const uint8_t *data);
static struct yetty_ycore_void_result ensure_cpu_state(struct yetty_yvnc_server *server,
                                                       uint32_t width, uint32_t height);
static struct yetty_ycore_void_result encode_tile(struct yetty_yvnc_server *server, uint16_t tx,
                                                  uint16_t ty, uint8_t **out_data, size_t *out_size,
                                                  uint8_t *out_encoding);
static struct yetty_ycore_void_result encode_and_send_dirty_tiles(struct yetty_yvnc_server *server,
                                                                  uint32_t width, uint32_t height);
static struct yetty_ycore_void_result ensure_client_owed(
    struct yetty_yvnc_vnc_client_ctx *client_ctx, uint32_t num_tiles);
static void check_ack_timeouts(struct yetty_yvnc_server *server);

/*===========================================================================
 * TCP Server Callbacks
 *===========================================================================*/

static void *vnc_server_on_connect(void *ctx, struct yetty_yevent_conn *conn)
{
    struct yetty_yvnc_server *server = ctx;

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        ywarn("VNC max clients reached, rejecting");
        server->event_loop->ops->tcp_close(conn);
        return NULL;
    }

    struct yetty_yvnc_vnc_client_ctx *client_ctx =
        calloc(1, sizeof(struct yetty_yvnc_vnc_client_ctx));
    if (!client_ctx) {
        yerror("VNC failed to allocate client context");
        server->event_loop->ops->tcp_close(conn);
        return NULL;
    }

    client_ctx->server = server;
    client_ctx->conn = conn;
    client_ctx->slot = slot;
    client_ctx->recv_buffer_capacity = RECV_BUFFER_SIZE;
    client_ctx->recv_buffer = malloc(client_ctx->recv_buffer_capacity);
    if (!client_ctx->recv_buffer) {
        free(client_ctx);
        server->event_loop->ops->tcp_close(conn);
        return NULL;
    }
    client_ctx->recv_needed = sizeof(struct yetty_yvnc_vnc_input_header);
    client_ctx->reading_header = 1;
    /* Brand-new client starts unblocked but needs every tile on first flush. */
    client_ctx->awaiting_ack = 0;
    client_ctx->awaiting_seq = 0;
    client_ctx->next_seq = 1;
    client_ctx->last_send_time = yetty_yplatform_ytime_monotonic_sec();
    client_ctx->need_full_frame = 1;

    server->clients[slot] = client_ctx;
    server->client_count++;
    /* Also force the engine to mark every tile dirty on its next readback —
     * otherwise a quiescent screen would produce an empty diff bitmap and the
     * new client would never receive the initial framebuffer contents. */
    server->force_full_frame = 1;

    yinfo("VNC client connected (slot %d, total %zu)", slot, server->client_count);

    /* Trigger render to send first frame to new client */
    if (server->event_loop && server->event_loop->ops->request_render) {
        server->event_loop->ops->request_render(server->event_loop);
    }

    return client_ctx;
}

static void vnc_server_on_alloc(void *conn_ctx, size_t suggested, char **buf, size_t *len)
{
    struct yetty_yvnc_vnc_client_ctx *client_ctx = conn_ctx;
    (void)suggested;

    if (!client_ctx) {
        *buf = NULL;
        *len = 0;
        return;
    }

    /* Return pointer into recv buffer at current offset */
    size_t space = client_ctx->recv_buffer_capacity - client_ctx->recv_offset;
    *buf = (char *)client_ctx->recv_buffer + client_ctx->recv_offset;
    *len = space;
}

static void vnc_server_on_data(void *conn_ctx, struct yetty_yevent_conn *conn, const char *data,
                               long nread)
{
    struct yetty_yvnc_vnc_client_ctx *client_ctx = conn_ctx;
    (void)conn;
    (void)data; /* Data is already in recv_buffer from on_alloc */

    if (!client_ctx || nread <= 0) {
        return;
    }

    struct yetty_yvnc_server *server = client_ctx->server;
    client_ctx->recv_offset += (size_t)nread;

    /* Process complete messages */
    while (client_ctx->recv_offset >= client_ctx->recv_needed) {
        if (client_ctx->reading_header) {
            /* Parse header */
            memcpy(&client_ctx->header, client_ctx->recv_buffer,
                   sizeof(struct yetty_yvnc_vnc_input_header));

            if (client_ctx->header.data_size > 0) {
                client_ctx->reading_header = 0;
                client_ctx->recv_needed =
                    sizeof(struct yetty_yvnc_vnc_input_header) + client_ctx->header.data_size;

                /* Resize buffer if needed */
                if (client_ctx->recv_needed > client_ctx->recv_buffer_capacity) {
                    size_t new_cap = client_ctx->recv_needed * 2;
                    uint8_t *new_buf = realloc(client_ctx->recv_buffer, new_cap);
                    if (!new_buf) {
                        yerror("VNC realloc failed");
                        return;
                    }
                    client_ctx->recv_buffer = new_buf;
                    client_ctx->recv_buffer_capacity = new_cap;
                }
                continue;
            }
            /* No payload - dispatch immediately */
        }

        /* Dispatch input */
        const uint8_t *payload =
            client_ctx->recv_buffer + sizeof(struct yetty_yvnc_vnc_input_header);
        dispatch_input(server, client_ctx, &client_ctx->header, payload);

        /* Shift remaining data */
        size_t consumed = sizeof(struct yetty_yvnc_vnc_input_header) + client_ctx->header.data_size;
        size_t remaining = client_ctx->recv_offset - consumed;
        if (remaining > 0) {
            memmove(client_ctx->recv_buffer, client_ctx->recv_buffer + consumed, remaining);
        }
        client_ctx->recv_offset = remaining;
        client_ctx->recv_needed = sizeof(struct yetty_yvnc_vnc_input_header);
        client_ctx->reading_header = 1;
    }
}

static void vnc_server_on_disconnect(void *conn_ctx)
{
    struct yetty_yvnc_vnc_client_ctx *client_ctx = conn_ctx;
    if (!client_ctx) {
        return;
    }

    struct yetty_yvnc_server *server = client_ctx->server;
    int slot = client_ctx->slot;

    yinfo("VNC client disconnected (slot %d)", slot);

    server->clients[slot] = NULL;
    server->client_count--;

    free(client_ctx->recv_buffer);
    free(client_ctx->owed_tiles);
    free(client_ctx);
}

/*===========================================================================
 * Public API
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * HID pipe translation shims
 *
 * When the server is created with a non-NULL hid_pipe, dispatch_input calls
 * these translators directly to convert RFB input into struct yetty_yui_event
 * records and write them into the HID pipe — same downstream path as local
 * GLFW input.
 *---------------------------------------------------------------------------*/

static void hid_push_event(struct yetty_yvnc_server *server, const struct yetty_yui_event *event)
{
    if (!server || !server->hid_pipe) {
        return;
    }
    server->hid_pipe->ops->write(server->hid_pipe, event, sizeof(*event));
}

static void hid_on_mouse_move(struct yetty_yvnc_server *server, int16_t x, int16_t y, uint8_t mods)
{
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_MOUSE_MOVE;
    ev.mouse.x = (float)x;
    ev.mouse.y = (float)y;
    ev.mouse.mods = mods;
    hid_push_event(server, &ev);
}

static void hid_on_mouse_button(struct yetty_yvnc_server *server, int16_t x, int16_t y,
                                uint8_t button, int pressed, uint8_t mods)
{
    struct yetty_yui_event ev = {0};
    ev.type = pressed ? YETTY_YCORE_MOUSE_DOWN : YETTY_YCORE_MOUSE_UP;
    ev.mouse.x = (float)x;
    ev.mouse.y = (float)y;
    ev.mouse.button = button;
    ev.mouse.mods = mods;
    hid_push_event(server, &ev);
}

static void hid_on_mouse_scroll(struct yetty_yvnc_server *server, int16_t x, int16_t y, int16_t dx,
                                int16_t dy, uint8_t mods)
{
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_MOUSE_SCROLL;
    ev.mouse_scroll.x = (float)x;
    ev.mouse_scroll.y = (float)y;
    ev.mouse_scroll.dx = (float)dx;
    ev.mouse_scroll.dy = (float)dy;
    ev.mouse_scroll.mods = mods;
    hid_push_event(server, &ev);
}

static void hid_on_key_down(struct yetty_yvnc_server *server, uint32_t keycode, uint32_t scancode,
                            uint8_t mods)
{
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_KEY_DOWN;
    ev.key.key = (int)keycode;
    ev.key.scancode = (int)scancode;
    ev.key.mods = mods;
    hid_push_event(server, &ev);
}

static void hid_on_key_up(struct yetty_yvnc_server *server, uint32_t keycode, uint32_t scancode,
                          uint8_t mods)
{
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_KEY_UP;
    ev.key.key = (int)keycode;
    ev.key.scancode = (int)scancode;
    ev.key.mods = mods;
    hid_push_event(server, &ev);
}

static void hid_on_char(struct yetty_yvnc_server *server, uint32_t codepoint, uint8_t mods)
{
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_CHAR;
    ev.chr.codepoint = codepoint;
    ev.chr.mods = mods;
    hid_push_event(server, &ev);
}

static void hid_on_resize(struct yetty_yvnc_server *server, uint16_t width, uint16_t height,
                          float content_scale)
{
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_RESIZE;
    ev.resize.width = (float)width;
    ev.resize.height = (float)height;
    /* Viewer-declared display density; 0 when the peer didn't send one.
     * The app-level RESIZE handler adopts a positive value as the new
     * content scale before re-laying out at width x height. */
    ev.resize.content_scale = content_scale;
    hid_push_event(server, &ev);
}

#ifdef YETTY_HAS_YVCODEC
/* minimp4 file-write callback. `token` is the FILE *. */
YETTY_EXTERNAL_CALLBACK
static int vnc_record_write_cb(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = token;
#ifdef _WIN32
    if (_fseeki64(f, offset, SEEK_SET) != 0) {
        return -1;
    }
#else
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) {
        return -1;
    }
#endif
    return fwrite(buffer, 1, size, f) != size ? -1 : 0;
}
#endif

#ifdef YETTY_HAS_YAUDIO_RECORD
/* Bring up the mic capture + AAC encoder + mp4 audio track. Called
 * from config_vnc_server when both --record and --record-audio are
 * set. On failure the whole record path is aborted (mp4 without the
 * expected audio track would silently produce a video-only file). */
static struct yetty_ycore_void_result record_audio_setup(struct yetty_yvnc_server *server,
                                                         const char *device_sel)
{
    struct yetty_yplatform_audio_capture_ptr_result cap_res = yetty_yplatform_audio_capture_create(
        YETTY_YVNC_RECORD_AUDIO_SAMPLE_RATE, YETTY_YVNC_RECORD_AUDIO_CHANNELS, device_sel);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cap_res, "vnc record-audio: capture create failed");
    server->record_audio_capture = cap_res.value;

    HANDLE_AACENCODER enc = NULL;
    if (aacEncOpen(&enc, 0, YETTY_YVNC_RECORD_AUDIO_CHANNELS) != AACENC_OK) {
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: aacEncOpen failed");
    }
    /* AOT 2 = AAC-LC; TT_MP4_RAW hands raw frames back for direct mp4
     * sample insertion (no ADTS wrapping). */
    if (aacEncoder_SetParam(enc, AACENC_AOT, 2) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_SAMPLERATE, YETTY_YVNC_RECORD_AUDIO_SAMPLE_RATE) !=
            AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_CHANNELMODE,
                            YETTY_YVNC_RECORD_AUDIO_CHANNELS == 2u ? MODE_2 : MODE_1) !=
            AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_BITRATE, YETTY_YVNC_RECORD_AUDIO_BITRATE) != AACENC_OK ||
        aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_RAW) != AACENC_OK) {
        aacEncClose(&enc);
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: aacEncoder_SetParam failed");
    }
    /* No-op encode-call finalises parameter setup. */
    if (aacEncEncode(enc, NULL, NULL, NULL, NULL) != AACENC_OK) {
        aacEncClose(&enc);
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: aacEncEncode init failed");
    }
    AACENC_InfoStruct info = {0};
    if (aacEncInfo(enc, &info) != AACENC_OK) {
        aacEncClose(&enc);
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: aacEncInfo failed");
    }
    server->record_audio_encoder = enc;

    /* Add the audio track to the same mux the video will use. Track
     * time_scale = sample rate; default_duration = one AAC frame's
     * worth of samples. object_type_indication = 0x40 (AAC / ISO
     * 14496-3). */
    MP4E_track_t track = {0};
    track.track_media_kind = e_audio;
    track.language[0] = 'u';
    track.language[1] = 'n';
    track.language[2] = 'd';
    track.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
    track.time_scale = YETTY_YVNC_RECORD_AUDIO_SAMPLE_RATE;
    track.default_duration = YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE;
    track.u.a.channelcount = YETTY_YVNC_RECORD_AUDIO_CHANNELS;
    int track_id = MP4E_add_track(server->record_mux, &track);
    if (track_id < 0) {
        aacEncClose(&server->record_audio_encoder);
        server->record_audio_encoder = NULL;
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: MP4E_add_track failed");
    }
    server->record_audio_track_id = track_id;
    /* AudioSpecificConfig blob — muxer needs it to describe the codec
     * to the decoder. */
    MP4E_set_dsi(server->record_mux, track_id, info.confBuf, (int)info.confSize);

    /* Scratch buffers. Sized for 1s of audio (48000 frames × 2 ch × 2 B
     * ≈ 190 KB); pump reads up to that per tick. */
    size_t scratch_frames = YETTY_YVNC_RECORD_AUDIO_SAMPLE_RATE;
    server->record_audio_scratch =
        calloc(scratch_frames * YETTY_YVNC_RECORD_AUDIO_CHANNELS, sizeof(int16_t));
    server->record_audio_carry = calloc(
        YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE * YETTY_YVNC_RECORD_AUDIO_CHANNELS, sizeof(int16_t));
    if (!server->record_audio_scratch || !server->record_audio_carry) {
        free(server->record_audio_scratch);
        free(server->record_audio_carry);
        server->record_audio_scratch = NULL;
        server->record_audio_carry = NULL;
        aacEncClose(&server->record_audio_encoder);
        server->record_audio_encoder = NULL;
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: buffer alloc failed");
    }
    server->record_audio_scratch_frames = scratch_frames;

    struct yetty_ycore_void_result start_res =
        yetty_yplatform_audio_capture_start(server->record_audio_capture);
    if (YETTY_IS_ERR(start_res)) {
        free(server->record_audio_scratch);
        free(server->record_audio_carry);
        server->record_audio_scratch = NULL;
        server->record_audio_carry = NULL;
        aacEncClose(&server->record_audio_encoder);
        server->record_audio_encoder = NULL;
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
        return YETTY_ERR(yetty_ycore_void, "vnc record-audio: capture start failed", start_res);
    }
    yinfo("VNC record-audio: %u Hz × %u ch AAC-LC @ %u bps -> track %d",
          YETTY_YVNC_RECORD_AUDIO_SAMPLE_RATE, YETTY_YVNC_RECORD_AUDIO_CHANNELS,
          YETTY_YVNC_RECORD_AUDIO_BITRATE, track_id);
    return YETTY_OK_VOID();
}

/* Encode one 1024-sample AAC frame (frames_per_channel samples ×
 * channels) into the mp4. Returns the number of encoded bytes written,
 * 0 if the encoder held onto the input (rare — fdk-aac is single-pass
 * for AAC-LC), negative on error. */
static int record_audio_encode_frame(struct yetty_yvnc_server *server, const int16_t *pcm)
{
    AACENC_BufDesc in_buf = {0}, out_buf = {0};
    AACENC_InArgs in_args = {0};
    AACENC_OutArgs out_args = {0};

    void *in_ptr = (void *)pcm;
    int in_id = IN_AUDIO_DATA;
    int in_elem_size = (int)sizeof(int16_t);
    int in_size = (int)(YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE * YETTY_YVNC_RECORD_AUDIO_CHANNELS *
                        sizeof(int16_t));
    in_buf.numBufs = 1;
    in_buf.bufs = &in_ptr;
    in_buf.bufferIdentifiers = &in_id;
    in_buf.bufSizes = &in_size;
    in_buf.bufElSizes = &in_elem_size;
    in_args.numInSamples =
        (INT)(YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE * YETTY_YVNC_RECORD_AUDIO_CHANNELS);

    /* fdk-aac's typical max AAC frame is 6144 bits per channel; give
     * it a comfortable buffer. */
    uint8_t out[8192];
    void *out_ptr = out;
    int out_id = OUT_BITSTREAM_DATA;
    int out_size = (int)sizeof(out);
    int out_elem_size = 1;
    out_buf.numBufs = 1;
    out_buf.bufs = &out_ptr;
    out_buf.bufferIdentifiers = &out_id;
    out_buf.bufSizes = &out_size;
    out_buf.bufElSizes = &out_elem_size;

    AACENC_ERROR err =
        aacEncEncode(server->record_audio_encoder, &in_buf, &out_buf, &in_args, &out_args);
    if (err != AACENC_OK) {
        ywarn("VNC record-audio: aacEncEncode failed: %d", (int)err);
        return -1;
    }
    if (out_args.numOutBytes <= 0) {
        return 0;
    }
    int put = MP4E_put_sample(server->record_mux, server->record_audio_track_id, out,
                              out_args.numOutBytes, (int)YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE,
                              MP4E_SAMPLE_DEFAULT);
    if (put != MP4E_STATUS_OK) {
        ywarn("VNC record-audio: MP4E_put_sample failed: %d", put);
        return -1;
    }
    server->record_audio_frames_written++;
    return out_args.numOutBytes;
}

/* Drain the mic ring and push whole AAC frames into the mp4. Runs on
 * the video-encode thread — same cadence as the video path — so we
 * naturally interleave audio samples with video NALs in wall-clock
 * order. Any partial AAC frame worth of samples is held in
 * record_audio_carry for next tick. */
static void record_audio_pump(struct yetty_yvnc_server *server)
{
    if (!server->record_audio_capture || !server->record_audio_encoder) {
        return;
    }
    const size_t channels = YETTY_YVNC_RECORD_AUDIO_CHANNELS;
    const size_t frame_samples = YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE;

    /* 1. Drain the mic ring into the scratch buffer. */
    struct yetty_ycore_size_result read_res = yetty_yplatform_audio_capture_read_s16(
        server->record_audio_capture, server->record_audio_scratch,
        server->record_audio_scratch_frames);
    if (YETTY_IS_ERR(read_res)) {
        ywarn("VNC record-audio: capture read failed: %s",
              read_res.error.msg ? read_res.error.msg : "?");
        return;
    }
    size_t got_frames = read_res.value;
    if (got_frames == 0u && server->record_audio_carry_frames == 0u) {
        return;
    }

    /* 2. Concatenate carry + fresh, encode in frame_samples chunks. */
    const int16_t *src = server->record_audio_scratch;
    size_t src_frames = got_frames;

    while (server->record_audio_carry_frames + src_frames >= frame_samples) {
        int16_t frame[YETTY_YVNC_RECORD_AUDIO_FRAME_SIZE * YETTY_YVNC_RECORD_AUDIO_CHANNELS];
        size_t need = frame_samples - server->record_audio_carry_frames;

        if (server->record_audio_carry_frames > 0u) {
            memcpy(frame, server->record_audio_carry,
                   server->record_audio_carry_frames * channels * sizeof(int16_t));
        }
        memcpy(frame + server->record_audio_carry_frames * channels, src,
               need * channels * sizeof(int16_t));

        (void)record_audio_encode_frame(server, frame);

        server->record_audio_carry_frames = 0u;
        src += need * channels;
        src_frames -= need;
    }

    /* 3. Stash the leftover partial frame for next tick. */
    if (src_frames > 0u) {
        memcpy(server->record_audio_carry + server->record_audio_carry_frames * channels, src,
               src_frames * channels * sizeof(int16_t));
        server->record_audio_carry_frames += src_frames;
    }
}

/* Reverse of record_audio_setup. Safe to call with a half-initialised
 * state — every pointer is NULL-checked. Called from destroy. */
static void record_audio_teardown(struct yetty_yvnc_server *server)
{
    if (server->record_audio_capture) {
        struct yetty_ycore_void_result stop_res =
            yetty_yplatform_audio_capture_stop(server->record_audio_capture);
        if (YETTY_IS_ERR(stop_res)) {
            ywarn("VNC record-audio: capture stop failed: %s",
                  stop_res.error.msg ? stop_res.error.msg : "?");
            yetty_ycore_error_destroy(stop_res.error);
        }
        yetty_yplatform_audio_capture_destroy(server->record_audio_capture);
        server->record_audio_capture = NULL;
    }
    if (server->record_audio_encoder) {
        aacEncClose(&server->record_audio_encoder);
        server->record_audio_encoder = NULL;
    }
    free(server->record_audio_scratch);
    free(server->record_audio_carry);
    server->record_audio_scratch = NULL;
    server->record_audio_carry = NULL;
    server->record_audio_scratch_frames = 0u;
    server->record_audio_carry_frames = 0u;
}
#endif /* YETTY_HAS_YAUDIO_RECORD */

static struct yetty_ycore_void_result config_vnc_server(struct yetty_yvnc_server *vnc_server,
                                                        const struct yetty_yconfig_config *config)
{
    /* Apply per-flag compression / delta-tracking settings. Each config
        * key comes from the matching --vnc-* CLI flag and tunes the VNC
        * server's encode+send path. Setters are no-ops on NULL / unset. */
    if (config->ops->get_bool(config, "vnc/raw", 0)) {
        yetty_yvnc_server_set_force_raw(vnc_server, 1);
    }
    int jpeg_q = config->ops->get_int(config, "vnc/compression-quality", 0);
    if (jpeg_q > 0) {
        yetty_yvnc_server_set_jpeg_quality(vnc_server, (uint8_t)jpeg_q);
    }
    if (config->ops->get_bool(config, "vnc/always-full", 0)) {
        yetty_yvnc_server_set_always_full_frame(vnc_server, 1);
    }
    if (config->ops->get_bool(config, "vnc/use-h264", 0)) {
        yetty_yvnc_server_set_use_h264(vnc_server, 1);
    }
    if (config->ops->get_bool(config, "vnc/merge-rects", 0)) {
        yetty_yvnc_server_set_merge_rectangles(vnc_server, 1);
    }

#ifdef YETTY_HAS_YVCODEC
    /* H.264 tuning knobs — read from vnc/h264/... config keys. Each is
        * optional; the server treats zero / unset as "use encoder defaults".
        * Setters live behind YETTY_HAS_YVCODEC (line 726+), so the calls do
        * too — otherwise this references undefined symbols when OpenH264 /
        * yvcodec is disabled (e.g. webasm). */
    int h264_bps = config->ops->get_int(config, "vnc/h264/bitrate", 0);
    if (h264_bps > 0) {
        yetty_yvnc_server_set_h264_bitrate(vnc_server, (uint32_t)h264_bps);
    }
    int h264_fps = config->ops->get_int(config, "vnc/h264/framerate", 0);
    if (h264_fps > 0) {
        yetty_yvnc_server_set_h264_framerate(vnc_server, (float)h264_fps);
    }
    int h264_idr = config->ops->get_int(config, "vnc/h264/idr-interval", 0);
    if (h264_idr > 0) {
        yetty_yvnc_server_set_h264_idr_interval(vnc_server, (uint32_t)h264_idr);
    }
    if (config->ops->has(config, "vnc/h264/screen-content")) {
        yetty_yvnc_server_set_h264_screen_content(
            vnc_server, config->ops->get_bool(config, "vnc/h264/screen-content", 1));
    }
#endif

#ifdef YETTY_HAS_YVCODEC
    /* MP4 recording: --record FILE sets vnc/record-file and forces use-h264.
   * Open the file + muxer up front; the H.264 writer is set up on the first
   * encoded frame (we need the encoder dimensions for it). */
    const char *record_path = config->ops->get_string(config, "vnc/record-file", NULL);
    if (record_path && record_path[0]) {
        vnc_server->record_path = strdup(record_path);
        if (!vnc_server->record_path) {
            return YETTY_ERR(yetty_ycore_void, "vnc record: strdup failed");
        }
        vnc_server->record_file = fopen(vnc_server->record_path, "wb");
        if (!vnc_server->record_file) {
            yerror("VNC record: failed to open %s", vnc_server->record_path);
            return YETTY_ERR(yetty_ycore_void, "vnc record: fopen failed");
        }
        vnc_server->record_mux = MP4E_open(0, 0, vnc_server->record_file, vnc_record_write_cb);
        if (!vnc_server->record_mux) {
            fclose(vnc_server->record_file);
            vnc_server->record_file = NULL;
            return YETTY_ERR(yetty_ycore_void, "vnc record: MP4E_open failed");
        }
        yinfo("VNC record: opened %s (waiting for first H.264 frame)", vnc_server->record_path);

#ifdef YETTY_HAS_YAUDIO_RECORD
        /* --record-audio: bring up mic capture + AAC encoder + mp4
         * audio track NOW, so the audio track is added to the mux
         * before the first video sample lands (the h26x writer adds
         * the video track lazily on the first NAL). */
        if (config->ops->get_bool(config, "vnc/record-audio", 0)) {
            vnc_server->record_audio_track_id = -1;
            const char *audio_device =
                config->ops->get_string(config, "vnc/record-audio-device", NULL);
            struct yetty_ycore_void_result audio_res = record_audio_setup(vnc_server, audio_device);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, audio_res, "vnc record: audio setup failed");
        }
#else
        if (config->ops->get_bool(config, "vnc/record-audio", 0)) {
            ywarn("VNC record: --record-audio requested but this build has no "
                  "audio-record support (miniaudio/fdk-aac disabled)");
        }
#endif
    }
#endif

    return YETTY_OK_VOID();
}

struct yetty_vnc_server_ptr_result yetty_yvnc_server_create(
    WGPUInstance instance, WGPUDevice device, WGPUQueue queue,
    struct yetty_yevent_event_loop *event_loop, struct yetty_yplatform_wgpu *wgpu,
    struct yetty_ycore_xthread_event_pipe *hid_pipe, const struct yetty_yconfig_config *config)
{
    if (!event_loop) {
        return YETTY_ERR(yetty_vnc_server_ptr, "event_loop is NULL");
    }
    if (!wgpu) {
        return YETTY_ERR(yetty_vnc_server_ptr, "wgpu is NULL");
    }

    struct yetty_yvnc_server *server = calloc(1, sizeof(struct yetty_yvnc_server));
    if (!server) {
        return YETTY_ERR(yetty_vnc_server_ptr, "failed to allocate server");
    }

    server->instance = instance;
    server->device = device;
    server->queue = queue;
    server->wgpu = wgpu;
    server->event_loop = event_loop;
    server->hid_pipe = hid_pipe;
    server->tcp_server_id = -1;
    server->jpeg_quality = 80;
    server->force_full_frame = 1;
#ifdef YETTY_HAS_YVCODEC
    /* -1 = "no override"; 0/1 = user set false/true. 0 would be
   * indistinguishable from "default true" if we left it as-is. */
    server->h264_cfg_screen_content = -1;
#endif

    server->jpeg_compressor = tjInitCompress();
    if (!server->jpeg_compressor) {
        free(server);
        return YETTY_ERR(yetty_vnc_server_ptr, "failed to init JPEG compressor");
    }

    /* If a HID pipe was provided, register the internal translators so that
     * remote input is forwarded into the platform HID pipe as yui_events. */

    struct yetty_ycore_void_result result = config_vnc_server(server, config);

    return YETTY_OK(yetty_vnc_server_ptr, server);
}

struct yetty_ycore_void_result yetty_yvnc_server_destroy(struct yetty_yvnc_server *server)
{
    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "yetty_vnc_server_destroy: NULL server");
    }

    struct yetty_ycore_void_result stop_r = yetty_yvnc_server_stop(server);

    if (server->diff_engine) {
        yetty_yrender_utils_tile_diff_engine_destroy(server->diff_engine);
    }

    if (server->jpeg_compressor) {
        tjDestroy(server->jpeg_compressor);
    }

#ifdef YETTY_HAS_YVCODEC
    if (server->h264_encoder) {
        yetty_yvcodec_encoder_destroy(server->h264_encoder);
    }
    free(server->yuv_buf);

    /* Finalize MP4 recording. Order matters: stop audio capture and
     * close the AAC encoder first (last audio samples land as mp4
     * samples on the same mux), then close the H.264 writer, then the
     * muxer (which writes indices), then the file. */
#ifdef YETTY_HAS_YAUDIO_RECORD
    /* One final pump to flush anything still in the mic ring, then
     * tear the audio path down. */
    if (server->record_mux) {
        record_audio_pump(server);
    }
    uint32_t audio_frames_final = server->record_audio_frames_written;
    record_audio_teardown(server);
#endif
    if (server->record_writer_initialized) {
        mp4_h26x_write_close(&server->record_writer);
    }
    if (server->record_mux) {
        MP4E_close(server->record_mux);
    }
    if (server->record_file) {
        fclose(server->record_file);
    }
    if (server->record_path) {
#ifdef YETTY_HAS_YAUDIO_RECORD
        yinfo("VNC record: saved %s (%u video frames, %u audio frames)", server->record_path,
              server->record_frames, audio_frames_final);
#else
        yinfo("VNC record: saved %s (%u frames)", server->record_path, server->record_frames);
#endif
        free(server->record_path);
    }
#endif

    free(server->dirty_tiles);
    free(server->gpu_readback_pixels);
    free(server);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_vnc_server_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_server_start(struct yetty_yvnc_server *server,
                                                       uint16_t port)
{
    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "null server");
    }

    if (server->running) {
        return YETTY_ERR(yetty_ycore_void, "already running");
    }

    /* Setup TCP server callbacks */
    struct yetty_yevent_tcp_server_callbacks callbacks = {
        .ctx = server,
        .on_connect = vnc_server_on_connect,
        .on_alloc = vnc_server_on_alloc,
        .on_data = vnc_server_on_data,
        .on_disconnect = vnc_server_on_disconnect,
    };

    /* Create TCP server */
    struct yetty_yevent_tcp_server_id_result id_res =
        server->event_loop->ops->create_tcp_server(server->event_loop, "0.0.0.0", port, &callbacks);
    if (!YETTY_IS_OK(id_res)) {
        return YETTY_ERR(yetty_ycore_void, "failed to create TCP server");
    }

    server->tcp_server_id = id_res.value;

    /* Start listening */
    struct yetty_ycore_void_result res =
        server->event_loop->ops->start_tcp_server(server->event_loop, server->tcp_server_id);
    if (!YETTY_IS_OK(res)) {
        server->tcp_server_id = -1;
        return res;
    }

    server->port = port;
    server->running = 1;

    yinfo("VNC server listening on port %u", port);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_server_start_record_only(struct yetty_yvnc_server *server)
{
    if (!server) {
        return YETTY_ERR(yetty_ycore_void, "null server");
    }
    if (server->running) {
        return YETTY_OK_VOID();
    }
    server->running = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_server_stop(struct yetty_yvnc_server *server)
{
    if (!server) {
        return YETTY_OK_VOID();
    }

    server->running = 0;

    /* Stop TCP server (closes all connections) */
    if (server->tcp_server_id >= 0) {
        server->event_loop->ops->stop_tcp_server(server->event_loop, server->tcp_server_id);
        server->tcp_server_id = -1;
    }

    /* Free any remaining client contexts */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i]) {
            free(server->clients[i]->recv_buffer);
            free(server->clients[i]->owed_tiles);
            free(server->clients[i]);
            server->clients[i] = NULL;
        }
    }
    server->client_count = 0;

    return YETTY_OK_VOID();
}

int yetty_yvnc_server_is_running(const struct yetty_yvnc_server *server)
{
    return server ? server->running : 0;
}

int yetty_yvnc_server_has_clients(const struct yetty_yvnc_server *server)
{
    return server ? (server->client_count > 0) : 0;
}

int yetty_yvnc_server_is_awaiting_ack(const struct yetty_yvnc_server *server)
{
    if (!server) {
        return 0;
    }
    /* Aggregate view across all clients — true iff at least one client is
     * mid-flight. Callers should not be using this for back-pressure (the
     * per-client gate inside encode_and_send is authoritative); kept for API
     * compatibility and observability. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (c && c->awaiting_ack) {
            return 1;
        }
    }
    return 0;
}

int yetty_yvnc_server_is_ready_for_frame(const struct yetty_yvnc_server *server)
{
    if (!server) {
        return 0;
    }
    if (server->client_count == 0) {
        /* No clients: nothing to gate on (recording, if any, never blocks). */
        return 1;
    }
    /* Ready if at least one client can be served right now. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (c && !c->awaiting_ack) {
            return 1;
        }
    }
    return 0;
}

void yetty_yvnc_server_force_full_frame(struct yetty_yvnc_server *server)
{
    if (server) {
        server->force_full_frame = 1;
    }
}

int yetty_yvnc_server_is_busy(const struct yetty_yvnc_server *server)
{
    if (!server || !server->diff_engine) {
        return 0;
    }
    return yetty_yrender_utils_tile_diff_engine_is_busy(server->diff_engine);
}

void yetty_yvnc_server_mark_redraw_pending(struct yetty_yvnc_server *server)
{
    if (!server || !server->diff_engine) {
        return;
    }
    yetty_yrender_utils_tile_diff_engine_mark_redraw_pending(server->diff_engine);
}

#ifdef YETTY_HAS_YVCODEC
void yetty_yvnc_server_set_h264_bitrate(struct yetty_yvnc_server *server, uint32_t bps)
{
    if (server) {
        server->h264_cfg_bitrate = bps;
    }
}

void yetty_yvnc_server_set_h264_framerate(struct yetty_yvnc_server *server, float fps)
{
    if (server) {
        server->h264_cfg_framerate = fps;
    }
}

void yetty_yvnc_server_set_h264_idr_interval(struct yetty_yvnc_server *server, uint32_t frames)
{
    if (server) {
        server->h264_cfg_idr_interval = frames;
    }
}

void yetty_yvnc_server_set_h264_screen_content(struct yetty_yvnc_server *server, int on)
{
    if (server) {
        server->h264_cfg_screen_content = on ? 1 : 0;
    }
}
#else
/* Symbols exist unconditionally in the public header so callers don't need
 * to guard each call with #ifdef YETTY_HAS_YVCODEC. No-ops when yvcodec is
 * disabled at build time. */
void yetty_vnc_server_set_h264_bitrate(struct yetty_vnc_server *s, uint32_t b)
{
    (void)s;
    (void)b;
}
void yetty_vnc_server_set_h264_framerate(struct yetty_vnc_server *s, float f)
{
    (void)s;
    (void)f;
}
void yetty_vnc_server_set_h264_idr_interval(struct yetty_vnc_server *s, uint32_t f)
{
    (void)s;
    (void)f;
}
void yetty_vnc_server_set_h264_screen_content(struct yetty_vnc_server *s, int on)
{
    (void)s;
    (void)on;
}
#endif

void yetty_yvnc_server_set_merge_rectangles(struct yetty_yvnc_server *server, int enable)
{
    if (server) {
        server->merge_rectangles = enable;
    }
}

int yetty_yvnc_server_get_merge_rectangles(const struct yetty_yvnc_server *server)
{
    return server ? server->merge_rectangles : 0;
}

void yetty_yvnc_server_set_force_raw(struct yetty_yvnc_server *server, int enable)
{
    if (server) {
        server->force_raw = enable;
    }
}

int yetty_yvnc_server_get_force_raw(const struct yetty_yvnc_server *server)
{
    return server ? server->force_raw : 0;
}

void yetty_yvnc_server_set_jpeg_quality(struct yetty_yvnc_server *server, uint8_t quality)
{
    if (server) {
        server->jpeg_quality = quality;
    }
}

uint8_t yetty_yvnc_server_get_jpeg_quality(const struct yetty_yvnc_server *server)
{
    return server ? server->jpeg_quality : 80;
}

void yetty_yvnc_server_set_always_full_frame(struct yetty_yvnc_server *server, int enable)
{
    if (server) {
        server->always_full_frame = enable;
    }
}

int yetty_yvnc_server_get_always_full_frame(const struct yetty_yvnc_server *server)
{
    return server ? server->always_full_frame : 0;
}

void yetty_yvnc_server_set_use_h264(struct yetty_yvnc_server *server, int enable)
{
    if (server) {
        server->use_h264 = enable;
    }
}

int yetty_yvnc_server_get_use_h264(const struct yetty_yvnc_server *server)
{
    return server ? server->use_h264 : 0;
}

void yetty_yvnc_server_force_h264_idr(struct yetty_yvnc_server *server)
{
    (void)server;
}

/*===========================================================================
 * Send to clients
 *===========================================================================*/

static struct yetty_ycore_void_result send_to_all_clients(struct yetty_yvnc_server *server,
                                                          const void *data, size_t size)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct yetty_yvnc_vnc_client_ctx *client = server->clients[i];
        if (client && client->conn) {
            server->event_loop->ops->tcp_send(client->conn, data, size);
        }
    }
    return YETTY_OK_VOID();
}

static void send_to_client(struct yetty_yvnc_server *server,
                           struct yetty_yvnc_vnc_client_ctx *client_ctx, const void *data,
                           size_t size)
{
    if (client_ctx && client_ctx->conn) {
        server->event_loop->ops->tcp_send(client_ctx->conn, data, size);
    }
}

/*
 * Make sure the client's owed_tiles bitmap matches the current tile-grid
 * size. Grows / shrinks lazily. New cells start zero (no fabricated dirty
 * bits — need_full_frame handles initial coverage).
 */
static struct yetty_ycore_void_result ensure_client_owed(
    struct yetty_yvnc_vnc_client_ctx *client_ctx, uint32_t num_tiles)
{
    if (client_ctx->owed_tiles_count == num_tiles && client_ctx->owed_tiles) {
        return YETTY_OK_VOID();
    }
    uint8_t *resized = realloc(client_ctx->owed_tiles, num_tiles);
    if (!resized) {
        return YETTY_ERR(yetty_ycore_void, "owed_tiles alloc failed");
    }
    /* If we just grew, zero the new tail. If we shrank, no init needed. */
    if (num_tiles > client_ctx->owed_tiles_count) {
        memset(resized + client_ctx->owed_tiles_count, 0, num_tiles - client_ctx->owed_tiles_count);
    }
    client_ctx->owed_tiles = resized;
    client_ctx->owed_tiles_count = num_tiles;
    return YETTY_OK_VOID();
}

/*
 * Close any client that has been waiting on an ack longer than
 * ACK_TIMEOUT_SEC. TCP itself can't surface a wedged receiver (the kernel
 * happily buffers up to the send window); this is the only mechanism we have
 * to evict a dead viewer.
 */
static void check_ack_timeouts(struct yetty_yvnc_server *server)
{
    double now = yetty_yplatform_ytime_monotonic_sec();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (!c || !c->conn || !c->awaiting_ack) {
            continue;
        }
        if (now - c->last_send_time > ACK_TIMEOUT_SEC) {
            ywarn("VNC: client slot %d ack timeout (%.1fs); closing", c->slot,
                  now - c->last_send_time);
            server->event_loop->ops->tcp_close(c->conn);
            /* on_disconnect will free the context. */
        }
    }
}

/*===========================================================================
 * CPU-side per-frame state + tile encoding
 *
 * The GPU diff + readback pipeline lives in yrender-utils/tile-diff.c. What
 * remains here is the CPU-side bookkeeping the wire encoder needs:
 *   - last_width / last_height for encode_tile's per-row pointer math
 *   - tiles_x / tiles_y for the dirty-tile iteration
 *   - dirty_tiles[] tracking which tiles still need to be sent
 * Both the CPU-input path (send_frame_cpu) and the GPU-readback sink reset
 * these via ensure_cpu_state().
 *===========================================================================*/

static struct yetty_ycore_void_result ensure_cpu_state(struct yetty_yvnc_server *server,
                                                       uint32_t width, uint32_t height)
{
    if (server->last_width == width && server->last_height == height && server->dirty_tiles) {
        return YETTY_OK_VOID();
    }

    server->force_full_frame = 1;
    if (server->diff_engine) {
        yetty_yrender_utils_tile_diff_engine_force_full(server->diff_engine);
    }

    server->last_width = width;
    server->last_height = height;
    server->tiles_x = vnc_tiles_x(width);
    server->tiles_y = vnc_tiles_y(height);

    uint32_t num_tiles = server->tiles_x * server->tiles_y;

    free(server->dirty_tiles);
    server->dirty_tiles = calloc(num_tiles, sizeof(int));
    if (!server->dirty_tiles) {
        return YETTY_ERR(yetty_ycore_void, "failed to allocate dirty tiles");
    }

    /* Tile-index space changed — stale bits in each client's owed_tiles now
     * refer to wrong tiles. Drop them; need_full_frame will repopulate on
     * the next flush. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (!c) {
            continue;
        }
        free(c->owed_tiles);
        c->owed_tiles = NULL;
        c->owed_tiles_count = 0;
        c->need_full_frame = 1;
    }

    ydebug("VNC CPU state: %ux%u, %u tiles", width, height, num_tiles);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result encode_tile(struct yetty_yvnc_server *server, uint16_t tx,
                                                  uint16_t ty, uint8_t **out_data, size_t *out_size,
                                                  uint8_t *out_encoding)
{
    const uint8_t *pixels = server->cpu_pixels ? server->cpu_pixels : server->gpu_readback_pixels;
    if (!pixels) {
        return YETTY_ERR(yetty_ycore_void, "no pixels");
    }

    uint32_t start_x = tx * VNC_TILE_SIZE;
    uint32_t start_y = ty * VNC_TILE_SIZE;
    uint32_t tile_w = VNC_TILE_SIZE;
    uint32_t tile_h = VNC_TILE_SIZE;

    if (start_x + tile_w > server->last_width) {
        tile_w = server->last_width - start_x;
    }
    if (start_y + tile_h > server->last_height) {
        tile_h = server->last_height - start_y;
    }

    uint32_t row_pitch = server->last_width * 4;
    size_t raw_size = tile_w * tile_h * 4;

    static uint8_t tile_buffer[VNC_TILE_SIZE * VNC_TILE_SIZE * 4];

    for (uint32_t y = 0; y < tile_h; y++) {
        const uint8_t *src = pixels + (start_y + y) * row_pitch + start_x * 4;
        uint8_t *dst = tile_buffer + y * tile_w * 4;
        memcpy(dst, src, tile_w * 4);
    }

    if (server->force_raw) {
        *out_data = tile_buffer;
        *out_size = raw_size;
        *out_encoding = YETTY_YVNC_VNC_ENCODING_RAW;
        return YETTY_OK_VOID();
    }

    static uint8_t jpeg_buffer[VNC_TILE_SIZE * VNC_TILE_SIZE * 4];
    unsigned long jpeg_size = sizeof(jpeg_buffer);
    unsigned char *jpeg_out = jpeg_buffer;

    int result = tjCompress2(server->jpeg_compressor, tile_buffer, tile_w, 0, tile_h, TJPF_BGRA,
                             &jpeg_out, &jpeg_size, TJSAMP_420, server->jpeg_quality,
                             TJFLAG_FASTDCT | TJFLAG_NOREALLOC);

    if (result == 0 && jpeg_size < raw_size) {
        *out_data = jpeg_buffer;
        *out_size = jpeg_size;
        *out_encoding = YETTY_YVNC_VNC_ENCODING_JPEG;
    } else {
        *out_data = tile_buffer;
        *out_size = raw_size;
        *out_encoding = YETTY_YVNC_VNC_ENCODING_RAW;
    }

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Input dispatch
 *===========================================================================*/

static void dispatch_input(struct yetty_yvnc_server *server,
                           struct yetty_yvnc_vnc_client_ctx *client_ctx,
                           const struct yetty_yvnc_vnc_input_header *hdr, const uint8_t *data)
{
    ydebug("VNC dispatch_input: type=%u size=%u", hdr->type, hdr->data_size);
    switch (hdr->type) {
    case YETTY_YVNC_VNC_INPUT_MOUSE_MOVE:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_mouse_move_event)) {
            const struct yetty_yvnc_vnc_mouse_move_event *msg = (const void *)data;
            hid_on_mouse_move(server, msg->x, msg->y, msg->mods);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_MOUSE_BUTTON:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_mouse_button_event)) {
            const struct yetty_yvnc_vnc_mouse_button_event *msg = (const void *)data;
            hid_on_mouse_button(server, msg->x, msg->y, msg->button, msg->pressed, msg->mods);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_MOUSE_SCROLL:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_mouse_scroll_event)) {
            const struct yetty_yvnc_vnc_mouse_scroll_event *msg = (const void *)data;
            hid_on_mouse_scroll(server, msg->x, msg->y, msg->delta_x, msg->delta_y, msg->mods);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_KEY_DOWN:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_key_event)) {
            const struct yetty_yvnc_vnc_key_event *msg = (const void *)data;
            hid_on_key_down(server, msg->keycode, msg->scancode, msg->mods);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_KEY_UP:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_key_event)) {
            const struct yetty_yvnc_vnc_key_event *msg = (const void *)data;
            hid_on_key_up(server, msg->keycode, msg->scancode, msg->mods);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_RESIZE:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_resize_event)) {
            const struct yetty_yvnc_vnc_resize_event *msg = (const void *)data;
            hid_on_resize(server, msg->width, msg->height,
                          vnc_content_scale_from_wire(msg->content_scale_x256));
        } else if (hdr->data_size >= 2 * sizeof(uint16_t)) {
            /* Legacy peer: width+height only, no scale field. Honor the
             * resize and leave the content scale untouched (0). */
            uint16_t legacy_dims[2];
            memcpy(legacy_dims, data, sizeof(legacy_dims));
            hid_on_resize(server, legacy_dims[0], legacy_dims[1], 0.0f);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_CHAR_WITH_MODS:
        if (hdr->data_size >= sizeof(struct yetty_yvnc_vnc_char_with_mods_event)) {
            const struct yetty_yvnc_vnc_char_with_mods_event *msg = (const void *)data;
            hid_on_char(server, msg->codepoint, msg->mods);
        }
        break;

    case YETTY_YVNC_VNC_INPUT_FRAME_ACK: {
        if (!client_ctx) {
            break;
        }
        if (hdr->data_size < sizeof(struct yetty_yvnc_vnc_frame_ack_event)) {
            /* Legacy / malformed ack with no seq. Ignore: the correct frame
             * is either still inflight (will arrive properly seq'd) or this
             * is a non-yetty client speaking the wrong protocol — in either
             * case we don't want a payloadless ack to bypass back-pressure. */
            ywarn("VNC: ack with no seq payload on slot %d; ignoring", client_ctx->slot);
            break;
        }
        const struct yetty_yvnc_vnc_frame_ack_event *msg = (const void *)data;
        uint32_t acked_seq = msg->seq;
        if (!client_ctx->awaiting_ack) {
            /* Spurious ack — we're not waiting on anything. Most likely a
             * late duplicate (TCP reorders nothing, but the same frame's
             * ack can arrive twice if the client sends from two layers). */
            ydebug("VNC: ack %u on slot %d while not awaiting; ignoring", acked_seq,
                   client_ctx->slot);
            break;
        }
        if (acked_seq != client_ctx->awaiting_seq) {
            /* Stale ack from before the current in-flight frame. Crucially,
             * we do NOT clear awaiting_ack — that would defeat the whole
             * point of stop-and-wait. The real ack will arrive next, or the
             * ACK_TIMEOUT_SEC path closes the client. */
            ydebug("VNC: stale ack %u on slot %d (waiting on %u); ignoring", acked_seq,
                   client_ctx->slot, client_ctx->awaiting_seq);
            break;
        }
        client_ctx->awaiting_ack = 0;
        /* Nudge a render so the catch-up frame ships without waiting for an
         * unrelated event. */
        if (server->event_loop && server->event_loop->ops->request_render) {
            server->event_loop->ops->request_render(server->event_loop);
        }
        break;
    }

    default:
        ydebug("VNC unknown input type %u", hdr->type);
        break;
    }
}

/*===========================================================================
 * Frame capture and send
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yvnc_server_send_frame_cpu(struct yetty_yvnc_server *server,
                                                                const uint8_t *pixels,
                                                                uint32_t width, uint32_t height)
{
    /* Recording (record_mux non-NULL) acts as a phantom local consumer:
   * even with no TCP clients we still need the encode pipeline to run so
   * frames land in the MP4. */
#ifdef YETTY_HAS_YVCODEC
    int has_consumers =
        server && server->running && (server->client_count > 0 || server->record_mux != NULL);
#else
    int has_consumers = server && server->running && server->client_count > 0;
#endif
    if (!has_consumers) {
        return YETTY_OK_VOID();
    }

    server->cpu_pixels = pixels;
    server->cpu_pixels_size = width * height * 4;

    struct yetty_ycore_void_result res = ensure_cpu_state(server, width, height);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    int do_full = server->force_full_frame || server->always_full_frame;
    if (do_full) {
        server->force_full_frame = 0;
    }

    /* Mark all tiles dirty for a full frame; the CPU path doesn't run the
   * GPU diff, so it has to flag every tile explicitly. */
    uint32_t num_tiles = server->tiles_x * server->tiles_y;
    if (do_full) {
        for (uint32_t i = 0; i < num_tiles; i++) {
            server->dirty_tiles[i] = 1;
        }
    }

    /* Delegate to the shared encode+send path so both CPU and GPU flows
   * honour merge_rectangles, raw/JPEG selection, etc. */
    return encode_and_send_dirty_tiles(server, width, height);
}

/* (flags_map_callback / pixels_map_callback removed — replaced by
 * yplatform_wgpu_buffer_map_await which yields the coroutine and resumes
 * it on the loop thread when the map completes.) */

/*===========================================================================
 * Rectangle-mode support (--vnc-merge-rects)
 *===========================================================================*/

struct yetty_yvnc_merged_rect {
    uint16_t x, y, w, h; /* pixel coordinates */
};

/*
 * Greedy maximal-rectangle merge of the dirty-tile bitmap. For each un-used
 * dirty tile, extend right as long as the row is all-dirty, then extend down
 * as long as every column stays all-dirty. Emit one rect covering the block
 * and mark those tiles as consumed. Only strict solid rectangles are merged,
 * so no wasted bandwidth on clean pixels.
 *
 * Ported from yetty-poc/src/yetty/vnc/vnc-server.cpp:mergeRectangles.
 */
static size_t merge_dirty_rects(struct yetty_yvnc_server *server, const uint8_t *bitmap,
                                struct yetty_yvnc_merged_rect *out, size_t out_cap)
{
    uint16_t tx_count = server->tiles_x;
    uint16_t ty_count = server->tiles_y;
    uint32_t num_tiles = (uint32_t)tx_count * ty_count;

    uint8_t *used = calloc(num_tiles, 1);
    if (!used) {
        return 0;
    }

    size_t out_n = 0;

    for (uint16_t ty = 0; ty < ty_count; ty++) {
        for (uint16_t tx = 0; tx < tx_count; tx++) {
            uint32_t idx = (uint32_t)ty * tx_count + tx;
            if (!bitmap[idx] || used[idx]) {
                continue;
            }

            uint16_t max_w = 1;
            uint16_t max_h = 1;

            /* Extend width. */
            while (tx + max_w < tx_count) {
                uint32_t next = (uint32_t)ty * tx_count + tx + max_w;
                if (!bitmap[next] || used[next]) {
                    break;
                }
                max_w++;
            }

            /* Extend height — every column in the row must be dirty. */
            while (ty + max_h < ty_count) {
                bool row_ok = true;
                for (uint16_t x = 0; x < max_w; x++) {
                    uint32_t check = (uint32_t)(ty + max_h) * tx_count + tx + x;
                    if (!bitmap[check] || used[check]) {
                        row_ok = false;
                        break;
                    }
                }
                if (!row_ok) {
                    break;
                }
                max_h++;
            }

            /* Consume the block. */
            for (uint16_t dy = 0; dy < max_h; dy++) {
                for (uint16_t dx = 0; dx < max_w; dx++) {
                    used[(uint32_t)(ty + dy) * tx_count + tx + dx] = 1;
                }
            }

            if (out_n < out_cap) {
                struct yetty_yvnc_merged_rect r;
                r.x = tx * VNC_TILE_SIZE;
                r.y = ty * VNC_TILE_SIZE;
                r.w = max_w * VNC_TILE_SIZE;
                r.h = max_h * VNC_TILE_SIZE;

                /* Clamp to frame bounds (right/bottom edge tiles
         * are often partial). */
                if (r.x + r.w > server->last_width) {
                    r.w = (uint16_t)(server->last_width - r.x);
                }
                if (r.y + r.h > server->last_height) {
                    r.h = (uint16_t)(server->last_height - r.y);
                }

                out[out_n++] = r;
            }
        }
    }

    free(used);
    return out_n;
}

/*
 * Encode an arbitrary pixel rectangle from the active framebuffer into
 * either raw BGRA or JPEG. The caller owns nothing; `*out_data` points
 * either at a server-owned per-call malloc (caller must free) or at the
 * turbojpeg-owned buffer (caller must tjFree). `out_free_with_tj` lets the
 * caller pick the right deallocator.
 */
static struct yetty_ycore_void_result encode_rect(struct yetty_yvnc_server *server, uint16_t px,
                                                  uint16_t py, uint16_t rw, uint16_t rh,
                                                  uint8_t **out_data, size_t *out_size,
                                                  uint8_t *out_encoding, int *out_free_with_tj)
{
    const uint8_t *pixels = server->cpu_pixels ? server->cpu_pixels : server->gpu_readback_pixels;
    if (!pixels) {
        return YETTY_ERR(yetty_ycore_void, "no pixels");
    }

    if (px + rw > server->last_width) {
        rw = (uint16_t)(server->last_width - px);
    }
    if (py + rh > server->last_height) {
        rh = (uint16_t)(server->last_height - py);
    }

    size_t raw_size = (size_t)rw * rh * 4;
    uint32_t src_stride = server->last_width * 4;

    uint8_t *rect_pixels = malloc(raw_size);
    if (!rect_pixels) {
        return YETTY_ERR(yetty_ycore_void, "rect alloc failed");
    }

    for (uint16_t y = 0; y < rh; y++) {
        const uint8_t *src = pixels + (py + y) * src_stride + px * 4;
        memcpy(rect_pixels + (size_t)y * rw * 4, src, (size_t)rw * 4);
    }

    if (server->force_raw) {
        *out_data = rect_pixels;
        *out_size = raw_size;
        *out_encoding = YETTY_YVNC_VNC_ENCODING_RECT_RAW;
        *out_free_with_tj = 0;
        return YETTY_OK_VOID();
    }

    unsigned char *jpeg_buf = NULL;
    unsigned long jpeg_size = 0;
    int result = tjCompress2(server->jpeg_compressor, rect_pixels, rw, 0, rh, TJPF_BGRA, &jpeg_buf,
                             &jpeg_size, TJSAMP_420, server->jpeg_quality, TJFLAG_FASTDCT);

    /* Only switch to JPEG if it saves meaningful bytes (matches poc: 0.8x
   * threshold avoids shipping JPEG headers for barely-compressible rects). */
    if (result == 0 && jpeg_size < (unsigned long)(raw_size * 0.8)) {
        free(rect_pixels);
        *out_data = jpeg_buf;
        *out_size = jpeg_size;
        *out_encoding = YETTY_YVNC_VNC_ENCODING_RECT_JPEG;
        *out_free_with_tj = 1;
        return YETTY_OK_VOID();
    }

    if (jpeg_buf) {
        tjFree(jpeg_buf);
    }
    *out_data = rect_pixels;
    *out_size = raw_size;
    *out_encoding = YETTY_YVNC_VNC_ENCODING_RECT_RAW;
    *out_free_with_tj = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * encode_and_send_dirty_tiles - ship the current frame to all clients.
 * Selects tile mode or rectangle mode based on server->merge_rectangles.
 *===========================================================================*/

#ifdef YETTY_HAS_YVCODEC
/*
 * H.264 full-frame send path. The encoder wants a contiguous BGRA framebuffer
 * → convert to YUV420 → hand to openh264 → wrap the resulting bitstream in a
 * single synthetic "tile" (tile 0,0) with encoding = VNC_ENCODING_H264.
 *
 * H.264 only wins when > half the tiles are dirty; isolated cell updates
 * compress better as per-tile JPEG. Caller is responsible for choosing this
 * path only when it's actually better than tile or rect mode.
 */
static struct yetty_ycore_void_result h264_send_full_frame(struct yetty_yvnc_server *server,
                                                           uint32_t width, uint32_t height)
{
    const uint8_t *pixels = server->cpu_pixels ? server->cpu_pixels : server->gpu_readback_pixels;
    if (!pixels) {
        return YETTY_ERR(yetty_ycore_void, "no pixels for H.264");
    }

    /* H.264 requires even dimensions — round down. Very occasionally this
   * loses a pixel row/col on the right/bottom; acceptable for streaming. */
    uint32_t enc_w = width & ~1u;
    uint32_t enc_h = height & ~1u;

    /* Rebuild encoder + YUV scratch buffer if first use or resolution
   * changed. `yuv_y_stride` is the Y-plane row stride aligned to 16 for
   * encoder-friendly layout; `yuv_uv_stride` covers both U and V planes. */
    if (!server->h264_encoder || server->h264_enc_width != enc_w ||
        server->h264_enc_height != enc_h) {
        if (server->h264_encoder) {
            yetty_yvcodec_encoder_destroy(server->h264_encoder);
            server->h264_encoder = NULL;
        }

        struct yetty_yvcodec_encoder_config cfg;
        yetty_yvcodec_encoder_config_defaults(&cfg, enc_w, enc_h);

        /* Apply user overrides from --vnc-h264-* flags or the vnc/h264/...
     * config keys. Each knob left at zero/-1 keeps the auto default. */
        if (server->h264_cfg_bitrate > 0) {
            cfg.bitrate = server->h264_cfg_bitrate;
        }
        if (server->h264_cfg_framerate > 0.0f) {
            cfg.frame_rate = server->h264_cfg_framerate;
        }
        if (server->h264_cfg_idr_interval > 0) {
            cfg.idr_interval = server->h264_cfg_idr_interval;
        }
        if (server->h264_cfg_screen_content >= 0) {
            cfg.screen_content = server->h264_cfg_screen_content != 0;
        }

        struct yetty_yvcodec_encoder_ptr_result eres =
            yetty_yvcodec_encoder_config_encoder_create(&cfg);
        if (!YETTY_IS_OK(eres)) {
            ywarn("VNC: H.264 encoder create failed: %s", eres.error.msg);
            /* Disable H.264 so the caller falls back to JPEG next frame. */
            server->use_h264 = 0;
            return YETTY_ERR(yetty_ycore_void, eres.error.msg);
        }
        server->h264_encoder = eres.value;

        server->yuv_y_stride = (enc_w + 15) & ~15u;
        server->yuv_uv_stride = (server->yuv_y_stride / 2 + 15) & ~15u;
        size_t y_size = (size_t)server->yuv_y_stride * enc_h;
        size_t uv_size = (size_t)server->yuv_uv_stride * (enc_h / 2);
        size_t need = y_size + uv_size * 2;
        if (need > server->yuv_buf_size) {
            free(server->yuv_buf);
            server->yuv_buf = malloc(need);
            if (!server->yuv_buf) {
                server->yuv_buf_size = 0;
                return YETTY_ERR(yetty_ycore_void, "yuv alloc failed");
            }
            server->yuv_buf_size = need;
        }
        server->h264_enc_width = enc_w;
        server->h264_enc_height = enc_h;
        yinfo("VNC: H.264 encoder %ux%u (from %ux%u source), yuv buf %zu KiB", enc_w, enc_h, width,
              height, need / 1024);
    }

    size_t y_size = (size_t)server->yuv_y_stride * enc_h;
    size_t uv_size = (size_t)server->yuv_uv_stride * (enc_h / 2);
    uint8_t *y_plane = server->yuv_buf;
    uint8_t *u_plane = y_plane + y_size;
    uint8_t *v_plane = u_plane + uv_size;

    yetty_yvcodec_bgra_to_yuv420(pixels, enc_w, enc_h, width * 4, y_plane, u_plane, v_plane,
                                 server->yuv_y_stride, server->yuv_uv_stride);

    struct yetty_yvcodec_encoded_frame encoded;
    struct yetty_ycore_void_result res =
        yetty_yvcodec_encoder_encode(server->h264_encoder, y_plane, u_plane, v_plane,
                                     server->yuv_y_stride, server->yuv_uv_stride, &encoded);
    if (!YETTY_IS_OK(res)) {
        ywarn("VNC: H.264 encode failed: %s", res.error.msg);
        server->use_h264 = 0;
        return res;
    }

    /* Rate-control skip — no bytes this tick, nothing to send. Leave the
   * dirty bitmap as-is; the next submit will retry. */
    if (encoded.size == 0) {
        return YETTY_OK_VOID();
    }

    /* Wire layout: frame header + synthetic tile header (encoding=H264) +
   * video frame header + NALs. The legacy tile header carries the payload
   * size including the inner video header; existing decoders branch on
   * encoding==H264 to parse the inner wrapping. */
    struct yetty_yvnc_vnc_frame_header fhdr = {
        .magic = VNC_FRAME_MAGIC,
        .width = (uint16_t)enc_w,
        .height = (uint16_t)enc_h,
        .tile_size = VNC_TILE_SIZE, /* non-zero: tile-layer parsing */
        .num_tiles = 1,
    };
    struct yetty_yvnc_vnc_tile_header thdr = {
        .tile_x = 0,
        .tile_y = 0,
        .encoding = YETTY_YVNC_VNC_ENCODING_H264,
        .data_size = (uint32_t)(sizeof(struct yetty_yvnc_vnc_video_frame_header) + encoded.size),
    };
    struct yetty_yvnc_vnc_video_frame_header vhdr = {
        .frame_type = encoded.is_idr ? 0u : 1u, /* 0=IDR, 1=P */
        .reserved = {0, 0, 0},
        .timestamp = (uint32_t)(encoded.timestamp_us / 1000u),
        .data_size = (uint32_t)encoded.size,
    };

    /* Per-client frame headers (each carries that client's own seq), then
     * the shared tile/video headers + NAL bytes. We can broadcast the latter
     * three because they're identical across clients — only the leading
     * frame_header differs. */
    double now = yetty_yplatform_ytime_monotonic_sec();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (!c || !c->conn) {
            continue;
        }
        fhdr.seq = c->next_seq;
        send_to_client(server, c, &fhdr, sizeof(fhdr));
        send_to_client(server, c, &thdr, sizeof(thdr));
        send_to_client(server, c, &vhdr, sizeof(vhdr));
        send_to_client(server, c, encoded.data, encoded.size);
        c->awaiting_seq = c->next_seq;
        c->next_seq++;
        c->awaiting_ack = 1;
        c->last_send_time = now;
        c->need_full_frame = 0;
        /* H.264 carries the whole framebuffer; nothing is "owed" any more. */
        if (c->owed_tiles) {
            memset(c->owed_tiles, 0, c->owed_tiles_count);
        }
    }

    /* MP4 recording: write the same NAL bytes to the file. The writer is
   * created lazily on the first frame so it knows the encoder's actual
   * dimensions (which may differ from the source by one row/col after the
   * even-rounding above). */
    if (server->record_mux) {
        if (!server->record_writer_initialized) {
            int err = mp4_h26x_write_init(&server->record_writer, server->record_mux, (int)enc_w,
                                          (int)enc_h, 0);
            if (err != MP4E_STATUS_OK) {
                ywarn("VNC record: mp4_h26x_write_init failed: %d", err);
            } else {
                server->record_writer_initialized = 1;
                yinfo("VNC record: %ux%u -> %s", enc_w, enc_h, server->record_path);
            }
        }
        if (server->record_writer_initialized) {
            uint32_t ts_ms = (uint32_t)(encoded.timestamp_us / 1000u);
            uint32_t dur_ms =
                (server->record_frames == 0) ? 33u : (ts_ms - server->record_prev_ts_ms);
            if (dur_ms == 0) {
                dur_ms = 33u; /* fall back to ~30fps */
            }
            int err = mp4_h26x_write_nal(&server->record_writer, encoded.data, (int)encoded.size,
                                         dur_ms * 90u);
            if (err != MP4E_STATUS_OK) {
                ywarn("VNC record: mp4_h26x_write_nal failed: %d", err);
            } else {
                server->record_prev_ts_ms = ts_ms;
                server->record_frames++;
            }
        }
#ifdef YETTY_HAS_YAUDIO_RECORD
        /* Drain the mic ring into AAC frames on every video-encode
         * tick. Piggybacks on the ~30 Hz video cadence — the 2s
         * capture ring absorbs any occasional stall. */
        record_audio_pump(server);
#endif
    }

    /* H.264 path consumes the entire frame; clear dirty tracking so the
   * tile-mode fallback doesn't re-send the same pixels. */
    uint32_t num_tiles = server->tiles_x * server->tiles_y;
    memset(server->dirty_tiles, 0, num_tiles * sizeof(int));

    server->current_stats.frames++;
    return YETTY_OK_VOID();
}
#endif /* YETTY_HAS_YVCODEC */

/*
 * Encode the current readback and ship to every client that's not currently
 * awaiting an ack. Each client's owed_tiles bitmap accumulates dirty tiles
 * across readbacks, so a slow client doesn't lose updates while it's behind —
 * it just receives fewer, larger flushes. The diff engine's prev-frame
 * texture continues to advance on every readback (engine-frame deltas only;
 * the *client-perceived* delta is whatever bits are still owed at flush time
 * combined with the freshest readback pixels).
 *
 * Flow per call:
 *   1. Time out any client that's been silent past ACK_TIMEOUT_SEC.
 *   2. For each client: ensure owed_tiles is the right size; if need_full_frame
 *      is set, mark every tile owed; OR the new engine bitmap into owed_tiles.
 *   3. H.264 mode: stateful encoder, single shared bitstream — only emit when
 *      every client is unblocked.
 *   4. Otherwise (tile / rect mode): per-client flush of unblocked clients.
 */
static struct yetty_ycore_void_result encode_and_send_dirty_tiles(struct yetty_yvnc_server *server,
                                                                  uint32_t width, uint32_t height)
{
    uint32_t num_tiles = server->tiles_x * server->tiles_y;

    check_ack_timeouts(server);

    uint32_t engine_dirty = 0;
    for (uint32_t i = 0; i < num_tiles; i++) {
        if (server->dirty_tiles[i]) {
            engine_dirty++;
        }
    }
    ydebug("VNC encode_and_send: %ux%u num_tiles=%u engine_dirty=%u clients=%zu", width, height,
           num_tiles, engine_dirty, server->client_count);

    /* Phase 1: fold this readback's dirty bitmap into every client's owed
     * tiles. Blocked clients just accumulate more; unblocked clients pick
     * everything up in phase 3. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (!c || !c->conn) {
            continue;
        }
        struct yetty_ycore_void_result eres = ensure_client_owed(c, num_tiles);
        if (YETTY_IS_ERR(eres)) {
            ywarn("VNC slot %d: %s", c->slot, eres.error.msg);
            yetty_ycore_error_destroy(eres.error);
            continue;
        }
        if (c->need_full_frame) {
            memset(c->owed_tiles, 1, num_tiles);
            c->need_full_frame = 0;
        }
        for (uint32_t t = 0; t < num_tiles; t++) {
            if (server->dirty_tiles[t]) {
                c->owed_tiles[t] = 1;
            }
        }
        uint32_t owed_after = 0;
        for (uint32_t t = 0; t < num_tiles; t++) {
            if (c->owed_tiles[t]) {
                owed_after++;
            }
        }
        ydebug("VNC slot %d: awaiting_ack=%d awaiting_seq=%u next_seq=%u owed=%u", c->slot,
               c->awaiting_ack, c->awaiting_seq, c->next_seq, owed_after);
    }

    /* Phase 2: dirty_tiles has been folded into per-client state — no
     * caller below this point reads it. Clear so the next CPU-path
     * always_full path starts from a clean slate. */
    memset(server->dirty_tiles, 0, num_tiles * sizeof(int));

#ifdef YETTY_HAS_YVCODEC
    /*-------------------------------------------------------------------
     * H.264 streaming mode — one stateful encoder, broadcast to all.
     * Mixing H.264 with JPEG per-tile mid-session would desynchronise the
     * encoder's reference frames. force_raw overrides H.264 (explicit
     * "no compression").
     *
     * Per-client back-pressure: H.264 P-frames reference the encoder's
     * own prior picture, so we can't send a new frame to client A without
     * also sending it to client B (their decoder states must track in
     * lockstep). If any client is awaiting_ack we drop this readback's
     * H.264 frame entirely; the next render will retry when the slowest
     * client has caught up. On encoder failure h264_send_full_frame()
     * clears use_h264 for the rest of the session.
     *-----------------------------------------------------------------*/
    if (server->use_h264 && !server->force_raw) {
        int any_blocked = 0;
        int any_active = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
            if (!c || !c->conn) {
                continue;
            }
            any_active = 1;
            if (c->awaiting_ack) {
                any_blocked = 1;
                break;
            }
        }
        /* If recording but no clients, still encode (record path is its own
         * consumer and doesn't ack). */
        if (any_active && any_blocked) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result res = h264_send_full_frame(server, width, height);
        if (YETTY_IS_OK(res)) {
            return res;
        }
    }
#endif

    /*-------------------------------------------------------------------
     * Per-client flush. Either rect mode (merge owed bits into solid
     * rectangles, one rect per region) or tile mode (one 64×64 tile per
     * owed bit). Each unblocked client's frame_header carries its own
     * seq; the server then waits for an ack matching that seq before
     * shipping the next frame to that client.
     *-----------------------------------------------------------------*/
    double now = yetty_yplatform_ytime_monotonic_sec();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct yetty_yvnc_vnc_client_ctx *c = server->clients[i];
        if (!c || !c->conn) {
            continue;
        }
        if (c->awaiting_ack) {
            continue;
        }

        /* Count owed for this client. */
        uint32_t owed_count = 0;
        for (uint32_t t = 0; t < num_tiles; t++) {
            if (c->owed_tiles[t]) {
                owed_count++;
            }
        }
        if (owed_count == 0) {
            continue;
        }

        if (server->merge_rectangles) {
            struct yetty_yvnc_merged_rect *rects = malloc(num_tiles * sizeof(*rects));
            if (!rects) {
                ywarn("VNC slot %d: rects alloc failed", c->slot);
                continue;
            }
            size_t rect_n = merge_dirty_rects(server, c->owed_tiles, rects, num_tiles);

            struct yetty_yvnc_vnc_frame_header frame_hdr = {
                .magic = VNC_FRAME_MAGIC,
                .width = (uint16_t)width,
                .height = (uint16_t)height,
                .tile_size = 0,
                .num_tiles = (uint16_t)rect_n,
                .seq = c->next_seq,
            };
            send_to_client(server, c, &frame_hdr, sizeof(frame_hdr));

            for (size_t k = 0; k < rect_n; k++) {
                struct yetty_yvnc_merged_rect *r = &rects[k];
                uint8_t *data;
                size_t size;
                uint8_t encoding;
                int free_with_tj;
                struct yetty_ycore_void_result rer = encode_rect(
                    server, r->x, r->y, r->w, r->h, &data, &size, &encoding, &free_with_tj);
                if (!YETTY_IS_OK(rer)) {
                    continue;
                }
                struct yetty_yvnc_vnc_rect_header rh = {
                    .px_x = r->x,
                    .px_y = r->y,
                    .width = r->w,
                    .height = r->h,
                    .encoding = encoding,
                    .reserved = 0,
                    .data_size = (uint32_t)size,
                };
                send_to_client(server, c, &rh, sizeof(rh));
                send_to_client(server, c, data, size);
                if (free_with_tj) {
                    tjFree(data);
                } else {
                    free(data);
                }
            }
            free(rects);
        } else {
            struct yetty_yvnc_vnc_frame_header frame_hdr = {
                .magic = VNC_FRAME_MAGIC,
                .width = (uint16_t)width,
                .height = (uint16_t)height,
                .tile_size = VNC_TILE_SIZE,
                .num_tiles = (uint16_t)owed_count,
                .seq = c->next_seq,
            };
            send_to_client(server, c, &frame_hdr, sizeof(frame_hdr));

            for (uint16_t ty = 0; ty < server->tiles_y; ty++) {
                for (uint16_t tx = 0; tx < server->tiles_x; tx++) {
                    uint32_t idx = (uint32_t)ty * server->tiles_x + tx;
                    if (!c->owed_tiles[idx]) {
                        continue;
                    }
                    uint8_t *tile_data;
                    size_t tile_size;
                    uint8_t encoding;
                    struct yetty_ycore_void_result ter =
                        encode_tile(server, tx, ty, &tile_data, &tile_size, &encoding);
                    if (!YETTY_IS_OK(ter)) {
                        continue;
                    }
                    struct yetty_yvnc_vnc_tile_header tile_hdr = {
                        .tile_x = tx,
                        .tile_y = ty,
                        .encoding = encoding,
                        .data_size = (uint32_t)tile_size,
                    };
                    send_to_client(server, c, &tile_hdr, sizeof(tile_hdr));
                    send_to_client(server, c, tile_data, tile_size);
                }
            }
        }

        /* Mark everything sent: client now owns these pixels and won't get
         * another frame until it acks `awaiting_seq`. */
        memset(c->owed_tiles, 0, num_tiles);
        c->awaiting_seq = c->next_seq;
        c->next_seq++;
        c->awaiting_ack = 1;
        c->last_send_time = now;
        server->current_stats.frames++;
        ydebug("VNC slot %d: SENT seq=%u tiles=%u, now awaiting", c->slot, c->awaiting_seq,
               owed_count);
    }

    return YETTY_OK_VOID();
}

/*
 * Invoked by the tile-diff engine on the loop thread after a submit has
 * completed and at least one subsequent submit was dropped for back-
 * pressure. Requests another render so the engine can catch up with the
 * latest render-target texture content.
 */
static void vnc_on_engine_idle(void *ctx)
{
    struct yetty_yvnc_server *server = ctx;
    if (server && server->event_loop && server->event_loop->ops->request_render) {
        server->event_loop->ops->request_render(server->event_loop);
    }
}

/*
 * Sink callback invoked by the tile-diff engine once the GPU diff + readback
 * have completed. `frame->pixels` is a row-aligned mapped range that's only
 * valid until this function returns, so we pack it into gpu_readback_pixels
 * (flat width*4 stride) and then defer to encode_and_send_dirty_tiles which
 * uses the existing encode_tile machinery.
 */
YETTY_EXTERNAL_CALLBACK
static void vnc_tile_diff_sink(void *ctx, const struct yetty_yrender_utils_tile_diff_frame *frame)
{
    struct yetty_yvnc_server *server = ctx;

    struct yetty_ycore_void_result res = ensure_cpu_state(server, frame->width, frame->height);
    if (!YETTY_IS_OK(res)) {
        ywarn("vnc sink: ensure_cpu_state failed: %s", res.error.msg);
        return;
    }

    /* Use GPU-readback pixels for encode_tile (cpu_pixels is the other
   * input path; clear it so encode_tile picks the readback). */
    server->cpu_pixels = NULL;

    size_t pixel_size = (size_t)frame->width * frame->height * 4;
    if (server->gpu_readback_pixels_size < pixel_size) {
        free(server->gpu_readback_pixels);
        server->gpu_readback_pixels = malloc(pixel_size);
        if (!server->gpu_readback_pixels) {
            ywarn("vnc sink: failed to allocate readback buffer");
            return;
        }
        server->gpu_readback_pixels_size = pixel_size;
    }

    /* Pack the aligned mapped pixels into a width*4-stride buffer so
   * encode_tile can use last_width*4 as the row pitch. */
    uint32_t packed_row = frame->width * 4;
    for (uint32_t y = 0; y < frame->height; y++) {
        memcpy(server->gpu_readback_pixels + y * packed_row,
               frame->pixels + y * frame->aligned_bytes_per_row, packed_row);
    }

    /* Translate the engine's dirty bitmap into the int-sized dirty_tiles
   * array the wire encoder expects. */
    uint32_t num_tiles = server->tiles_x * server->tiles_y;
    if ((uint32_t)(frame->tiles_x * frame->tiles_y) != num_tiles) {
        ywarn("vnc sink: tile count mismatch engine=%ux%u server=%ux%u", frame->tiles_x,
              frame->tiles_y, server->tiles_x, server->tiles_y);
        return;
    }
    for (uint32_t i = 0; i < num_tiles; i++) {
        server->dirty_tiles[i] = frame->dirty_bitmap[i] ? 1 : 0;
    }

    res = encode_and_send_dirty_tiles(server, frame->width, frame->height);
    if (!YETTY_IS_OK(res)) {
        ywarn("vnc sink: encode_and_send failed: %s", res.error.msg);
    }
}

struct yetty_ycore_void_result yetty_yvnc_server_send_frame_gpu(struct yetty_yvnc_server *server,
                                                                WGPUTexture texture, uint32_t width,
                                                                uint32_t height)
{
#ifdef YETTY_HAS_YVCODEC
    int has_consumers =
        server && server->running && (server->client_count > 0 || server->record_mux != NULL);
#else
    int has_consumers = server && server->running && server->client_count > 0;
#endif
    if (!has_consumers) {
        return YETTY_OK_VOID();
    }

    if (!server->diff_engine) {
        struct yetty_yrender_utils_tile_diff_engine_ptr_result eng_res =
            yetty_yrender_utils_tile_diff_engine_create(server->device, server->queue, server->wgpu,
                                                        VNC_TILE_SIZE);
        if (!YETTY_IS_OK(eng_res)) {
            return YETTY_ERR(yetty_ycore_void, eng_res.error.msg);
        }
        server->diff_engine = eng_res.value;

        /* Engine back-pressure: if a second submit arrives while the first
     * is still reading back, it's dropped to avoid racing on the shared
     * GPU buffers. The engine fires this callback on the loop thread
     * once it's idle; we ask for another render so the catch-up frame
     * ships. Without this, a burst of terminal output (nvim initial
     * draw, `find /nix` storm) would stall visibly until the next
     * unrelated event nudged the render loop. */
        yetty_yrender_utils_tile_diff_engine_set_on_idle(server->diff_engine, vnc_on_engine_idle,
                                                         server);
    }

    /* Propagate VNC-level full-frame requests (e.g. new client) to the
   * engine so the next submit marks all tiles dirty. */
    if (server->force_full_frame) {
        server->force_full_frame = 0;
        yetty_yrender_utils_tile_diff_engine_force_full(server->diff_engine);
    }
    yetty_yrender_utils_tile_diff_engine_set_always_full(server->diff_engine,
                                                         server->always_full_frame != 0);

    return yetty_yrender_utils_tile_diff_engine_submit(server->diff_engine, texture, width, height,
                                                       vnc_tile_diff_sink, server);
}

struct yetty_ycore_void_result yetty_yvnc_server_send_frame(struct yetty_yvnc_server *server,
                                                            WGPUTexture texture,
                                                            const uint8_t *cpu_pixels,
                                                            uint32_t width, uint32_t height)
{
    (void)texture;
    /* Use CPU path for now */
    return yetty_yvnc_server_send_frame_cpu(server, cpu_pixels, width, height);
}

struct yetty_yvnc_server_stats yetty_yvnc_server_get_stats(const struct yetty_yvnc_server *server)
{
    if (server) {
        return server->stats;
    }
    struct yetty_yvnc_server_stats empty = {0};
    return empty;
}
