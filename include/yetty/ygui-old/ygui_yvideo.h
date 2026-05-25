#ifndef YETTY_YGUI_OLD_YGUI_YVIDEO_H
#define YETTY_YGUI_OLD_YGUI_YVIDEO_H

/*
 * ygui_yvideo — yvideo widget for ygui.
 *
 * Wraps a yvideo complex prim in a RICH-style widget. The H.264
 * Annex-B byte stream + SPS dimensions are baked into the INIT
 * envelope at construction time; the receiving scene-canvas decodes
 * via openh264 and plays back through its per-instance animation
 * timer (LOOP|AUTOPLAY by default).
 *
 * Three constructor paths:
 *
 *   from_h264       low-level — supply raw NAL bytes + a render_config
 *                   whose `video_w` / `video_h` match the SPS.
 *
 *   from_mp4_bytes  delegates to yetty_yvideo_render_from_mp4_bytes
 *                   (see yvideo-mp4.h). Demuxes server-bound bytes
 *                   sender-side until v2 lifts the demux into the
 *                   yvideo figure itself.
 *
 *   from_mp4_file   same, but slurps the file first.
 *
 * Both MP4 constructors return NULL when the build wasn't linked
 * against minimp4 (i.e. yvideo-mp4 isn't compiled in).
 *
 * The set_* / clear helpers replace the widget's content while
 * keeping the widget id stable. On the wire the receiver sees the
 * new INIT prim under the same CMD_GROUP — the previous yvideo
 * entity is torn down (decoder, audio device, animation
 * subscription) and a fresh one is created with the new bytes.
 *
 * Streaming append-NAL / append-audio and playback-control envelopes
 * (SEEK_PTS_MS, SET_PLAYING, SET_SPEED, SET_LOOP) are NOT exposed
 * here yet — they require routing CMD_UPDATE envelopes through the
 * engine's OSC pipe, which the widget layer doesn't surface. Future
 * work.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yvideo/yvideo.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_old_engine;
struct yetty_ygui_old_widget;

/* Create a yvideo widget from a raw H.264 Annex-B byte stream. The
 * config carries video_w / video_h (the SPS dimensions); a NULL
 * config or video_w/video_h == 0 is an error and the function
 * returns NULL. `bounds_w` / `bounds_h` of 0 in the config default
 * to the widget's (w, h). */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yvideo_from_h264(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const uint8_t *nal_bytes, size_t nal_len, const struct yetty_yvideo_render_config *config);

/* Create a yvideo widget from MP4 container bytes. Delegates to
 * yetty_yvideo_render_from_mp4_bytes — SPS dimensions and demuxed
 * NAL bytes come from the helper. `config_overrides` may be NULL;
 * non-zero fields it carries override the defaults (fps, color
 * matrix, flags, audio_*); `video_w` / `video_h` are always taken
 * from the parsed SPS. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yvideo_from_mp4_bytes(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *config_overrides);

/* Same, but sourcing the bytes from a file path. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yvideo_from_mp4_file(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const char *path, const struct yetty_yvideo_render_config *config_overrides);

/* Replace the widget's content with a fresh INIT prim built from
 * the supplied NAL bytes + config. */
struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_set_h264(
    struct yetty_ygui_old_widget *widget, const uint8_t *nal_bytes, size_t nal_len,
    const struct yetty_yvideo_render_config *config);

/* Replace the content with the bytes demuxed from MP4 input. */
struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_set_mp4_bytes(
    struct yetty_ygui_old_widget *widget, const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *config_overrides);

struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_set_mp4_file(
    struct yetty_ygui_old_widget *widget, const char *path,
    const struct yetty_yvideo_render_config *config_overrides);

/* Drop the playback prim — the widget's draw_list is cleared. The
 * receiving canvas tears the yvideo entity down (decoder, audio
 * device, animation subscription all go with it). */
struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_clear(struct yetty_ygui_old_widget *widget);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OLD_YGUI_YVIDEO_H */
