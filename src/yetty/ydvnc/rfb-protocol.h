#ifndef YETTY_YDVNC_RFB_PROTOCOL_H
#define YETTY_YDVNC_RFB_PROTOCOL_H

/*
 * RFB wire protocol — internal to ydvnc.
 *
 * All on-the-wire integers are big-endian. We use `htons/htonl/ntohs/ntohl`
 * at the encode/decode boundary; structs here are declared as packed for
 * direct reads but every multi-byte field is byte-swapped on access.
 */

#include <stdint.h>

#define YETTY_YDVNC_RFB_DEFAULT_PORT 5900

/*=============================================================================
 * Handshake
 *===========================================================================*/

/* Protocol version — 12-byte ASCII string "RFB xxx.yyy\n" */
#define YETTY_YDVNC_RFB_PROTO_VERSION_LEN 12
#define YETTY_YDVNC_RFB_PROTO_VERSION_38 "RFB 003.008\n"

/* Security types (RFC 6143 §7.1.2) */
enum {
    YETTY_YDVNC_RFB_SEC_INVALID = 0,
    YETTY_YDVNC_RFB_SEC_NONE = 1,
    YETTY_YDVNC_RFB_SEC_VNC_AUTH = 2,
};

/* SecurityResult */
enum {
    YETTY_YDVNC_RFB_SECRESULT_OK = 0,
    YETTY_YDVNC_RFB_SECRESULT_FAILED = 1,
};

/* VNC authentication challenge size (RFC 6143 §7.2.2) */
#define YETTY_YDVNC_RFB_VNC_AUTH_CHALLENGE_LEN 16

/*=============================================================================
 * PixelFormat (16 bytes — RFC 6143 §7.4)
 *===========================================================================*/

#pragma pack(push, 1)

struct yetty_ydvnc_rfb_pixel_format {
    uint8_t bits_per_pixel;
    uint8_t depth;
    uint8_t big_endian_flag;
    uint8_t true_colour_flag;
    uint16_t red_max; /* big-endian on the wire */
    uint16_t green_max;
    uint16_t blue_max;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    uint8_t padding[3];
};

/* ServerInit body (after pixel format) — name length + name string follow */
struct yetty_ydvnc_rfb_server_init_fixed {
    uint16_t framebuffer_width;
    uint16_t framebuffer_height;
    struct yetty_ydvnc_rfb_pixel_format pixel_format;
    uint32_t name_length;
};

#pragma pack(pop)

/*=============================================================================
 * Client → server messages (RFC 6143 §7.5)
 *===========================================================================*/

enum {
    YETTY_YDVNC_RFB_C2S_SET_PIXEL_FORMAT = 0,
    YETTY_YDVNC_RFB_C2S_SET_ENCODINGS = 2,
    YETTY_YDVNC_RFB_C2S_FRAMEBUFFER_UPDATE_REQUEST = 3,
    YETTY_YDVNC_RFB_C2S_KEY_EVENT = 4,
    YETTY_YDVNC_RFB_C2S_POINTER_EVENT = 5,
    YETTY_YDVNC_RFB_C2S_CLIENT_CUT_TEXT = 6,
};

#pragma pack(push, 1)

struct yetty_ydvnc_rfb_set_pixel_format_msg {
    uint8_t message_type; /* = 0 */
    uint8_t padding[3];
    struct yetty_ydvnc_rfb_pixel_format pixel_format;
};

struct yetty_ydvnc_rfb_framebuffer_update_request_msg {
    uint8_t message_type; /* = 3 */
    uint8_t incremental;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

struct yetty_ydvnc_rfb_key_event_msg {
    uint8_t message_type; /* = 4 */
    uint8_t down_flag;
    uint8_t padding[2];
    uint32_t key;
};

struct yetty_ydvnc_rfb_pointer_event_msg {
    uint8_t message_type; /* = 5 */
    uint8_t button_mask;
    uint16_t x;
    uint16_t y;
};

#pragma pack(pop)

/*=============================================================================
 * Server → client messages (RFC 6143 §7.6)
 *===========================================================================*/

enum {
    YETTY_YDVNC_RFB_S2C_FRAMEBUFFER_UPDATE = 0,
    YETTY_YDVNC_RFB_S2C_SET_COLOUR_MAP_ENTRIES = 1,
    YETTY_YDVNC_RFB_S2C_BELL = 2,
    YETTY_YDVNC_RFB_S2C_SERVER_CUT_TEXT = 3,
};

#pragma pack(push, 1)

struct yetty_ydvnc_rfb_fb_update_header {
    uint8_t message_type; /* = 0 */
    uint8_t padding;
    uint16_t num_rectangles;
};

struct yetty_ydvnc_rfb_rectangle_header {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    int32_t encoding;
};

#pragma pack(pop)

/*=============================================================================
 * Encoding type IDs (RFC 6143 §7.7 + community)
 *===========================================================================*/

#define YETTY_YDVNC_RFB_ENC_RAW 0
#define YETTY_YDVNC_RFB_ENC_COPYRECT 1
#define YETTY_YDVNC_RFB_ENC_RRE 2
#define YETTY_YDVNC_RFB_ENC_HEXTILE 5
#define YETTY_YDVNC_RFB_ENC_TIGHT 7
#define YETTY_YDVNC_RFB_ENC_ZRLE 16

/* Pseudo-encodings — negative IDs requested in SetEncodings to signal client
 * capabilities. Server then uses them as encoding IDs in special rectangles. */
#define YETTY_YDVNC_RFB_PSEUDO_CURSOR (-239)
#define YETTY_YDVNC_RFB_PSEUDO_DESKTOP_SIZE (-223)

/*=============================================================================
 * Tight encoding (rfbproto §6.7)
 *
 * Compression-control byte:
 *   bits 0..3  reset zlib streams (per-stream bit)
 *   bits 4..7  encoding type:
 *       0x00 = basic (deflate); reset bits matter
 *       0x80 = JPEG (TurboJPEG sub-encoding)
 *       0x40..0x70 = fill / copy-filter (subset of "basic" with filter bit)
 *
 *   For "basic" the high nibble is one of:
 *     0x00 -> stream 0
 *     0x10 -> stream 1
 *     0x20 -> stream 2
 *     0x30 -> stream 3
 *   When the FILTER bit (0x40) is set, a filter-id byte follows.
 *
 * Sub-encodings the server may send under "basic":
 *   filter 0 = COPY (raw pixels deflated)
 *   filter 1 = PALETTE (n+1 colours, then indexed pixels deflated)
 *   filter 2 = GRADIENT
 *
 * Special compact form: control byte == 0x80 ^ 0x80 == FILL, single pixel
 * follows uncompressed (no length prefix, no zlib).
 *===========================================================================*/

#define YETTY_YDVNC_RFB_TIGHT_RESET_STREAM(n) (1u << (n))
#define YETTY_YDVNC_RFB_TIGHT_RESET_MASK 0x0fu
#define YETTY_YDVNC_RFB_TIGHT_EXPLICIT_FILTER 0x40u
#define YETTY_YDVNC_RFB_TIGHT_FILL 0x80u
#define YETTY_YDVNC_RFB_TIGHT_JPEG 0x90u
#define YETTY_YDVNC_RFB_TIGHT_STREAM_MASK 0x30u
#define YETTY_YDVNC_RFB_TIGHT_STREAM_SHIFT 4

#define YETTY_YDVNC_RFB_TIGHT_FILTER_COPY 0
#define YETTY_YDVNC_RFB_TIGHT_FILTER_PALETTE 1
#define YETTY_YDVNC_RFB_TIGHT_FILTER_GRADIENT 2

#define YETTY_YDVNC_RFB_TIGHT_NUM_STREAMS 4

/* Tight uses a variable-length length encoding (1..3 bytes, 7 bits each) for
 * compressed-payload sizes — bit 7 of each byte is a continuation flag. */

#endif /* YETTY_YDVNC_RFB_PROTOCOL_H */
