#ifndef YETTY_YVIDEO_YVIDEO_MP4_H
#define YETTY_YVIDEO_YVIDEO_MP4_H

/*
 * yvideo-mp4 — MP4 → Annex-B convenience for senders.
 *
 * Demuxes an MP4 container with minimp4, parses the first SPS NAL for
 * the video dimensions, and builds a drawable_list holding ONE yvideo
 * complex prim from the result. Single source of truth for MP4
 * ingestion on the sender side — every consumer (tools/yvideo, the
 * ygui_yvideo widget, future apps) calls these instead of carrying
 * its own copy of the demuxer.
 *
 * v1 placement note: ideally the demuxer would run server-side as
 * part of the yvideo figure factory, so senders only ship the
 * container bytes and the wire spec stays codec-aware rather than
 * codec-cooked. That requires reworking how the binder discovers
 * video_w/video_h (today they're read directly from the wire payload
 * on every render — see src/yetty/yvideo/yvideo-gen.c — so the wire
 * format would need a `media_format` discriminator and the create
 * hook would need to write the demuxed dims back into buffer_data
 * before binder->submit). Tracked separately — for v1 the demuxer
 * lives here, next to the figure, but executes sender-side.
 *
 * Available only when the build was linked against minimp4 (the
 * functions are still declared so callers don't need their own
 * ifdef; absent the library they return ERR / NULL).
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yvideo/yvideo.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demux `mp4_bytes` + render a yvideo prim in one shot. video_w /
 * video_h come from the parsed SPS — any values in `overrides` for
 * those fields are ignored. Other non-zero fields in `overrides`
 * (bounds, fps, color_matrix, flags, audio_*) replace the defaults
 * (30 fps, BT.709, LOOP|AUTOPLAY, no audio). `overrides` may be NULL.
 *
 * Caller owns the returned drawable_list (free with
 * yetty_ydraw_drawable_list_destroy). */
struct yetty_ydraw_drawable_list_result yetty_yvideo_render_from_mp4_bytes(
    const uint8_t *mp4_bytes, size_t mp4_len, const struct yetty_yvideo_render_config *overrides);

/* Same as above, sourcing the bytes from a file path. The file is
 * slurped into memory first; minimp4 needs a byte-addressable buffer. */
struct yetty_ydraw_drawable_list_result yetty_yvideo_render_from_mp4_file(
    const char *path, const struct yetty_yvideo_render_config *overrides);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVIDEO_YVIDEO_MP4_H */
