#ifndef YETTY_YVIDEO_YVIDEO_H
#define YETTY_YVIDEO_YVIDEO_H

/*
 * yvideo — high-level API for producing a yvideo composite from
 * a raw H.264 Annex-B byte stream.
 *
 * Pipeline:
 *   caller supplies H.264 NAL bytes (raw file or in-memory buffer)
 *   yetty_yvideo_uniforms_serialize(uniforms, nal_stream) → wire bytes
 *   wrap in CMD_GROUP(id) + the prim                       → drawable_list
 *   yetty_yvideo_osc_bin_emit(...)                         → OSC envelope
 *
 * The figure is STATEFUL on the receiving terminal — once the INIT
 * envelope creates the instance, sender can stream more NAL bytes via
 * CMD_UPDATE envelopes targeting the same id. See
 * demo/yvideo/video-source.c for the reference driver.
 *
 * Only scene-canvas supports CMD_UPDATE today. On scrolling-canvas the
 * INIT envelope still creates a playable instance; UPDATE envelopes
 * are dropped.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yvideo/yvideo-gen.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flag bits — wire-format `flags` uniform. */
#define YETTY_YVIDEO_FLAG_LOOP 0x1u
#define YETTY_YVIDEO_FLAG_AUTOPLAY 0x2u

/*
 * CMD_UPDATE typed-payload op codes (v2, #198 item 3).
 *
 * Every CMD_UPDATE envelope sent to a yvideo figure carries a 4-byte
 * header — [u8 op][u8 reserved[3]] — followed by op-specific body
 * bytes. v1's bare-NAL-bytes payload was retired with v2; senders must
 * use the typed shape.
 *
 *   APPEND_NAL    body = raw H.264 Annex-B bytes
 *   APPEND_AUDIO  body = length-prefixed audio packets
 *                        (same layout as the audio_stream buffer)
 *   SEEK_PTS_MS   body = u32 pts_ms — decoders reset, rings cleared,
 *                        sender should follow with bytes starting from
 *                        an IDR at PTS=pts_ms
 *   SET_PLAYING   body = u8 playing (1=play, 0=pause)
 *   SET_SPEED     body = f32 speed (1.0 = normal; video clock only —
 *                        audio still plays at 1×)
 *   SET_LOOP      body = u8 loop (toggles the LOOP flag bit; actual
 *                        EOS-driven looping is future work)
 */
#define YETTY_YVIDEO_UPDATE_OP_APPEND_NAL 0x00u
#define YETTY_YVIDEO_UPDATE_OP_APPEND_AUDIO 0x01u
#define YETTY_YVIDEO_UPDATE_OP_SEEK_PTS_MS 0x02u
#define YETTY_YVIDEO_UPDATE_OP_SET_PLAYING 0x03u
#define YETTY_YVIDEO_UPDATE_OP_SET_SPEED 0x04u
#define YETTY_YVIDEO_UPDATE_OP_SET_LOOP 0x05u

/* Geometry + playback config. NULL fields fall back to defaults. */
struct yetty_yvideo_render_config {
    float bounds_x;        /* 0    — overridden by canvas at render time */
    float bounds_y;        /* 0    — overridden by canvas at render time */
    float bounds_w;        /* 0    — when 0, defaults to video_w */
    float bounds_h;        /* 0    — when 0, defaults to video_h */
    uint32_t video_w;      /* required (>0) — H.264 SPS dimensions */
    uint32_t video_h;      /* required (>0) — H.264 SPS dimensions */
    float fps;             /* 30.0 — playback rate */
    uint32_t color_matrix; /* 1 (BT.709) */
    uint32_t flags;        /* default LOOP|AUTOPLAY */

    /* v2 audio (per #198 item 2). audio_codec=0 → video-only; the
     * audio_bytes pointer/len pair is ignored in that case. */
    uint32_t audio_codec;       /* yetty_yacodec_codec enum: 0=none 1=opus */
    uint32_t audio_sample_rate; /* required if audio_codec!=0 — 48000 typical */
    uint32_t audio_channels;    /* 1 or 2 */
};

/*
 * Build a drawable_list holding ONE yvideo complex prim that carries the
 * supplied NAL bytes as its initial chunk + (optionally) the initial
 * audio packets. The caller owns the returned buffer and frees it with
 * yetty_ydraw_drawable_list_destroy.
 *
 * `audio_bytes` carries length-prefixed audio packets (each: u32
 * length followed by `length` bytes; the v2 audio_stream buffer
 * layout). NULL/0 when audio_codec=0.
 *
 * The first envelope a sender ships should typically wrap this prim in
 * a CMD_GROUP(id) so subsequent CMD_UPDATE envelopes can target it; the
 * helper below is intentionally low-level and does NOT wrap — callers
 * (or the demo driver) compose the GROUP themselves.
 */
struct yetty_ydraw_drawable_list_result yetty_yvideo_render(
    const uint8_t *nal_bytes, size_t nal_len, const uint8_t *audio_bytes, size_t audio_len,
    const struct yetty_yvideo_render_config *config);

/* OSC envelope (YETTY_OSC_YDRAW_BIN, same wire format as ycat / yecho).
 * Returns bytes written; ERR on failure. */
struct yetty_ycore_size_result yetty_yvideo_osc_bin_emit(const struct yetty_ydraw_drawable_list *buffer,
                                                         FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVIDEO_YVIDEO_H */
