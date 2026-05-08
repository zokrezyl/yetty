/*
 * rfb-client.c — RFB protocol state machine, framebuffer, GPU upload/render.
 *
 * State machine:
 *   PROTO_VERSION   : recv 12 bytes server version, send our version
 *   SECURITY_TYPES  : recv u8 N + N security types, pick one, send 1 byte back
 *   SECURITY_REASON : recv u32 reason length + reason string (only if N==0)
 *   AUTH_RESULT     : recv u32 status (and optional reason on failure)
 *   SERVER_INIT     : recv ServerInit, then SetEncodings + SetPixelFormat +
 *                     initial FramebufferUpdateRequest
 *   MAIN            : recv messages (FramebufferUpdate / Bell / etc.)
 *
 * Currently negotiates security type None only.
 */

#include "rfb-client.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yevent/event-loop.h>
#include <yetty/ytrace/ytrace.h>

#include "rfb-protocol.h"
#include "transport.h"
#include "vnc-auth.h"

#define YDVNC_RECV_BUF_INITIAL (1u * 1024u * 1024u)
#define YDVNC_RECV_BUF_MAX     (64u * 1024u * 1024u)

/* Fullscreen quad shader — copy of the well-known WebGPU pattern: emit six
 * vertices spanning [-1,1]^2 with matching uv [0,1]^2, sample one texture
 * with a bilinear sampler. */
static const char *YDVNC_QUAD_WGSL =
    "struct VsOut {\n"
    "    @builtin(position) pos: vec4<f32>,\n"
    "    @location(0) uv: vec2<f32>,\n"
    "};\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) idx: u32) -> VsOut {\n"
    "    var p = array<vec2<f32>, 6>(\n"
    "        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),\n"
    "        vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0));\n"
    "    var u = array<vec2<f32>, 6>(\n"
    "        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0),\n"
    "        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(1.0, 0.0));\n"
    "    var o: VsOut;\n"
    "    o.pos = vec4(p[idx], 0.0, 1.0);\n"
    "    o.uv  = u[idx];\n"
    "    return o;\n"
    "}\n"
    "@group(0) @binding(0) var fb_tex: texture_2d<f32>;\n"
    "@group(0) @binding(1) var fb_smp: sampler;\n"
    "@fragment\n"
    "fn fs_main(in: VsOut) -> @location(0) vec4<f32> {\n"
    /* RFB pixels are 32-bit, but only 24 bits of colour are defined; the
     * fourth byte is unspecified padding. Force opaque alpha so it doesn't
     * leak through as transparency. */
    "    let s = textureSample(fb_tex, fb_smp, in.uv);\n"
    "    return vec4<f32>(s.rgb, 1.0);\n"
    "}\n";

/*=============================================================================
 * State
 *===========================================================================*/

enum ydvnc_state {
    YDVNC_ST_DISCONNECTED,
    YDVNC_ST_PROTO_VERSION,
    YDVNC_ST_SECURITY_TYPES,
    YDVNC_ST_SECURITY_REASON,
    YDVNC_ST_AUTH_CHALLENGE,
    YDVNC_ST_AUTH_RESULT,
    YDVNC_ST_AUTH_FAIL_REASON,
    YDVNC_ST_SERVER_INIT,
    YDVNC_ST_MAIN,
};

struct yetty_ydvnc_rfb_client {
    /* GPU */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;

    WGPUTexture fb_texture;
    WGPUTextureView fb_view;
    WGPUSampler sampler;
    WGPUBindGroupLayout bgl;
    WGPUBindGroup bind_group;
    WGPURenderPipeline pipeline;

    /* Transport */
    struct yetty_yevent_event_loop *event_loop;
    struct yetty_ydvnc_transport *transport;
    int transport_owned;

    /* Connection */
    enum ydvnc_state state;

    /* Pixel/framebuffer state */
    uint16_t fb_width;
    uint16_t fb_height;
    /* The pixel format we negotiated with the server. We always request
     * 32-bpp little-endian BGRA so that server-sent Raw bytes can be
     * memcpy'd straight into a BGRA8Unorm texture upload. */
    struct yetty_ydvnc_rfb_pixel_format pf;

    /* In-progress FramebufferUpdate parser state. Stored across recv chunks. */
    int in_fb_update;
    uint16_t fb_rects_remaining;
    int have_rect_header;
    struct yetty_ydvnc_rfb_rectangle_header current_rect;

    /* Recv ring buffer — we accumulate bytes and consume parsed prefixes.
     * Tight uses variable-length payloads, so we keep buffering until we
     * have a full message before advancing the state machine. */
    uint8_t *rb;
    size_t rb_off;     /* offset into rb where the next byte arrives */
    size_t rb_cap;

    /* Callbacks */
    yetty_ydvnc_on_frame_fn on_frame;
    void *on_frame_userdata;
    yetty_ydvnc_on_connected_fn on_connected;
    void *on_connected_userdata;
    yetty_ydvnc_on_disconnected_fn on_disconnected;
    void *on_disconnected_userdata;

    /* Rect data scratch — Raw/CopyRect/etc. assembled here. */
    uint8_t *rect_buf;
    size_t rect_buf_cap;

    /* Optional password for VNC auth (security type 2). NULL → only
     * security type None is accepted. */
    char *password;
};

/*=============================================================================
 * Forward declarations
 *===========================================================================*/

static struct yetty_ycore_void_result ensure_gpu_pipeline(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result resize_framebuffer(struct yetty_ydvnc_rfb_client *c,
                                                         uint16_t w, uint16_t h);
static struct yetty_ycore_void_result process_recv(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_proto_version(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_security_types(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_security_reason(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_auth_challenge(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_auth_result(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_auth_fail_reason(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_server_init(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result handle_main(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result send_client_init(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result send_set_pixel_format(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result send_set_encodings(struct yetty_ydvnc_rfb_client *c);
static struct yetty_ycore_void_result send_full_fb_update_request(struct yetty_ydvnc_rfb_client *c,
                                                                  int incremental);
static struct yetty_ycore_void_result decode_raw_rect(struct yetty_ydvnc_rfb_client *c,
                                                     const uint8_t *data, size_t data_size);
static struct yetty_ycore_void_result decode_copyrect(struct yetty_ydvnc_rfb_client *c,
                                                     const uint8_t *data, size_t data_size);

static void consume(struct yetty_ydvnc_rfb_client *c, size_t n);
static struct yetty_ycore_void_result transport_send(struct yetty_ydvnc_rfb_client *c,
                                                     const void *data, size_t nbytes);

/*=============================================================================
 * Transport callbacks
 *===========================================================================*/

static void on_transport_connect(void *userdata)
{
    struct yetty_ydvnc_rfb_client *c = userdata;
    yinfo("ydvnc: TCP connected, awaiting protocol version");
    c->state = YDVNC_ST_PROTO_VERSION;
}

static void on_transport_connect_error(void *userdata, const char *err)
{
    struct yetty_ydvnc_rfb_client *c = userdata;
    yerror("ydvnc: connect failed: %s", err);
    c->state = YDVNC_ST_DISCONNECTED;
    if (c->on_disconnected) {
        c->on_disconnected(c->on_disconnected_userdata, err);
    }
}

YETTY_EXTERNAL_CALLBACK
static void on_transport_data(void *userdata, const uint8_t *data, size_t nbytes)
{
    struct yetty_ydvnc_rfb_client *c = userdata;

    /* Grow ring buffer if needed. */
    if (c->rb_off + nbytes > c->rb_cap) {
        size_t new_cap = c->rb_cap ? c->rb_cap : YDVNC_RECV_BUF_INITIAL;
        while (new_cap < c->rb_off + nbytes) {
            new_cap *= 2;
            if (new_cap > YDVNC_RECV_BUF_MAX) {
                yerror("ydvnc: recv buffer would exceed %u bytes", YDVNC_RECV_BUF_MAX);
                return;
            }
        }
        uint8_t *p = realloc(c->rb, new_cap);
        if (!p) {
            yerror("ydvnc: recv buffer alloc failed");
            return;
        }
        c->rb = p;
        c->rb_cap = new_cap;
    }
    memcpy(c->rb + c->rb_off, data, nbytes);
    c->rb_off += nbytes;

    struct yetty_ycore_void_result r = process_recv(c);
    if (YETTY_IS_ERR(r)) {
        yerror("ydvnc: recv processing error: %s", r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
}

static void on_transport_disconnect(void *userdata)
{
    struct yetty_ydvnc_rfb_client *c = userdata;
    ywarn("ydvnc: transport disconnected");
    c->state = YDVNC_ST_DISCONNECTED;
    if (c->on_disconnected) {
        c->on_disconnected(c->on_disconnected_userdata, "disconnected");
    }
}

/*=============================================================================
 * Recv buffer helpers
 *===========================================================================*/

static void consume(struct yetty_ydvnc_rfb_client *c, size_t n)
{
    if (n >= c->rb_off) {
        c->rb_off = 0;
        return;
    }
    memmove(c->rb, c->rb + n, c->rb_off - n);
    c->rb_off -= n;
}

static struct yetty_ycore_void_result transport_send(struct yetty_ydvnc_rfb_client *c,
                                                     const void *data, size_t nbytes)
{
    if (!c->transport) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: transport not set");
    }
    return c->transport->ops->send(c->transport, data, nbytes);
}

/*=============================================================================
 * Recv state machine driver
 *===========================================================================*/

static struct yetty_ycore_void_result process_recv(struct yetty_ydvnc_rfb_client *c)
{
    /* Loop while we make progress. Each handler returns OK without
     * advancing state if not enough bytes are buffered yet — the next
     * on_data will retry. */
    for (;;) {
        size_t before = c->rb_off;
        enum ydvnc_state st_before = c->state;

        switch (c->state) {
        case YDVNC_ST_PROTO_VERSION:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_proto_version(c),
                                "proto version handler failed");
            break;
        case YDVNC_ST_SECURITY_TYPES:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_security_types(c),
                                "security types handler failed");
            break;
        case YDVNC_ST_SECURITY_REASON:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_security_reason(c),
                                "security reason handler failed");
            break;
        case YDVNC_ST_AUTH_CHALLENGE:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_auth_challenge(c),
                                "auth challenge handler failed");
            break;
        case YDVNC_ST_AUTH_RESULT:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_auth_result(c),
                                "auth result handler failed");
            break;
        case YDVNC_ST_AUTH_FAIL_REASON:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_auth_fail_reason(c),
                                "auth fail reason handler failed");
            break;
        case YDVNC_ST_SERVER_INIT:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_server_init(c),
                                "server init handler failed");
            break;
        case YDVNC_ST_MAIN:
            YETTY_RETURN_IF_ERR(yetty_ycore_void, handle_main(c), "main handler failed");
            break;
        case YDVNC_ST_DISCONNECTED:
            return YETTY_OK_VOID();
        }

        /* Stop if no progress (bytes consumed AND state changed are both
         * false means waiting for more data). */
        if (c->rb_off == before && c->state == st_before) {
            return YETTY_OK_VOID();
        }
    }
}

/*=============================================================================
 * Handshake: ProtocolVersion
 *===========================================================================*/

static struct yetty_ycore_void_result handle_proto_version(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < YETTY_YDVNC_RFB_PROTO_VERSION_LEN) {
        return YETTY_OK_VOID();
    }

    char server_version[YETTY_YDVNC_RFB_PROTO_VERSION_LEN + 1] = {0};
    memcpy(server_version, c->rb, YETTY_YDVNC_RFB_PROTO_VERSION_LEN);
    consume(c, YETTY_YDVNC_RFB_PROTO_VERSION_LEN);

    yinfo("ydvnc: server version: %.*s", YETTY_YDVNC_RFB_PROTO_VERSION_LEN - 1, server_version);

    /* Always reply 003.008 — that's what every modern server supports. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                       transport_send(c, YETTY_YDVNC_RFB_PROTO_VERSION_38,
                                       YETTY_YDVNC_RFB_PROTO_VERSION_LEN),
                       "sending client version failed");

    c->state = YDVNC_ST_SECURITY_TYPES;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Handshake: Security types
 *===========================================================================*/

static struct yetty_ycore_void_result handle_security_types(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < 1) {
        return YETTY_OK_VOID();
    }
    uint8_t n = c->rb[0];

    if (n == 0) {
        /* Server refused — reason follows. */
        consume(c, 1);
        c->state = YDVNC_ST_SECURITY_REASON;
        return YETTY_OK_VOID();
    }

    if (c->rb_off < (size_t)1 + n) {
        return YETTY_OK_VOID();
    }

    int has_none = 0;
    int has_vnc = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t t = c->rb[1 + i];
        if (t == YETTY_YDVNC_RFB_SEC_NONE) has_none = 1;
        if (t == YETTY_YDVNC_RFB_SEC_VNC_AUTH) has_vnc = 1;
    }
    consume(c, (size_t)1 + n);

    /* Prefer None if offered (saves a round-trip). Otherwise fall back to
     * VNC password auth if we have one. */
    uint8_t chosen;
    enum ydvnc_state next;
    if (has_none) {
        chosen = YETTY_YDVNC_RFB_SEC_NONE;
        next = YDVNC_ST_AUTH_RESULT;
    } else if (has_vnc && c->password) {
        chosen = YETTY_YDVNC_RFB_SEC_VNC_AUTH;
        next = YDVNC_ST_AUTH_CHALLENGE;
    } else if (has_vnc) {
        return YETTY_ERR(yetty_ycore_void,
                        "ydvnc: server requires VNC password but none was provided "
                        "(use --ydvnc-password)");
    } else {
        return YETTY_ERR(yetty_ycore_void,
                        "ydvnc: no compatible security type offered (need None or VNC auth)");
    }

    YETTY_RETURN_IF_ERR(yetty_ycore_void, transport_send(c, &chosen, 1),
                       "sending chosen security type failed");
    yinfo("ydvnc: security type chosen: %s",
          chosen == YETTY_YDVNC_RFB_SEC_NONE ? "None" : "VNC (DES)");
    c->state = next;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_security_reason(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < 4) {
        return YETTY_OK_VOID();
    }
    uint32_t reason_len = ntohl(*(const uint32_t *)c->rb);
    if (reason_len > 4096) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: implausible security-reject reason length");
    }
    if (c->rb_off < 4 + reason_len) {
        return YETTY_OK_VOID();
    }
    char buf[4097];
    if (reason_len) {
        memcpy(buf, c->rb + 4, reason_len);
    }
    buf[reason_len] = 0;
    consume(c, 4 + reason_len);

    yerror("ydvnc: server rejected connection: %s", buf);
    return YETTY_ERR(yetty_ycore_void, "ydvnc: security rejection from server");
}

/*=============================================================================
 * Handshake: VNC auth challenge → response
 *===========================================================================*/

static struct yetty_ycore_void_result handle_auth_challenge(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < YETTY_YDVNC_RFB_VNC_AUTH_CHALLENGE_LEN) {
        return YETTY_OK_VOID();
    }
    uint8_t challenge[YETTY_YDVNC_RFB_VNC_AUTH_CHALLENGE_LEN];
    memcpy(challenge, c->rb, sizeof(challenge));
    consume(c, sizeof(challenge));

    uint8_t response[YETTY_YDVNC_RFB_VNC_AUTH_CHALLENGE_LEN];
    yetty_ydvnc_vnc_auth_response(c->password, challenge, response);

    YETTY_RETURN_IF_ERR(yetty_ycore_void, transport_send(c, response, sizeof(response)),
                       "VNC auth response send failed");
    c->state = YDVNC_ST_AUTH_RESULT;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Handshake: SecurityResult
 *===========================================================================*/

static struct yetty_ycore_void_result handle_auth_result(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < 4) {
        return YETTY_OK_VOID();
    }
    uint32_t status = ntohl(*(const uint32_t *)c->rb);
    consume(c, 4);

    if (status != YETTY_YDVNC_RFB_SECRESULT_OK) {
        /* RFB 3.8: a reason string follows on failure. Read it so we can
         * surface a useful error. */
        c->state = YDVNC_ST_AUTH_FAIL_REASON;
        return YETTY_OK_VOID();
    }

    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_client_init(c), "send_client_init failed");
    c->state = YDVNC_ST_SERVER_INIT;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_auth_fail_reason(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < 4) {
        return YETTY_OK_VOID();
    }
    uint32_t reason_len = ntohl(*(const uint32_t *)c->rb);
    if (reason_len > 4096) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: implausible auth-failed reason length");
    }
    if (c->rb_off < 4 + reason_len) {
        return YETTY_OK_VOID();
    }
    char buf[4097];
    if (reason_len) {
        memcpy(buf, c->rb + 4, reason_len);
    }
    buf[reason_len] = 0;
    consume(c, 4 + reason_len);

    yerror("ydvnc: server auth failed: %s", buf);
    return YETTY_ERR(yetty_ycore_void, "ydvnc: VNC authentication rejected");
}

/*=============================================================================
 * Handshake: ClientInit / ServerInit
 *===========================================================================*/

static struct yetty_ycore_void_result send_client_init(struct yetty_ydvnc_rfb_client *c)
{
    /* shared-flag = 1 → we're OK sharing the desktop with other viewers. */
    uint8_t shared = 1;
    return transport_send(c, &shared, 1);
}

static struct yetty_ycore_void_result handle_server_init(struct yetty_ydvnc_rfb_client *c)
{
    if (c->rb_off < sizeof(struct yetty_ydvnc_rfb_server_init_fixed)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydvnc_rfb_server_init_fixed init;
    memcpy(&init, c->rb, sizeof(init));
    uint32_t name_len = ntohl(init.name_length);
    if (name_len > 4096) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: implausible server name length");
    }
    if (c->rb_off < sizeof(init) + name_len) {
        return YETTY_OK_VOID();
    }

    uint16_t w = ntohs(init.framebuffer_width);
    uint16_t h = ntohs(init.framebuffer_height);
    char name[4097];
    if (name_len) {
        memcpy(name, c->rb + sizeof(init), name_len);
    }
    name[name_len] = 0;
    consume(c, sizeof(init) + name_len);

    yinfo("ydvnc: server init: %ux%u \"%s\"", w, h, name);

    YETTY_RETURN_IF_ERR(yetty_ycore_void, resize_framebuffer(c, w, h),
                       "framebuffer resize failed");

    /* Tell server we want native BGRA8 little-endian — straight memcpy
     * into a WGPU BGRA8Unorm texture. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_set_pixel_format(c),
                       "SetPixelFormat send failed");

    /* Advertise encodings (POC: Raw + CopyRect + DesktopSize). */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_set_encodings(c),
                       "SetEncodings send failed");

    /* Initial full update request. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_full_fb_update_request(c, 0),
                       "initial FramebufferUpdateRequest send failed");

    c->state = YDVNC_ST_MAIN;
    if (c->on_connected) {
        c->on_connected(c->on_connected_userdata);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Outgoing: SetPixelFormat / SetEncodings / FramebufferUpdateRequest
 *===========================================================================*/

static struct yetty_ycore_void_result send_set_pixel_format(struct yetty_ydvnc_rfb_client *c)
{
    struct yetty_ydvnc_rfb_set_pixel_format_msg msg = {0};
    msg.message_type = YETTY_YDVNC_RFB_C2S_SET_PIXEL_FORMAT;
    msg.pixel_format.bits_per_pixel = 32;
    msg.pixel_format.depth = 24;
    msg.pixel_format.big_endian_flag = 0;
    msg.pixel_format.true_colour_flag = 1;
    msg.pixel_format.red_max = htons(255);
    msg.pixel_format.green_max = htons(255);
    msg.pixel_format.blue_max = htons(255);
    /* Wire interpretation: little-endian uint32 with R at byte 2, G at byte 1,
     * B at byte 0 → byte order B,G,R,A → matches BGRA8Unorm. */
    msg.pixel_format.red_shift = 16;
    msg.pixel_format.green_shift = 8;
    msg.pixel_format.blue_shift = 0;

    /* Cache for pixel-format conversion (currently a memcpy fast path). */
    c->pf = msg.pixel_format;

    return transport_send(c, &msg, sizeof(msg));
}

static struct yetty_ycore_void_result send_set_encodings(struct yetty_ydvnc_rfb_client *c)
{
    /* Order matters — the server picks earlier encodings first. */
    int32_t encs[8];
    uint16_t n = 0;
    encs[n++] = htonl((uint32_t)YETTY_YDVNC_RFB_ENC_COPYRECT);
    encs[n++] = htonl((uint32_t)YETTY_YDVNC_RFB_ENC_RAW);
    /* Pseudo-encodings (negative IDs) — capability advertisements. */
    encs[n++] = htonl((uint32_t)YETTY_YDVNC_RFB_PSEUDO_DESKTOP_SIZE);

    uint8_t header[4];
    header[0] = YETTY_YDVNC_RFB_C2S_SET_ENCODINGS;
    header[1] = 0; /* padding */
    uint16_t n_be = htons(n);
    memcpy(header + 2, &n_be, 2);

    YETTY_RETURN_IF_ERR(yetty_ycore_void, transport_send(c, header, sizeof(header)),
                       "SetEncodings header send failed");
    return transport_send(c, encs, sizeof(int32_t) * n);
}

static struct yetty_ycore_void_result send_full_fb_update_request(struct yetty_ydvnc_rfb_client *c,
                                                                  int incremental)
{
    struct yetty_ydvnc_rfb_framebuffer_update_request_msg msg = {0};
    msg.message_type = YETTY_YDVNC_RFB_C2S_FRAMEBUFFER_UPDATE_REQUEST;
    msg.incremental = incremental ? 1 : 0;
    msg.x = htons(0);
    msg.y = htons(0);
    msg.width = htons(c->fb_width);
    msg.height = htons(c->fb_height);
    return transport_send(c, &msg, sizeof(msg));
}

/*=============================================================================
 * Main loop: server messages
 *===========================================================================*/

static struct yetty_ycore_void_result handle_main(struct yetty_ydvnc_rfb_client *c)
{
    /* If we're inside a FramebufferUpdate, keep parsing rectangles. */
    if (c->in_fb_update) {
        for (;;) {
            if (c->fb_rects_remaining == 0) {
                c->in_fb_update = 0;
                /* Ask for the next incremental update. */
                YETTY_RETURN_IF_ERR(yetty_ycore_void, send_full_fb_update_request(c, 1),
                                   "incremental FramebufferUpdateRequest failed");
                if (c->on_frame) {
                    c->on_frame(c->on_frame_userdata);
                }
                break;
            }

            if (!c->have_rect_header) {
                if (c->rb_off < sizeof(struct yetty_ydvnc_rfb_rectangle_header)) {
                    return YETTY_OK_VOID();
                }
                memcpy(&c->current_rect, c->rb, sizeof(c->current_rect));
                consume(c, sizeof(c->current_rect));
                c->have_rect_header = 1;
                /* Big-endian → host. */
                c->current_rect.x = ntohs(c->current_rect.x);
                c->current_rect.y = ntohs(c->current_rect.y);
                c->current_rect.width = ntohs(c->current_rect.width);
                c->current_rect.height = ntohs(c->current_rect.height);
                c->current_rect.encoding = (int32_t)ntohl((uint32_t)c->current_rect.encoding);
                ydebug("ydvnc: rect %ux%u@(%u,%u) enc=%d",
                       c->current_rect.width, c->current_rect.height,
                       c->current_rect.x, c->current_rect.y, c->current_rect.encoding);
            }

            int32_t enc = c->current_rect.encoding;
            if (enc == YETTY_YDVNC_RFB_PSEUDO_DESKTOP_SIZE) {
                /* No payload — width/height are in the rect header itself. */
                YETTY_RETURN_IF_ERR(yetty_ycore_void,
                                   resize_framebuffer(c, c->current_rect.width,
                                                      c->current_rect.height),
                                   "DesktopSize: resize_framebuffer failed");
                c->have_rect_header = 0;
                c->fb_rects_remaining--;
                continue;
            }

            if (enc == YETTY_YDVNC_RFB_ENC_RAW) {
                size_t needed = (size_t)c->current_rect.width *
                                (size_t)c->current_rect.height * 4u;
                if (c->rb_off < needed) {
                    return YETTY_OK_VOID();
                }
                YETTY_RETURN_IF_ERR(yetty_ycore_void, decode_raw_rect(c, c->rb, needed),
                                   "Raw decode failed");
                consume(c, needed);
                c->have_rect_header = 0;
                c->fb_rects_remaining--;
                continue;
            }

            if (enc == YETTY_YDVNC_RFB_ENC_COPYRECT) {
                /* CopyRect: 4 bytes — src_x (u16) + src_y (u16). */
                if (c->rb_off < 4) {
                    return YETTY_OK_VOID();
                }
                YETTY_RETURN_IF_ERR(yetty_ycore_void, decode_copyrect(c, c->rb, 4),
                                   "CopyRect decode failed");
                consume(c, 4);
                c->have_rect_header = 0;
                c->fb_rects_remaining--;
                continue;
            }

            yerror("ydvnc: server sent unsupported encoding %d for rect %ux%u — "
                  "framing is now lost, disconnecting", enc,
                  c->current_rect.width, c->current_rect.height);
            return YETTY_ERR(yetty_ycore_void,
                            "ydvnc: server sent unsupported encoding (Tight/Cursor not yet wired)");
        }
        return YETTY_OK_VOID();
    }

    /* Outside a fb-update — read message type. */
    if (c->rb_off < 1) {
        return YETTY_OK_VOID();
    }
    uint8_t mtype = c->rb[0];

    switch (mtype) {
    case YETTY_YDVNC_RFB_S2C_FRAMEBUFFER_UPDATE: {
        if (c->rb_off < sizeof(struct yetty_ydvnc_rfb_fb_update_header)) {
            return YETTY_OK_VOID();
        }
        struct yetty_ydvnc_rfb_fb_update_header h;
        memcpy(&h, c->rb, sizeof(h));
        consume(c, sizeof(h));
        c->fb_rects_remaining = ntohs(h.num_rectangles);
        c->in_fb_update = 1;
        c->have_rect_header = 0;
        ydebug("ydvnc: FramebufferUpdate: %u rects", c->fb_rects_remaining);
        return YETTY_OK_VOID();
    }
    case YETTY_YDVNC_RFB_S2C_BELL:
        consume(c, 1);
        return YETTY_OK_VOID();
    case YETTY_YDVNC_RFB_S2C_SERVER_CUT_TEXT: {
        /* 1 type + 3 padding + 4 length + length bytes — discard for POC. */
        if (c->rb_off < 8) {
            return YETTY_OK_VOID();
        }
        uint32_t len = ntohl(*(const uint32_t *)(c->rb + 4));
        if (len > 16u * 1024u * 1024u) {
            return YETTY_ERR(yetty_ycore_void, "ydvnc: implausible cut-text length");
        }
        if (c->rb_off < 8 + len) {
            return YETTY_OK_VOID();
        }
        consume(c, 8 + len);
        return YETTY_OK_VOID();
    }
    case YETTY_YDVNC_RFB_S2C_SET_COLOUR_MAP_ENTRIES:
        /* We negotiated TrueColour; this should never arrive. Bail. */
        return YETTY_ERR(yetty_ycore_void,
                        "ydvnc: server sent SetColourMapEntries despite TrueColour pixel format");
    default:
        return YETTY_ERR(yetty_ycore_void, "ydvnc: unknown server message type");
    }
}

/*=============================================================================
 * Decoders: Raw, CopyRect
 *===========================================================================*/

static struct yetty_ycore_void_result decode_raw_rect(struct yetty_ydvnc_rfb_client *c,
                                                     const uint8_t *data, size_t data_size)
{
    uint16_t x = c->current_rect.x;
    uint16_t y = c->current_rect.y;
    uint16_t w = c->current_rect.width;
    uint16_t h = c->current_rect.height;

    if (!c->fb_texture) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: framebuffer texture not ready");
    }
    if (x + w > c->fb_width || y + h > c->fb_height) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: Raw rect out of bounds");
    }
    if (data_size != (size_t)w * h * 4u) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: Raw rect data_size mismatch");
    }

    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = c->fb_texture;
    dst.origin = (WGPUOrigin3D){x, y, 0};
    WGPUTexelCopyBufferLayout layout = {0};
    layout.bytesPerRow = (uint32_t)w * 4u;
    layout.rowsPerImage = h;
    WGPUExtent3D extent = {w, h, 1};
    wgpuQueueWriteTexture(c->queue, &dst, data, data_size, &layout, &extent);

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result decode_copyrect(struct yetty_ydvnc_rfb_client *c,
                                                     const uint8_t *data, size_t data_size)
{
    if (data_size != 4) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: CopyRect data_size != 4");
    }
    uint16_t src_x = ntohs(*(const uint16_t *)(data + 0));
    uint16_t src_y = ntohs(*(const uint16_t *)(data + 2));
    uint16_t dst_x = c->current_rect.x;
    uint16_t dst_y = c->current_rect.y;
    uint16_t w = c->current_rect.width;
    uint16_t h = c->current_rect.height;

    if (!c->fb_texture) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: framebuffer texture not ready");
    }
    if (src_x + w > c->fb_width || src_y + h > c->fb_height) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: CopyRect src out of bounds");
    }
    if (dst_x + w > c->fb_width || dst_y + h > c->fb_height) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: CopyRect dst out of bounds");
    }

    /* GPU-only solution would be a CommandEncoder copy_texture_to_texture
     * pass; for the POC we reach for a CPU staging buffer. Servers send
     * CopyRect rarely (typically window drags) so the cost is acceptable. */
    size_t bytes = (size_t)w * h * 4u;
    if (bytes > c->rect_buf_cap) {
        uint8_t *p = realloc(c->rect_buf, bytes);
        if (!p) {
            return YETTY_ERR(yetty_ycore_void, "ydvnc: CopyRect alloc failed");
        }
        c->rect_buf = p;
        c->rect_buf_cap = bytes;
    }

    /* Read source rect from GPU — there's no synchronous read in WebGPU;
     * we maintain a CPU shadow buffer separately. For POC we skip and just
     * leave the destination unchanged (the server will catch up via a Raw
     * update on the next frame for that region). Log a warn. */
    ywarn("ydvnc: CopyRect requested (%u,%u)->(%u,%u) %ux%u — POC: not blitted, awaiting server raw redraw",
          src_x, src_y, dst_x, dst_y, w, h);

    return YETTY_OK_VOID();
}

/*=============================================================================
 * GPU resources
 *===========================================================================*/

static struct yetty_ycore_void_result resize_framebuffer(struct yetty_ydvnc_rfb_client *c,
                                                         uint16_t w, uint16_t h)
{
    if (w == 0 || h == 0 || w > 8192 || h > 8192) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: implausible framebuffer size");
    }
    if (c->fb_width == w && c->fb_height == h && c->fb_texture) {
        return YETTY_OK_VOID();
    }
    c->fb_width = w;
    c->fb_height = h;

    if (c->bind_group) {
        wgpuBindGroupRelease(c->bind_group);
        c->bind_group = NULL;
    }
    if (c->fb_view) {
        wgpuTextureViewRelease(c->fb_view);
        c->fb_view = NULL;
    }
    if (c->fb_texture) {
        wgpuTextureRelease(c->fb_texture);
        c->fb_texture = NULL;
    }

    WGPUTextureDescriptor td = {0};
    td.size = (WGPUExtent3D){w, h, 1};
    td.format = WGPUTextureFormat_BGRA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    c->fb_texture = wgpuDeviceCreateTexture(c->device, &td);
    if (!c->fb_texture) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: createTexture failed");
    }
    c->fb_view = wgpuTextureCreateView(c->fb_texture, NULL);
    if (!c->fb_view) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: createTextureView failed");
    }

    /* Clear texture to opaque black so unwritten regions don't show GPU
     * garbage before the first FramebufferUpdate lands. */
    {
        size_t row = (size_t)w * 4u;
        size_t bytes = row * (size_t)h;
        uint8_t *clr = calloc(1, bytes);
        if (clr) {
            WGPUTexelCopyTextureInfo dst = {0};
            dst.texture = c->fb_texture;
            WGPUTexelCopyBufferLayout layout = {0};
            layout.bytesPerRow = (uint32_t)row;
            layout.rowsPerImage = h;
            WGPUExtent3D extent = {w, h, 1};
            wgpuQueueWriteTexture(c->queue, &dst, clr, bytes, &layout, &extent);
            free(clr);
        }
    }

    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_gpu_pipeline(c), "ensure_gpu_pipeline failed");

    WGPUBindGroupEntry e[2] = {0};
    e[0].binding = 0;
    e[0].textureView = c->fb_view;
    e[1].binding = 1;
    e[1].sampler = c->sampler;
    WGPUBindGroupDescriptor bd = {0};
    bd.layout = c->bgl;
    bd.entryCount = 2;
    bd.entries = e;
    c->bind_group = wgpuDeviceCreateBindGroup(c->device, &bd);
    if (!c->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: createBindGroup failed");
    }

    yinfo("ydvnc: framebuffer resized to %ux%u", w, h);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_gpu_pipeline(struct yetty_ydvnc_rfb_client *c)
{
    if (c->pipeline) {
        return YETTY_OK_VOID();
    }

    if (!c->sampler) {
        WGPUSamplerDescriptor sd = {0};
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge;
        sd.addressModeV = WGPUAddressMode_ClampToEdge;
        sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.maxAnisotropy = 1;
        c->sampler = wgpuDeviceCreateSampler(c->device, &sd);
        if (!c->sampler) {
            return YETTY_ERR(yetty_ycore_void, "ydvnc: createSampler failed");
        }
    }

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){.data = YDVNC_QUAD_WGSL, .length = strlen(YDVNC_QUAD_WGSL)};
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&wgsl;
    WGPUShaderModule sm = wgpuDeviceCreateShaderModule(c->device, &smd);
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: createShaderModule failed");
    }

    WGPUBindGroupLayoutEntry bge[2] = {0};
    bge[0].binding = 0;
    bge[0].visibility = WGPUShaderStage_Fragment;
    bge[0].texture.sampleType = WGPUTextureSampleType_Float;
    bge[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    bge[1].binding = 1;
    bge[1].visibility = WGPUShaderStage_Fragment;
    bge[1].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor bgld = {0};
    bgld.entryCount = 2;
    bgld.entries = bge;
    c->bgl = wgpuDeviceCreateBindGroupLayout(c->device, &bgld);
    if (!c->bgl) {
        wgpuShaderModuleRelease(sm);
        return YETTY_ERR(yetty_ycore_void, "ydvnc: createBindGroupLayout failed");
    }

    WGPUPipelineLayoutDescriptor pld = {0};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &c->bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(c->device, &pld);

    WGPUColorTargetState ct = {0};
    ct.format = c->surface_format;
    ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {0};
    fs.module = sm;
    fs.entryPoint = (WGPUStringView){.data = "fs_main", .length = 7};
    fs.targetCount = 1;
    fs.targets = &ct;
    WGPURenderPipelineDescriptor rpd = {0};
    rpd.layout = pl;
    rpd.vertex.module = sm;
    rpd.vertex.entryPoint = (WGPUStringView){.data = "vs_main", .length = 7};
    rpd.fragment = &fs;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.multisample.count = 1;
    rpd.multisample.mask = ~0u;
    c->pipeline = wgpuDeviceCreateRenderPipeline(c->device, &rpd);

    wgpuShaderModuleRelease(sm);
    wgpuPipelineLayoutRelease(pl);

    if (!c->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: createRenderPipeline failed");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ydvnc_rfb_client_ptr_result yetty_ydvnc_rfb_client_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_yevent_event_loop *event_loop)
{
    if (!event_loop) {
        return YETTY_ERR(yetty_ydvnc_rfb_client_ptr, "ydvnc: event_loop is NULL");
    }
    struct yetty_ydvnc_rfb_client *c = calloc(1, sizeof(*c));
    if (!c) {
        return YETTY_ERR(yetty_ydvnc_rfb_client_ptr, "ydvnc: alloc failed");
    }
    c->device = device;
    c->queue = queue;
    c->surface_format = surface_format;
    c->event_loop = event_loop;
    c->state = YDVNC_ST_DISCONNECTED;
    c->rb_cap = YDVNC_RECV_BUF_INITIAL;
    c->rb = malloc(c->rb_cap);
    if (!c->rb) {
        free(c);
        return YETTY_ERR(yetty_ydvnc_rfb_client_ptr, "ydvnc: recv buffer alloc failed");
    }

    struct yetty_ydvnc_transport_ptr_result t_res =
        yetty_ydvnc_transport_tcp_create(event_loop);
    if (YETTY_IS_ERR(t_res)) {
        free(c->rb);
        free(c);
        return YETTY_ERR(yetty_ydvnc_rfb_client_ptr, "ydvnc: transport create failed", t_res);
    }
    c->transport = t_res.value;
    c->transport_owned = 1;

    return YETTY_OK(yetty_ydvnc_rfb_client_ptr, c);
}

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_destroy(struct yetty_ydvnc_rfb_client *c)
{
    if (!c) {
        return YETTY_OK_VOID();
    }
    if (c->transport && c->transport_owned) {
        struct yetty_ycore_void_result r = c->transport->ops->destroy(c->transport);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
    if (c->bind_group) wgpuBindGroupRelease(c->bind_group);
    if (c->bgl) wgpuBindGroupLayoutRelease(c->bgl);
    if (c->pipeline) wgpuRenderPipelineRelease(c->pipeline);
    if (c->sampler) wgpuSamplerRelease(c->sampler);
    if (c->fb_view) wgpuTextureViewRelease(c->fb_view);
    if (c->fb_texture) wgpuTextureRelease(c->fb_texture);

    if (c->password) {
        /* Best-effort scrub before free. */
        memset(c->password, 0, strlen(c->password));
        free(c->password);
    }
    free(c->rect_buf);
    free(c->rb);
    free(c);
    return YETTY_OK_VOID();
}

void yetty_ydvnc_rfb_client_set_password(struct yetty_ydvnc_rfb_client *c, const char *password)
{
    if (!c) return;
    if (c->password) {
        memset(c->password, 0, strlen(c->password));
        free(c->password);
        c->password = NULL;
    }
    if (password && password[0]) {
        c->password = strdup(password);
    }
}

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_connect(
    struct yetty_ydvnc_rfb_client *c, const char *host, uint16_t port)
{
    if (!c || !host) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: NULL client or host");
    }
    struct yetty_ydvnc_transport_callbacks cbs = {
        .ctx = c,
        .on_connect = on_transport_connect,
        .on_connect_error = on_transport_connect_error,
        .on_data = on_transport_data,
        .on_disconnect = on_transport_disconnect,
    };
    return c->transport->ops->connect(c->transport, host, port, &cbs);
}

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_disconnect(struct yetty_ydvnc_rfb_client *c)
{
    if (!c) {
        return YETTY_OK_VOID();
    }
    if (c->transport) {
        return c->transport->ops->disconnect(c->transport);
    }
    return YETTY_OK_VOID();
}

int yetty_ydvnc_rfb_client_is_connected(const struct yetty_ydvnc_rfb_client *c)
{
    return c && c->state == YDVNC_ST_MAIN;
}

uint16_t yetty_ydvnc_rfb_client_width(const struct yetty_ydvnc_rfb_client *c)
{
    return c ? c->fb_width : 0;
}

uint16_t yetty_ydvnc_rfb_client_height(const struct yetty_ydvnc_rfb_client *c)
{
    return c ? c->fb_height : 0;
}

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_render(struct yetty_ydvnc_rfb_client *c,
                                                             WGPURenderPassEncoder pass)
{
    if (!c || !c->pipeline || !c->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "ydvnc: render before resources ready");
    }
    wgpuRenderPassEncoderSetPipeline(pass, c->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, c->bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    return YETTY_OK_VOID();
}

void yetty_ydvnc_rfb_client_set_on_frame(struct yetty_ydvnc_rfb_client *c,
                                         yetty_ydvnc_on_frame_fn cb, void *ud)
{
    if (!c) return;
    c->on_frame = cb;
    c->on_frame_userdata = ud;
}
void yetty_ydvnc_rfb_client_set_on_connected(struct yetty_ydvnc_rfb_client *c,
                                             yetty_ydvnc_on_connected_fn cb, void *ud)
{
    if (!c) return;
    c->on_connected = cb;
    c->on_connected_userdata = ud;
}
void yetty_ydvnc_rfb_client_set_on_disconnected(struct yetty_ydvnc_rfb_client *c,
                                                yetty_ydvnc_on_disconnected_fn cb, void *ud)
{
    if (!c) return;
    c->on_disconnected = cb;
    c->on_disconnected_userdata = ud;
}

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_send_pointer(
    struct yetty_ydvnc_rfb_client *c, int16_t x, int16_t y, uint8_t button_mask)
{
    if (!c || c->state != YDVNC_ST_MAIN) {
        return YETTY_OK_VOID();
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    struct yetty_ydvnc_rfb_pointer_event_msg msg = {0};
    msg.message_type = YETTY_YDVNC_RFB_C2S_POINTER_EVENT;
    msg.button_mask = button_mask;
    msg.x = htons((uint16_t)x);
    msg.y = htons((uint16_t)y);
    return transport_send(c, &msg, sizeof(msg));
}

struct yetty_ycore_void_result yetty_ydvnc_rfb_client_send_key(
    struct yetty_ydvnc_rfb_client *c, uint32_t keysym, int down)
{
    if (!c || c->state != YDVNC_ST_MAIN) {
        return YETTY_OK_VOID();
    }
    if (keysym == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydvnc_rfb_key_event_msg msg = {0};
    msg.message_type = YETTY_YDVNC_RFB_C2S_KEY_EVENT;
    msg.down_flag = down ? 1 : 0;
    msg.key = htonl(keysym);
    return transport_send(c, &msg, sizeof(msg));
}
