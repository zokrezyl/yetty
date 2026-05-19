#ifndef YETTY_YVIDEO_YVIDEO_H
#define YETTY_YVIDEO_YVIDEO_H

/*
 * yvideo — high-level API for producing a yvideo complex primitive from
 * a raw H.264 Annex-B byte stream.
 *
 * Pipeline:
 *   caller supplies H.264 NAL bytes (raw file or in-memory buffer)
 *   yetty_yvideo_uniforms_serialize(uniforms, nal_stream) → wire bytes
 *   wrap in CMD_GROUP(id) + the prim                       → draw_list
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
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yvideo/yvideo-gen.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flag bits — wire-format `flags` uniform. */
#define YETTY_YVIDEO_FLAG_LOOP     0x1u
#define YETTY_YVIDEO_FLAG_AUTOPLAY 0x2u

/* Geometry + playback config. NULL fields fall back to defaults. */
struct yetty_yvideo_render_config {
    float bounds_x;     /* 0    — overridden by canvas at render time */
    float bounds_y;     /* 0    — overridden by canvas at render time */
    float bounds_w;     /* 0    — when 0, defaults to video_w */
    float bounds_h;     /* 0    — when 0, defaults to video_h */
    uint32_t video_w;   /* required (>0) — H.264 SPS dimensions */
    uint32_t video_h;   /* required (>0) — H.264 SPS dimensions */
    float fps;          /* 30.0 — playback rate */
    uint32_t color_matrix; /* 1 (BT.709) */
    uint32_t flags;     /* default LOOP|AUTOPLAY */
};

/*
 * Build a draw_list holding ONE yvideo complex prim that carries the
 * supplied NAL bytes as its initial chunk. The caller owns the returned
 * buffer and frees it with yetty_ydraw_draw_list_destroy.
 *
 * The first envelope a sender ships should typically wrap this prim in
 * a CMD_GROUP(id) so subsequent CMD_UPDATE envelopes can target it; the
 * helper below is intentionally low-level and does NOT wrap — callers
 * (or the demo driver) compose the GROUP themselves.
 */
struct yetty_ydraw_draw_list_result yetty_yvideo_render(
    const uint8_t *nal_bytes, size_t nal_len,
    const struct yetty_yvideo_render_config *config);

/* OSC envelope (YETTY_OSC_YDRAW_BIN, same wire format as ycat / yecho).
 * Returns bytes written; ERR on failure. */
struct yetty_ycore_size_result yetty_yvideo_osc_bin_emit(
    const struct yetty_ydraw_draw_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVIDEO_YVIDEO_H */
