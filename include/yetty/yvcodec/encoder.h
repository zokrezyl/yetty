/*
 * yvcodec/encoder.h - H.264 video encoder (openh264 backend).
 *
 * C API over a C++ implementation. Suitable for:
 *   - streaming the VNC framebuffer over H.264 (vnc-server)
 *   - recording terminal sessions for demos / replay
 */

#ifndef YETTY_YVCODEC_ENCODER_H
#define YETTY_YVCODEC_ENCODER_H

#include <yetty/ycore/result.h>
#include <yetty/yvcodec/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvcodec_encoder;

YETTY_YRESULT_DECLARE(yetty_yvcodec_encoder_ptr, struct yetty_yvcodec_encoder *);

/* Encoder configuration. All fields required except frame_rate / idr_interval /
 * screen_content which have sensible defaults in _create_default(). */
struct yetty_yvcodec_encoder_config {
    uint32_t width;
    uint32_t height;
    uint32_t bitrate;      /* bits/second (typical screen: 2-4 Mbps) */
    float frame_rate;      /* fps */
    uint32_t idr_interval; /* keyframe period in frames */
    bool screen_content;   /* SCREEN_CONTENT_REAL_TIME in openh264 */
};

/* Helper: fill `cfg` with reasonable defaults for the given resolution. */
void yetty_yvcodec_encoder_config_defaults(struct yetty_yvcodec_encoder_config *cfg, uint32_t width,
                                           uint32_t height);

/*
 * Create an H.264 encoder. Width and height must be even (H.264 constraint);
 * callers should round down before passing them in.
 */
struct yetty_yvcodec_encoder_ptr_result yetty_yvcodec_encoder_config_encoder_create(
    const struct yetty_yvcodec_encoder_config *cfg);

void yetty_yvcodec_encoder_destroy(struct yetty_yvcodec_encoder *enc);

/*
 * Encode one YUV420 frame.
 *
 * On success `*out` is populated; `out->data` points into the encoder's
 * internal buffer and is valid until the next encode()/destroy() call. Copy
 * the bytes out before touching the encoder again if you need to retain them.
 *
 * A rate-control skip surfaces as `out->size == 0` — treat it as "no frame
 * this tick, resubmit a catch-up later".
 */
struct yetty_ycore_void_result yetty_yvcodec_encoder_encode(
    struct yetty_yvcodec_encoder *enc, const uint8_t *y_plane, const uint8_t *u_plane,
    const uint8_t *v_plane, uint32_t y_stride, uint32_t uv_stride,
    struct yetty_yvcodec_encoded_frame *out);

/* Force the next encoded frame to be an IDR (keyframe). Used on new VNC
 * client connect so the newcomer gets a valid stream from frame 1. */
void yetty_yvcodec_encoder_force_idr(struct yetty_yvcodec_encoder *enc);

/* Update target bitrate in bps. */
struct yetty_ycore_void_result yetty_yvcodec_encoder_set_bitrate(struct yetty_yvcodec_encoder *enc,
                                                                 uint32_t bitrate);

uint32_t yetty_yvcodec_encoder_width(const struct yetty_yvcodec_encoder *enc);
uint32_t yetty_yvcodec_encoder_height(const struct yetty_yvcodec_encoder *enc);

/*---------------------------------------------------------------------------
 * Color conversion helpers — pure C, usable without an encoder instance.
 *-------------------------------------------------------------------------*/

/*
 * Convert BGRA8 pixels to YUV420 (BT.709, video range) using a scalar CPU
 * path. Output planes and strides are caller-allocated. This is what the VNC
 * server uses before handing pixels to yetty_yvcodec_encoder_encode().
 */
void yetty_yvcodec_bgra_to_yuv420(const uint8_t *bgra, uint32_t width, uint32_t height,
                                  uint32_t bgra_stride, uint8_t *y_plane, uint8_t *u_plane,
                                  uint8_t *v_plane, uint32_t y_stride, uint32_t uv_stride);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVCODEC_ENCODER_H */
