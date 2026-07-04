#ifndef YETTY_YVNC_PROTOCOL_INTERNAL_H
#define YETTY_YVNC_PROTOCOL_INTERNAL_H

#include <stdint.h>

/*=============================================================================
 * Internal wire protocol - NOT part of public API
 *===========================================================================*/

#define VNC_DEFAULT_PORT 5900
#define VNC_TILE_SIZE 64
#define VNC_FRAME_MAGIC 0x594E4346 /* "YNCF" */

/* Modifier flags */
#define VNC_MOD_SHIFT 0x01
#define VNC_MOD_CTRL 0x02
#define VNC_MOD_ALT 0x04
#define VNC_MOD_SUPER 0x08

/* Encoding types */
enum yetty_yvnc_vnc_encoding {
    YETTY_YVNC_VNC_ENCODING_RAW = 0,
    YETTY_YVNC_VNC_ENCODING_RLE = 1,
    YETTY_YVNC_VNC_ENCODING_JPEG = 2,
    YETTY_YVNC_VNC_ENCODING_FULL_FRAME = 3,
    YETTY_YVNC_VNC_ENCODING_RECT_RAW = 4,
    YETTY_YVNC_VNC_ENCODING_RECT_JPEG = 5,
    YETTY_YVNC_VNC_ENCODING_H264 = 6,
};

/* Input event types (client -> server) */
enum yetty_yvnc_vnc_input_type {
    YETTY_YVNC_VNC_INPUT_MOUSE_MOVE = 0,
    YETTY_YVNC_VNC_INPUT_MOUSE_BUTTON = 1,
    YETTY_YVNC_VNC_INPUT_MOUSE_SCROLL = 2,
    YETTY_YVNC_VNC_INPUT_KEY_DOWN = 3,
    YETTY_YVNC_VNC_INPUT_KEY_UP = 4,
    YETTY_YVNC_VNC_INPUT_RESIZE = 6,
    YETTY_YVNC_VNC_INPUT_CHAR_WITH_MODS = 8,
    YETTY_YVNC_VNC_INPUT_FRAME_ACK = 9,
    YETTY_YVNC_VNC_INPUT_COMPRESSION_CONFIG = 10,
};

/* Mouse buttons */
enum yetty_yvnc_vnc_mouse_button {
    YETTY_YVNC_VNC_MOUSE_LEFT = 0,
    YETTY_YVNC_VNC_MOUSE_MIDDLE = 1,
    YETTY_YVNC_VNC_MOUSE_RIGHT = 2,
};

/* Codec types */
#define VNC_CODEC_JPEG 0
#define VNC_CODEC_H264 1

/*=============================================================================
 * Wire format structures (packed)
 *===========================================================================*/

#pragma pack(push, 1)

struct yetty_yvnc_vnc_frame_header {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint16_t tile_size;
    uint16_t num_tiles;
    /* Per-client monotonically increasing sequence number. Client must echo
     * back via FRAME_ACK after the frame is fully decoded — the server uses
     * this for stop-and-wait flow control (no new frame to a client until
     * it acks the previous one). */
    uint32_t seq;
};

struct yetty_yvnc_vnc_tile_header {
    uint16_t tile_x;
    uint16_t tile_y;
    uint8_t encoding;
    uint32_t data_size;
};

struct yetty_yvnc_vnc_rect_header {
    uint16_t px_x;
    uint16_t px_y;
    uint16_t width;
    uint16_t height;
    uint8_t encoding;
    uint8_t reserved;
    uint32_t data_size;
};

struct yetty_yvnc_vnc_video_frame_header {
    uint8_t frame_type;
    uint8_t reserved[3];
    uint32_t timestamp;
    uint32_t data_size;
};

struct yetty_yvnc_vnc_input_header {
    uint8_t type;
    uint8_t reserved;
    uint16_t data_size;
};

struct yetty_yvnc_vnc_mouse_move_event {
    int16_t x;
    int16_t y;
    uint8_t mods;
    uint8_t reserved;
};

struct yetty_yvnc_vnc_mouse_button_event {
    int16_t x;
    int16_t y;
    uint8_t button;
    uint8_t pressed;
    uint8_t mods;
    uint8_t reserved;
};

struct yetty_yvnc_vnc_mouse_scroll_event {
    int16_t x;
    int16_t y;
    int16_t delta_x;
    int16_t delta_y;
    uint8_t mods;
    uint8_t reserved;
};

struct yetty_yvnc_vnc_key_event {
    uint32_t keycode;
    uint32_t scancode;
    uint8_t mods;
};

struct yetty_yvnc_vnc_char_with_mods_event {
    uint32_t codepoint;
    uint8_t mods;
};

struct yetty_yvnc_vnc_resize_event {
    uint16_t width;
    uint16_t height;
    /* Viewer display density (framebuffer px / logical px) in 8.8 fixed
     * point: 256 = 1.0x, 512 = Retina 2x. 0 = the peer did not declare a
     * scale (legacy 4-byte resize payload, or an unknown density) — the
     * server keeps its current content scale in that case. Carried so
     * the producer can render at the VIEWER's density instead of baking
     * its own display's scale into the shipped framebuffer. */
    uint16_t content_scale_x256;
};

struct yetty_yvnc_vnc_compression_config_event {
    uint8_t force_raw;
    uint8_t quality;
    uint8_t always_full;
    uint8_t codec;
};

struct yetty_yvnc_vnc_frame_ack_event {
    uint32_t seq;
};

#pragma pack(pop)

/*=============================================================================
 * Utility
 *===========================================================================*/

static inline uint16_t vnc_tiles_x(uint16_t width)
{
    return (width + VNC_TILE_SIZE - 1) / VNC_TILE_SIZE;
}

static inline uint16_t vnc_tiles_y(uint16_t height)
{
    return (height + VNC_TILE_SIZE - 1) / VNC_TILE_SIZE;
}

/* 8.8 fixed-point codec for resize_event.content_scale_x256. Non-positive
 * scales encode to 0 ("undeclared"), and 0 decodes back to 0.0f so the
 * receiver can distinguish "no opinion" from an actual 1.0x. */
static inline uint16_t vnc_content_scale_to_wire(float content_scale)
{
    if (content_scale <= 0.0f) {
        return 0;
    }
    float scaled = content_scale * 256.0f + 0.5f;
    if (scaled > 65535.0f) {
        return 65535;
    }
    return (uint16_t)scaled;
}

static inline float vnc_content_scale_from_wire(uint16_t wire_scale)
{
    return wire_scale ? (float)wire_scale / 256.0f : 0.0f;
}

#endif /* YETTY_YVNC_PROTOCOL_INTERNAL_H */
