/*
 * vnc-client.c - VNC client using libuv TCP via event loop
 */

#include <yetty/yvnc/vnc-client.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/yplatform/time.h>
#include <yetty/ytrace.h>
#include "protocol.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

#ifdef YETTY_HAS_YVIDEO
#include <yetty/yvideo/decoder.h>
#endif

#define RECV_BUFFER_SIZE 65536

/* Recv state machine */
enum yetty_yvnc_recv_state {
    YETTY_YVNC_RECV_FRAME_HEADER,
    YETTY_YVNC_RECV_TILE_HEADER,
    YETTY_YVNC_RECV_TILE_DATA,
    YETTY_YVNC_RECV_RECT_HEADER,
    YETTY_YVNC_RECV_RECT_DATA
};

/* Fullscreen quad shader */
static const char *QUAD_SHADER = "\
struct VertexOutput {\n\
    @builtin(position) position: vec4<f32>,\n\
    @location(0) uv: vec2<f32>,\n\
};\n\
\n\
@vertex\n\
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {\n\
    var positions = array<vec2<f32>, 6>(\n\
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),\n\
        vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)\n\
    );\n\
    var uvs = array<vec2<f32>, 6>(\n\
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0),\n\
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(1.0, 0.0)\n\
    );\n\
    var out: VertexOutput;\n\
    out.position = vec4(positions[idx], 0.0, 1.0);\n\
    out.uv = uvs[idx];\n\
    return out;\n\
}\n\
\n\
@group(0) @binding(0) var frame_texture: texture_2d<f32>;\n\
@group(0) @binding(1) var frame_sampler: sampler;\n\
\n\
@fragment\n\
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {\n\
    let tex = textureSample(frame_texture, frame_sampler, in.uv);\n\
    if (tex.a < 0.01) {\n\
        return vec4<f32>(1.0, 0.0, 1.0, 1.0);\n\
    }\n\
    return tex;\n\
}\n";

struct yetty_yvnc_client {
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;

    /* Event loop for async I/O */
    struct yetty_yplatform_event_loop *event_loop;
    yetty_ycore_tcp_client_id tcp_client_id;
    struct yetty_ycore_conn *conn;

    /* Connection state */
    int connected;
    int wants_reconnect;

    /* Reconnection */
    char *reconnect_host;
    uint16_t reconnect_port;

    /* Frame state */
    uint16_t width;
    uint16_t height;

    /* Recv state machine */
    enum yetty_yvnc_recv_state recv_state;
    uint8_t *recv_buffer;
    size_t recv_buffer_capacity;
    size_t recv_offset;
    size_t recv_needed;

    /* Current frame/tile/rect being received */
    struct yetty_yvnc_vnc_frame_header current_frame;
    struct yetty_yvnc_vnc_tile_header current_tile;
    struct yetty_yvnc_vnc_rect_header current_rect;
    uint16_t tiles_received;

    /* GPU resources */
    WGPUTexture texture;
    uint16_t texture_width;
    uint16_t texture_height;
    WGPUTextureView texture_view;
    WGPUSampler sampler;
    WGPUBindGroup bind_group;
    WGPUBindGroupLayout bind_group_layout;
    WGPURenderPipeline pipeline;

    /* JPEG decompressor */
    tjhandle jpeg_decompressor;

    /* Tile pixel buffer */
    uint8_t *tile_pixels;

#ifdef YETTY_HAS_YVIDEO
    /* H.264 decoder — lazily created the first time we see an H.264 tile.
	 * Also a reusable BGRA buffer for the YUV→BGRA conversion output. */
    struct yetty_yvideo_decoder *h264_decoder;
    uint8_t *h264_bgra_buf;
    size_t h264_bgra_buf_size;
#endif

    /* Callbacks */
    yetty_vnc_on_frame_fn on_frame_fn;
    void *on_frame_userdata;
    yetty_vnc_on_connected_fn on_connected_fn;
    void *on_connected_userdata;
    yetty_vnc_on_disconnected_fn on_disconnected_fn;
    void *on_disconnected_userdata;

    /* Stats */
    struct yetty_yvnc_client_stats stats;
    uint64_t stats_bytes_window;
    uint32_t stats_frames_window;
    uint32_t stats_tiles_window;
    double stats_window_start;
};

static struct yetty_ycore_void_result ensure_resources(struct yetty_yvnc_client *client,
                                                       uint16_t width, uint16_t height);
static struct yetty_ycore_void_result create_pipeline(struct yetty_yvnc_client *client);

/*===========================================================================
 * Send helper
 *===========================================================================*/

static void send_input(struct yetty_yvnc_client *client, const void *data, size_t size)
{
    if (!client || !client->connected || !client->conn) {
        return;
    }

    client->event_loop->ops->tcp_send(client->conn, data, size);
}

/*===========================================================================
 * Frame processing
 *===========================================================================*/

static struct yetty_ycore_void_result process_tile_data(struct yetty_yvnc_client *client)
{
    ydebug("vnc_client: tile (%u,%u) encoding=%u size=%u", client->current_tile.tile_x,
           client->current_tile.tile_y, client->current_tile.encoding,
           client->current_tile.data_size);

    memset(client->tile_pixels, 0, VNC_TILE_SIZE * VNC_TILE_SIZE * 4);

    switch (client->current_tile.encoding) {
    case YETTY_YVNC_VNC_ENCODING_RAW:
        ydebug("vnc_client: decoding RAW tile");
        if (client->current_tile.data_size == VNC_TILE_SIZE * VNC_TILE_SIZE * 4) {
            memcpy(client->tile_pixels, client->recv_buffer, client->current_tile.data_size);
        }
        break;

    case YETTY_YVNC_VNC_ENCODING_JPEG: {
        ydebug("vnc_client: decoding JPEG tile");
        int w, h, subsamp, cs;
        if (tjDecompressHeader3(client->jpeg_decompressor, client->recv_buffer,
                                client->current_tile.data_size, &w, &h, &subsamp, &cs) == 0) {
            if (w <= VNC_TILE_SIZE && h <= VNC_TILE_SIZE) {
                tjDecompress2(client->jpeg_decompressor, client->recv_buffer,
                              client->current_tile.data_size, client->tile_pixels, VNC_TILE_SIZE, 0,
                              VNC_TILE_SIZE, TJPF_BGRA, TJFLAG_FASTDCT);
            }
        }
        break;
    }

    case YETTY_YVNC_VNC_ENCODING_FULL_FRAME: {
        int w, h, subsamp, cs;
        if (tjDecompressHeader3(client->jpeg_decompressor, client->recv_buffer,
                                client->current_tile.data_size, &w, &h, &subsamp, &cs) == 0) {
            if ((uint16_t)w == client->width && (uint16_t)h == client->height) {
                uint8_t *full_frame = malloc(client->width * client->height * 4);
                if (full_frame) {
                    tjDecompress2(client->jpeg_decompressor, client->recv_buffer,
                                  client->current_tile.data_size, full_frame, client->width, 0,
                                  client->height, TJPF_BGRA, TJFLAG_FASTDCT);

                    if (client->texture) {
                        WGPUTexelCopyTextureInfo dst = {0};
                        dst.texture = client->texture;
                        dst.origin = (WGPUOrigin3D){0, 0, 0};

                        WGPUTexelCopyBufferLayout layout = {0};
                        layout.bytesPerRow = client->width * 4;
                        layout.rowsPerImage = client->height;

                        WGPUExtent3D size = {client->width, client->height, 1};
                        wgpuQueueWriteTexture(client->queue, &dst, full_frame,
                                              client->width * client->height * 4, &layout, &size);
                    }
                    free(full_frame);
                }
            }
        }
        client->tiles_received++;
        client->stats_tiles_window++;
        if (client->tiles_received >= client->current_frame.num_tiles) {
            client->stats_frames_window++;
            client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
            client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
            struct yetty_ycore_void_result ar = yetty_yvnc_client_send_frame_ack(client);
            if (YETTY_IS_ERR(ar)) {
                yetty_ycore_error_destroy(ar.error);
            }
        }
        return YETTY_OK_VOID();
    }

#ifdef YETTY_HAS_YVIDEO
    case YETTY_YVNC_VNC_ENCODING_H264: {
        /*
		 * Wire layout (produced by h264_send_full_frame on the server):
		 *     [vnc_video_frame_header] [NAL bytes ...]
		 * The enclosing tile header's data_size counts both parts; the
		 * inner data_size is the NAL-only size.
		 */
        if (client->current_tile.data_size < sizeof(struct yetty_yvnc_vnc_video_frame_header)) {
            ywarn("vnc_client: H.264 tile too small (%u bytes)", client->current_tile.data_size);
            break;
        }
        const struct yetty_yvnc_vnc_video_frame_header *vhdr =
            (const struct yetty_yvnc_vnc_video_frame_header *)client->recv_buffer;
        const uint8_t *nal_data = client->recv_buffer + sizeof(struct yetty_yvnc_vnc_video_frame_header);
        size_t nal_size = client->current_tile.data_size - sizeof(struct yetty_yvnc_vnc_video_frame_header);

        if (vhdr->data_size != nal_size) {
            ywarn("vnc_client: H.264 inner data_size mismatch (hdr=%u, avail=%zu)", vhdr->data_size,
                  nal_size);
            break;
        }

        if (!client->h264_decoder) {
            struct yetty_yvideo_decoder_ptr_result dres = yetty_yvideo_decoder_create_h264();
            if (!YETTY_IS_OK(dres)) {
                ywarn("vnc_client: H.264 decoder create failed: %s", dres.error.msg);
                yetty_ycore_error_destroy(dres.error);
                break;
            }
            client->h264_decoder = dres.value;
        }

        struct yetty_ycore_void_result fres =
            yetty_yvideo_decoder_feed(client->h264_decoder, nal_data, nal_size);
        if (!YETTY_IS_OK(fres)) {
            ywarn("vnc_client: H.264 feed failed: %s", fres.error.msg);
            yetty_ycore_error_destroy(fres.error);
            break;
        }

        struct yetty_yvideo_yuv_frame yuv;
        bool got_frame = false;
        fres = yetty_yvideo_decoder_get_frame(client->h264_decoder, &yuv, &got_frame);
        if (!YETTY_IS_OK(fres)) {
            ywarn("vnc_client: H.264 decode failed: %s", fres.error.msg);
            yetty_ycore_error_destroy(fres.error);
            break;
        }
        if (!got_frame) {
            /* Reordering / SPS-PPS-only NAL / partial frame — no pixels
			 * yet. Still count the tile so the frame ack fires. */
            break;
        }

        /* Convert to BGRA and upload. Reuse a persistent buffer so we
		 * don't malloc/free a full framebuffer every frame. */
        size_t bgra_size = (size_t)yuv.width * yuv.height * 4;
        if (bgra_size > client->h264_bgra_buf_size) {
            free(client->h264_bgra_buf);
            client->h264_bgra_buf = malloc(bgra_size);
            if (!client->h264_bgra_buf) {
                client->h264_bgra_buf_size = 0;
                ywarn("vnc_client: H.264 BGRA alloc failed (%zu bytes)", bgra_size);
                break;
            }
            client->h264_bgra_buf_size = bgra_size;
        }
        yetty_yvideo_yuv_frame_yuv420_to_bgra(&yuv, client->h264_bgra_buf);

        if (client->texture && (uint16_t)yuv.width == client->width &&
            (uint16_t)yuv.height == client->height) {
            WGPUTexelCopyTextureInfo dst = {0};
            dst.texture = client->texture;
            dst.origin = (WGPUOrigin3D){0, 0, 0};

            WGPUTexelCopyBufferLayout layout = {0};
            layout.bytesPerRow = yuv.width * 4;
            layout.rowsPerImage = yuv.height;

            WGPUExtent3D size = {yuv.width, yuv.height, 1};
            wgpuQueueWriteTexture(client->queue, &dst, client->h264_bgra_buf, bgra_size, &layout,
                                  &size);
        }

        client->tiles_received++;
        client->stats_tiles_window++;
        if (client->tiles_received >= client->current_frame.num_tiles) {
            client->stats_frames_window++;
            client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
            client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
            struct yetty_ycore_void_result ar = yetty_yvnc_client_send_frame_ack(client);
            if (YETTY_IS_ERR(ar)) {
                yetty_ycore_error_destroy(ar.error);
            }
        }
        return YETTY_OK_VOID();
    }
#endif

    default:
        break;
    }

    /* Upload tile to GPU */
    if (client->texture && client->width > 0 && client->height > 0) {
        uint32_t px = client->current_tile.tile_x * VNC_TILE_SIZE;
        uint32_t py = client->current_tile.tile_y * VNC_TILE_SIZE;
        if (px < client->width && py < client->height) {
            uint32_t tw = VNC_TILE_SIZE;
            uint32_t th = VNC_TILE_SIZE;
            if (px + tw > client->width) {
                tw = client->width - px;
            }
            if (py + th > client->height) {
                th = client->height - py;
            }

            ydebug("vnc_client: uploading tile to GPU at (%u,%u) size %ux%u", px, py, tw, th);

            WGPUTexelCopyTextureInfo dst = {0};
            dst.texture = client->texture;
            dst.origin = (WGPUOrigin3D){px, py, 0};

            WGPUTexelCopyBufferLayout layout = {0};
            layout.bytesPerRow = VNC_TILE_SIZE * 4;
            layout.rowsPerImage = VNC_TILE_SIZE;

            WGPUExtent3D size = {tw, th, 1};
            wgpuQueueWriteTexture(client->queue, &dst, client->tile_pixels,
                                  VNC_TILE_SIZE * VNC_TILE_SIZE * 4, &layout, &size);
        }
    }

    client->tiles_received++;
    client->stats_tiles_window++;

    /* State transition - buffer management handled by caller */
    if (client->tiles_received >= client->current_frame.num_tiles) {
        client->stats_frames_window++;
        client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
        client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
        struct yetty_ycore_void_result ar = yetty_yvnc_client_send_frame_ack(client);
        if (YETTY_IS_ERR(ar)) {
            yetty_ycore_error_destroy(ar.error);
        }
    } else {
        client->recv_state = YETTY_YVNC_RECV_TILE_HEADER;
        client->recv_needed = sizeof(struct yetty_yvnc_vnc_tile_header);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result process_rect_data(struct yetty_yvnc_client *client)
{
    uint16_t px = client->current_rect.px_x;
    uint16_t py = client->current_rect.px_y;
    uint16_t rw = client->current_rect.width;
    uint16_t rh = client->current_rect.height;

    uint8_t *pixels = malloc(rw * rh * 4);
    if (!pixels) {
        return YETTY_ERR(yetty_ycore_void, "process_rect_data: alloc failed");
    }

    memset(pixels, 0, rw * rh * 4);

    switch (client->current_rect.encoding) {
    case YETTY_YVNC_VNC_ENCODING_RAW:
        if (client->current_rect.data_size == (uint32_t)(rw * rh * 4)) {
            memcpy(pixels, client->recv_buffer, client->current_rect.data_size);
        }
        break;

    case YETTY_YVNC_VNC_ENCODING_JPEG: {
        int w, h, subsamp, cs;
        if (tjDecompressHeader3(client->jpeg_decompressor, client->recv_buffer,
                                client->current_rect.data_size, &w, &h, &subsamp, &cs) == 0) {
            if (w == rw && h == rh) {
                tjDecompress2(client->jpeg_decompressor, client->recv_buffer,
                              client->current_rect.data_size, pixels, rw, 0, rh, TJPF_BGRA,
                              TJFLAG_FASTDCT);
            }
        }
        break;
    }

    default:
        break;
    }

    /* Upload to GPU */
    if (client->texture && client->width > 0 && client->height > 0) {
        if (px < client->width && py < client->height) {
            uint32_t uw = rw;
            uint32_t uh = rh;
            if (px + uw > client->width) {
                uw = client->width - px;
            }
            if (py + uh > client->height) {
                uh = client->height - py;
            }

            WGPUTexelCopyTextureInfo dst = {0};
            dst.texture = client->texture;
            dst.origin = (WGPUOrigin3D){px, py, 0};

            WGPUTexelCopyBufferLayout layout = {0};
            layout.bytesPerRow = rw * 4;
            layout.rowsPerImage = rh;

            WGPUExtent3D size = {uw, uh, 1};
            wgpuQueueWriteTexture(client->queue, &dst, pixels, rw * rh * 4, &layout, &size);
        }
    }

    free(pixels);

    client->tiles_received++;
    client->stats_tiles_window++;

    if (client->tiles_received >= client->current_frame.num_tiles) {
        client->stats_frames_window++;
        client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
        client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
        client->recv_offset = 0;
        struct yetty_ycore_void_result ar = yetty_yvnc_client_send_frame_ack(client);
        if (YETTY_IS_ERR(ar)) {
            yetty_ycore_error_destroy(ar.error);
        }
    } else {
        client->recv_state = YETTY_YVNC_RECV_RECT_HEADER;
        client->recv_needed = sizeof(struct yetty_yvnc_vnc_rect_header);
        client->recv_offset = 0;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result process_received_data(struct yetty_yvnc_client *client)
{
    int tiles_received = 0;

    while (client->recv_offset >= client->recv_needed) {
        switch (client->recv_state) {
        case YETTY_YVNC_RECV_FRAME_HEADER: {
            memcpy(&client->current_frame, client->recv_buffer, sizeof(struct yetty_yvnc_vnc_frame_header));

            if (client->current_frame.magic != VNC_FRAME_MAGIC) {
                ywarn("VNC client: invalid frame magic 0x%08X", client->current_frame.magic);
                client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
                client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
                client->recv_offset = 0;
                return YETTY_ERR(yetty_ycore_void, "vnc_client: protocol error or invalid frame");
            }

            if (client->current_frame.width == 0 || client->current_frame.height == 0 ||
                client->current_frame.width > 8192 || client->current_frame.height > 8192) {
                ywarn("VNC client: invalid frame dimensions %ux%u", client->current_frame.width,
                      client->current_frame.height);
                client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
                client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
                client->recv_offset = 0;
                return YETTY_ERR(yetty_ycore_void, "vnc_client: protocol error or invalid frame");
            }

            ydebug("VNC client: frame %ux%u tiles=%u", client->current_frame.width,
                   client->current_frame.height, client->current_frame.num_tiles);

            if (client->current_frame.width != client->width ||
                client->current_frame.height != client->height) {
                client->width = client->current_frame.width;
                client->height = client->current_frame.height;
            }

            ensure_resources(client, client->width, client->height);
            client->tiles_received = 0;

            /* Consume header and shift remaining data */
            size_t consumed = sizeof(struct yetty_yvnc_vnc_frame_header);
            size_t remaining = client->recv_offset - consumed;
            if (remaining > 0) {
                memmove(client->recv_buffer, client->recv_buffer + consumed, remaining);
            }
            client->recv_offset = remaining;

            if (client->current_frame.num_tiles == 0) {
                client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
            } else {
                client->recv_state = YETTY_YVNC_RECV_TILE_HEADER;
                client->recv_needed = sizeof(struct yetty_yvnc_vnc_tile_header);
            }
            break;
        }

        case YETTY_YVNC_RECV_TILE_HEADER: {
            memcpy(&client->current_tile, client->recv_buffer, sizeof(struct yetty_yvnc_vnc_tile_header));

            if (client->current_tile.data_size > 16 * 1024 * 1024) {
                ywarn("VNC client: tile data too large %u", client->current_tile.data_size);
                client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
                client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
                client->recv_offset = 0;
                return YETTY_ERR(yetty_ycore_void, "vnc_client: protocol error or invalid frame");
            }

            size_t consumed = sizeof(struct yetty_yvnc_vnc_tile_header);
            size_t remaining = client->recv_offset - consumed;
            if (remaining > 0) {
                memmove(client->recv_buffer, client->recv_buffer + consumed, remaining);
            }
            client->recv_offset = remaining;

            client->recv_state = YETTY_YVNC_RECV_TILE_DATA;
            client->recv_needed = client->current_tile.data_size;
            break;
        }

        case YETTY_YVNC_RECV_TILE_DATA: {
            { struct yetty_ycore_void_result _ptr = process_tile_data(client); if (YETTY_IS_ERR(_ptr)) yetty_ycore_error_destroy(_ptr.error); }
            tiles_received = 1;

            /* Consume tile data and shift remaining */
            size_t consumed = client->current_tile.data_size;
            size_t remaining = client->recv_offset - consumed;
            if (remaining > 0) {
                memmove(client->recv_buffer, client->recv_buffer + consumed, remaining);
            }
            client->recv_offset = remaining;
            break;
        }

        case YETTY_YVNC_RECV_RECT_HEADER: {
            memcpy(&client->current_rect, client->recv_buffer, sizeof(struct yetty_yvnc_vnc_rect_header));

            if (client->current_rect.data_size > 16 * 1024 * 1024) {
                ywarn("VNC client: rect data too large %u", client->current_rect.data_size);
                client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
                client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
                client->recv_offset = 0;
                return YETTY_ERR(yetty_ycore_void, "vnc_client: protocol error or invalid frame");
            }

            size_t consumed = sizeof(struct yetty_yvnc_vnc_rect_header);
            size_t remaining = client->recv_offset - consumed;
            if (remaining > 0) {
                memmove(client->recv_buffer, client->recv_buffer + consumed, remaining);
            }
            client->recv_offset = remaining;

            client->recv_state = YETTY_YVNC_RECV_RECT_DATA;
            client->recv_needed = client->current_rect.data_size;
            break;
        }

        case YETTY_YVNC_RECV_RECT_DATA: {
            { struct yetty_ycore_void_result _prr = process_rect_data(client); if (YETTY_IS_ERR(_prr)) yetty_ycore_error_destroy(_prr.error); }
            tiles_received = 1;

            size_t consumed = client->current_rect.data_size;
            size_t remaining = client->recv_offset - consumed;
            if (remaining > 0) {
                memmove(client->recv_buffer, client->recv_buffer + consumed, remaining);
            }
            client->recv_offset = remaining;
            break;
        }
        }
    }

    if (tiles_received && client->on_frame_fn) {
        client->on_frame_fn(client->on_frame_userdata);
    }
    return YETTY_OK_VOID();

}

/*===========================================================================
 * TCP Client Callbacks
 *===========================================================================*/

static void vnc_client_on_connect(void *ctx, struct yetty_ycore_conn *conn)
{
    struct yetty_yvnc_client *client = ctx;
    ydebug("VNC client: on_connect callback fired, conn=%p", (void *)conn);
    client->conn = conn;
    client->connected = 1;

    yinfo("VNC client connected");

    if (client->on_connected_fn) {
        ydebug("VNC client: calling on_connected_fn");
        client->on_connected_fn(client->on_connected_userdata);
    }
}

static void vnc_client_on_connect_error(void *ctx, const char *error)
{
    struct yetty_yvnc_client *client = ctx;
    ydebug("VNC client: on_connect_error callback fired, error=%s", error);

    yerror("VNC client connect failed: %s", error);
    client->connected = 0;
    client->conn = NULL;
    client->tcp_client_id = -1;
    client->wants_reconnect = 1;

    if (client->on_disconnected_fn) {
        ydebug("VNC client: calling on_disconnected_fn (from connect error)");
        client->on_disconnected_fn(client->on_disconnected_userdata);
    }
}

static void vnc_client_on_alloc(void *ctx, size_t suggested, char **buf, size_t *len)
{
    struct yetty_yvnc_client *client = ctx;
    (void)suggested;

    /* Ensure buffer capacity */
    size_t need = client->recv_offset + suggested;
    if (need > client->recv_buffer_capacity) {
        size_t new_cap = need * 2;
        if (new_cap < 64 * 1024) {
            new_cap = 64 * 1024;
        }
        uint8_t *new_buf = realloc(client->recv_buffer, new_cap);
        if (new_buf) {
            client->recv_buffer = new_buf;
            client->recv_buffer_capacity = new_cap;
        }
    }

    *buf = (char *)client->recv_buffer + client->recv_offset;
    *len = client->recv_buffer_capacity - client->recv_offset;
}

YETTY_EXTERNAL_CALLBACK
static void vnc_client_on_data(void *ctx, struct yetty_ycore_conn *conn, const char *data, long nread)
{
    struct yetty_yvnc_client *client = ctx;
    (void)conn;
    (void)data; /* Data already in recv_buffer from on_alloc */

    ydebug("VNC client: on_data callback, nread=%ld", nread);

    if (nread <= 0) {
        return;
    }

    client->recv_offset += (size_t)nread;
    client->stats_bytes_window += (size_t)nread;

    ydebug("VNC client: processing data, recv_offset=%zu, recv_needed=%zu", client->recv_offset,
           client->recv_needed);
    struct yetty_ycore_void_result pr = process_received_data(client);
    if (YETTY_IS_ERR(pr)) {
        yetty_ycore_error_destroy(pr.error);
    }
}

static void vnc_client_on_disconnect(void *ctx)
{
    struct yetty_yvnc_client *client = ctx;

    yinfo("VNC client disconnected");

    client->connected = 0;
    client->conn = NULL;
    client->tcp_client_id = -1;
    client->wants_reconnect = 1;

    if (client->on_disconnected_fn) {
        client->on_disconnected_fn(client->on_disconnected_userdata);
    }
}

/*===========================================================================
 * Public API
 *===========================================================================*/

struct yetty_vnc_client_ptr_result yetty_yvnc_client_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_yplatform_event_loop *event_loop, uint16_t width, uint16_t height)
{
    ydebug("VNC client: create called, device=%p queue=%p event_loop=%p %ux%u", (void *)device,
           (void *)queue, (void *)event_loop, width, height);

    if (!event_loop) {
        return YETTY_ERR(yetty_vnc_client_ptr, "event_loop is NULL");
    }

    struct yetty_yvnc_client *client = calloc(1, sizeof(struct yetty_yvnc_client));
    if (!client) {
        return YETTY_ERR(yetty_vnc_client_ptr, "failed to allocate client");
    }

    client->device = device;
    client->queue = queue;
    client->surface_format = surface_format;
    client->event_loop = event_loop;
    client->tcp_client_id = -1;
    client->width = width;
    client->height = height;

    /* Initialize recv state */
    client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
    client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
    client->recv_buffer_capacity = RECV_BUFFER_SIZE;
    client->recv_buffer = malloc(client->recv_buffer_capacity);
    if (!client->recv_buffer) {
        free(client);
        return YETTY_ERR(yetty_vnc_client_ptr, "failed to allocate recv buffer");
    }

    /* Initialize JPEG decompressor */
    client->jpeg_decompressor = tjInitDecompress();
    if (!client->jpeg_decompressor) {
        free(client->recv_buffer);
        free(client);
        return YETTY_ERR(yetty_vnc_client_ptr, "failed to init jpeg decompressor");
    }

    /* Tile pixel buffer */
    client->tile_pixels = malloc(VNC_TILE_SIZE * VNC_TILE_SIZE * 4);
    if (!client->tile_pixels) {
        tjDestroy(client->jpeg_decompressor);
        free(client->recv_buffer);
        free(client);
        return YETTY_ERR(yetty_vnc_client_ptr, "failed to allocate tile pixels");
    }

    client->stats_window_start = yetty_yplatform_ytime_monotonic_sec();

    /* Create initial GPU resources */
    if (width > 0 && height > 0) {
        struct yetty_ycore_void_result res = ensure_resources(client, width, height);
        if (!YETTY_IS_OK(res)) {
            tjDestroy(client->jpeg_decompressor);
            free(client->tile_pixels);
            free(client->recv_buffer);
            free(client);
            return YETTY_ERR(yetty_vnc_client_ptr, "failed to create GPU resources");
        }
    }

    ydebug("VNC client: create complete, client=%p", (void *)client);
    return YETTY_OK(yetty_vnc_client_ptr, client);
}

struct yetty_ycore_void_result yetty_yvnc_client_destroy(struct yetty_yvnc_client *client)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "yetty_vnc_client_destroy: NULL client");
    }

    struct yetty_ycore_void_result disc_r = yetty_yvnc_client_disconnect(client);

    if (client->jpeg_decompressor) {
        tjDestroy(client->jpeg_decompressor);
    }

#ifdef YETTY_HAS_YVIDEO
    if (client->h264_decoder) {
        yetty_yvideo_decoder_destroy(client->h264_decoder);
    }
    free(client->h264_bgra_buf);
#endif

    if (client->pipeline) {
        wgpuRenderPipelineRelease(client->pipeline);
    }
    if (client->bind_group) {
        wgpuBindGroupRelease(client->bind_group);
    }
    if (client->bind_group_layout) {
        wgpuBindGroupLayoutRelease(client->bind_group_layout);
    }
    if (client->sampler) {
        wgpuSamplerRelease(client->sampler);
    }
    if (client->texture_view) {
        wgpuTextureViewRelease(client->texture_view);
    }
    if (client->texture) {
        wgpuTextureRelease(client->texture);
    }

    free(client->reconnect_host);
    free(client->tile_pixels);
    free(client->recv_buffer);
    free(client);

    if (YETTY_IS_ERR(disc_r)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_vnc_client_destroy: disconnect failed", disc_r);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_connect(struct yetty_yvnc_client *client,
                                                        const char *host, uint16_t port)
{
    ydebug("VNC client: connect called, host=%s port=%u", host, port);

    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }
    if (client->connected) {
        return YETTY_ERR(yetty_ycore_void, "already connected");
    }

    /* Reset recv state */
    client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
    client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
    client->recv_offset = 0;

    /* Setup TCP client callbacks */
    ydebug("VNC client: setting up TCP callbacks");
    struct yetty_ycore_client_callbacks callbacks = {
        .ctx = client,
        .on_connect = vnc_client_on_connect,
        .on_connect_error = vnc_client_on_connect_error,
        .on_alloc = vnc_client_on_alloc,
        .on_data = vnc_client_on_data,
        .on_disconnect = vnc_client_on_disconnect,
    };

    /* Create TCP client (starts connecting immediately) */
    ydebug("VNC client: calling event_loop->create_tcp_client");
    struct yetty_ycore_tcp_client_id_result id_res =
        client->event_loop->ops->create_tcp_client(client->event_loop, host, port, &callbacks);
    if (!YETTY_IS_OK(id_res)) {
        ydebug("VNC client: create_tcp_client failed: %s", id_res.error.msg);
        return YETTY_ERR(yetty_ycore_void, "failed to create TCP client");
    }

    client->tcp_client_id = id_res.value;

    ydebug("VNC client: connecting to %s:%u, tcp_client_id=%d", host, port, client->tcp_client_id);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_disconnect(struct yetty_yvnc_client *client)
{
    if (!client) {
        return YETTY_OK_VOID();
    }

    if (client->tcp_client_id >= 0) {
        client->event_loop->ops->stop_tcp_client(client->event_loop, client->tcp_client_id);
        client->tcp_client_id = -1;
    }

    client->connected = 0;
    client->conn = NULL;

    return YETTY_OK_VOID();
}

int yetty_yvnc_client_is_connected(const struct yetty_yvnc_client *client)
{
    return client ? client->connected : 0;
}

uint16_t yetty_yvnc_client_width(const struct yetty_yvnc_client *client)
{
    return client ? client->width : 0;
}

uint16_t yetty_yvnc_client_height(const struct yetty_yvnc_client *client)
{
    return client ? client->height : 0;
}

/*===========================================================================
 * GPU Resources
 *===========================================================================*/

static struct yetty_ycore_void_result ensure_resources(struct yetty_yvnc_client *client,
                                                       uint16_t width, uint16_t height)
{
    if (client->texture_width == width && client->texture_height == height && client->texture) {
        return YETTY_OK_VOID();
    }

    /* Release old */
    if (client->bind_group) {
        wgpuBindGroupRelease(client->bind_group);
        client->bind_group = NULL;
    }
    if (client->texture_view) {
        wgpuTextureViewRelease(client->texture_view);
        client->texture_view = NULL;
    }
    if (client->texture) {
        wgpuTextureRelease(client->texture);
        client->texture = NULL;
    }

    client->texture_width = width;
    client->texture_height = height;

    /* Create texture */
    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.size = (WGPUExtent3D){width, height, 1};
    tex_desc.format = WGPUTextureFormat_BGRA8Unorm;
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    tex_desc.dimension = WGPUTextureDimension_2D;

    client->texture = wgpuDeviceCreateTexture(client->device, &tex_desc);
    if (!client->texture) {
        return YETTY_ERR(yetty_ycore_void, "failed to create texture");
    }

    /* Clear texture to black with alpha */
    size_t pixel_count = width * height;
    uint8_t *clear_data = calloc(pixel_count, 4);
    if (clear_data) {
        for (size_t i = 0; i < pixel_count; i++) {
            clear_data[i * 4 + 3] = 255; /* Set alpha to 255 */
        }

        WGPUTexelCopyTextureInfo dst = {0};
        dst.texture = client->texture;
        WGPUTexelCopyBufferLayout layout = {0};
        layout.bytesPerRow = width * 4;
        layout.rowsPerImage = height;
        WGPUExtent3D size = {width, height, 1};
        wgpuQueueWriteTexture(client->queue, &dst, clear_data, pixel_count * 4, &layout, &size);
        free(clear_data);
    }

    /* Create texture view */
    client->texture_view = wgpuTextureCreateView(client->texture, NULL);
    if (!client->texture_view) {
        return YETTY_ERR(yetty_ycore_void, "failed to create texture view");
    }

    /* Create sampler if needed */
    if (!client->sampler) {
        WGPUSamplerDescriptor sampler_desc = {0};
        sampler_desc.magFilter = WGPUFilterMode_Linear;
        sampler_desc.minFilter = WGPUFilterMode_Linear;
        sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
        sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
        sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
        sampler_desc.maxAnisotropy = 1;
        client->sampler = wgpuDeviceCreateSampler(client->device, &sampler_desc);
    }

    /* Create pipeline if needed */
    if (!client->pipeline) {
        struct yetty_ycore_void_result res = create_pipeline(client);
        if (!YETTY_IS_OK(res)) {
            return res;
        }
    }

    /* Create bind group */
    WGPUBindGroupEntry entries[2] = {0};
    entries[0].binding = 0;
    entries[0].textureView = client->texture_view;
    entries[1].binding = 1;
    entries[1].sampler = client->sampler;

    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = client->bind_group_layout;
    bg_desc.entryCount = 2;
    bg_desc.entries = entries;
    client->bind_group = wgpuDeviceCreateBindGroup(client->device, &bg_desc);

    ydebug("VNC client: created resources %ux%u", width, height);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result create_pipeline(struct yetty_yvnc_client *client)
{
    /* Create shader */
    WGPUShaderSourceWGSL wgsl_desc = {0};
    wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_desc.code = (WGPUStringView){.data = QUAD_SHADER, .length = strlen(QUAD_SHADER)};

    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_desc;

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(client->device, &shader_desc);
    if (!shader) {
        return YETTY_ERR(yetty_ycore_void, "failed to create shader");
    }

    /* Bind group layout */
    WGPUBindGroupLayoutEntry entries[2] = {0};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 2;
    bgl_desc.entries = entries;
    client->bind_group_layout = wgpuDeviceCreateBindGroupLayout(client->device, &bgl_desc);

    /* Pipeline layout */
    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &client->bind_group_layout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(client->device, &pl_desc);

    /* Render pipeline */
    WGPUColorTargetState target = {0};
    target.format = client->surface_format;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {0};
    fragment.module = shader;
    fragment.entryPoint = (WGPUStringView){.data = "fs_main", .length = 7};
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.layout = layout;
    rp_desc.vertex.module = shader;
    rp_desc.vertex.entryPoint = (WGPUStringView){.data = "vs_main", .length = 7};
    rp_desc.fragment = &fragment;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;

    client->pipeline = wgpuDeviceCreateRenderPipeline(client->device, &rp_desc);

    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(layout);

    if (!client->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "failed to create pipeline");
    }

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Rendering
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yvnc_client_update_texture(struct yetty_yvnc_client *client)
{
    (void)client;
    /* Texture is updated in on_data callback */
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_render(struct yetty_yvnc_client *client,
                                                       WGPURenderPassEncoder pass,
                                                       uint32_t viewport_width,
                                                       uint32_t viewport_height)
{
    (void)viewport_width;
    (void)viewport_height;

    if (!client || !client->pipeline || !client->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "resources not ready");
    }

    wgpuRenderPassEncoderSetPipeline(pass, client->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, client->bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Input sending
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yvnc_client_send_mouse_move(struct yetty_yvnc_client *client,
                                                                int16_t x, int16_t y,
                                                                uint8_t buttons)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_MOUSE_MOVE;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_mouse_move_event);

    struct yetty_yvnc_vnc_mouse_move_event msg = {0};
    msg.x = x;
    msg.y = y;
    msg.mods = buttons;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_mouse_button(struct yetty_yvnc_client *client,
                                                                  int16_t x, int16_t y,
                                                                  uint8_t button, int pressed,
                                                                  uint8_t mods)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_MOUSE_BUTTON;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_mouse_button_event);

    struct yetty_yvnc_vnc_mouse_button_event msg = {0};
    msg.x = x;
    msg.y = y;
    msg.button = button;
    msg.pressed = (uint8_t)pressed;
    msg.mods = mods;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_mouse_scroll(struct yetty_yvnc_client *client,
                                                                  int16_t x, int16_t y, int16_t dx,
                                                                  int16_t dy, uint8_t buttons)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_MOUSE_SCROLL;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_mouse_scroll_event);

    struct yetty_yvnc_vnc_mouse_scroll_event msg = {0};
    msg.x = x;
    msg.y = y;
    msg.delta_x = dx;
    msg.delta_y = dy;
    msg.mods = buttons;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_key_down(struct yetty_yvnc_client *client,
                                                              uint32_t keycode, uint32_t scancode,
                                                              uint8_t mods)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_KEY_DOWN;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_key_event);

    struct yetty_yvnc_vnc_key_event msg = {0};
    msg.keycode = keycode;
    msg.scancode = scancode;
    msg.mods = mods;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_key_up(struct yetty_yvnc_client *client,
                                                            uint32_t keycode, uint32_t scancode,
                                                            uint8_t mods)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_KEY_UP;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_key_event);

    struct yetty_yvnc_vnc_key_event msg = {0};
    msg.keycode = keycode;
    msg.scancode = scancode;
    msg.mods = mods;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_char_with_mods(struct yetty_yvnc_client *client,
                                                                    uint32_t codepoint,
                                                                    uint8_t mods)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_CHAR_WITH_MODS;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_char_with_mods_event);

    struct yetty_yvnc_vnc_char_with_mods_event msg = {0};
    msg.codepoint = codepoint;
    msg.mods = mods;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_resize(struct yetty_yvnc_client *client,
                                                            uint16_t width, uint16_t height)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    ydebug("VNC client send resize: %ux%u", width, height);

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_RESIZE;
    hdr.data_size = sizeof(struct yetty_yvnc_vnc_resize_event);

    struct yetty_yvnc_vnc_resize_event msg = {0};
    msg.width = width;
    msg.height = height;

    uint8_t buf[sizeof(hdr) + sizeof(msg)];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), &msg, sizeof(msg));
    send_input(client, buf, sizeof(buf));

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvnc_client_send_frame_ack(struct yetty_yvnc_client *client)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }

    struct yetty_yvnc_vnc_input_header hdr = {0};
    hdr.type = YETTY_YVNC_VNC_INPUT_FRAME_ACK;
    hdr.data_size = 0;

    send_input(client, &hdr, sizeof(hdr));

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Callbacks and stats
 *===========================================================================*/

void yetty_yvnc_client_set_on_frame(struct yetty_yvnc_client *client, yetty_vnc_on_frame_fn callback,
                                   void *userdata)
{
    if (!client) {
        return;
    }
    client->on_frame_fn = callback;
    client->on_frame_userdata = userdata;
}

void yetty_yvnc_client_set_on_connected(struct yetty_yvnc_client *client,
                                       yetty_vnc_on_connected_fn callback, void *userdata)
{
    if (!client) {
        return;
    }
    client->on_connected_fn = callback;
    client->on_connected_userdata = userdata;
}

void yetty_yvnc_client_set_on_disconnected(struct yetty_yvnc_client *client,
                                          yetty_vnc_on_disconnected_fn callback, void *userdata)
{
    if (!client) {
        return;
    }
    client->on_disconnected_fn = callback;
    client->on_disconnected_userdata = userdata;
}

struct yetty_yvnc_client_stats yetty_yvnc_client_get_stats(const struct yetty_yvnc_client *client)
{
    if (!client) {
        struct yetty_yvnc_client_stats empty = {0};
        return empty;
    }
    return client->stats;
}

void yetty_yvnc_client_set_reconnect_params(struct yetty_yvnc_client *client, const char *host,
                                           uint16_t port)
{
    if (!client) {
        return;
    }
    free(client->reconnect_host);
    client->reconnect_host = host ? strdup(host) : NULL;
    client->reconnect_port = port;
}

struct yetty_ycore_void_result yetty_yvnc_client_reconnect(struct yetty_yvnc_client *client)
{
    if (!client) {
        return YETTY_ERR(yetty_ycore_void, "null client");
    }
    if (!client->reconnect_host) {
        return YETTY_ERR(yetty_ycore_void, "no reconnect params set");
    }

    ydebug("VNC client reconnecting to %s:%u", client->reconnect_host, client->reconnect_port);
    client->wants_reconnect = 0;

    /* Reset recv state */
    client->recv_state = YETTY_YVNC_RECV_FRAME_HEADER;
    client->recv_needed = sizeof(struct yetty_yvnc_vnc_frame_header);
    client->recv_offset = 0;

    return yetty_yvnc_client_connect(client, client->reconnect_host, client->reconnect_port);
}

int yetty_yvnc_client_wants_reconnect(const struct yetty_yvnc_client *client)
{
    return client ? client->wants_reconnect : 0;
}

void yetty_yvnc_client_clear_reconnect(struct yetty_yvnc_client *client)
{
    if (client) {
        client->wants_reconnect = 0;
    }
}
