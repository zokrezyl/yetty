/*
 * handler-video.c - H.264 Annex-B bytes → yvideo OSC envelope.
 *
 * Compiled only when YETTY_YCAT_HAS_YVIDEO is set (yvideo + yvcodec
 * present at link time). The handler forwards the raw NAL bytes to
 * yetty_yvideo_render, which wraps them in a yvideo prim. We pull the
 * source pixel dimensions from the caller's config:
 *
 *   width_cells  * cell_width   → video_w
 *   height_cells * cell_height  → video_h
 *
 * If either is zero, we punt on a safe default (640×360) and emit a
 * yerror. Proper SPS-based dimension extraction needs an Exp-Golomb
 * decoder; tracked separately. (#198 item 6 future work.)
 */

#include <yetty/ycat/ycat.h>

#include <yetty/yvideo/yvideo.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <string.h>

struct yetty_ydraw_draw_list_result yetty_ycat_handler_video(const uint8_t *bytes, size_t len,
                                                             const char *path_hint,
                                                             const struct yetty_ycat_config *config)
{
    (void)path_hint;
    if (!bytes || len == 0u) {
        return YETTY_ERR(yetty_ydraw_draw_list, "ycat video: no bytes");
    }

    uint32_t video_w = 0u;
    uint32_t video_h = 0u;
    if (config) {
        if (config->width_cells > 0u && config->cell_width > 0u) {
            video_w = config->width_cells * config->cell_width;
        }
        if (config->height_cells > 0u && config->cell_height > 0u) {
            video_h = config->height_cells * config->cell_height;
        }
    }
    if (video_w == 0u || video_h == 0u) {
        yerror("ycat video: source dimensions unknown — defaulting to 640×360. "
               "Pass --width / --height (or set CELL_W × WIDTH_CELLS) to override.");
        video_w = 640u;
        video_h = 360u;
    }
    /* Force even dimensions — H.264 constraint, also required for the
     * yvideo 4:2:0 chroma plane sizing. */
    video_w &= ~1u;
    video_h &= ~1u;

    struct yetty_yvideo_render_config cfg = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = (float)video_w,
        .bounds_h = (float)video_h,
        .video_w = video_w,
        .video_h = video_h,
        .fps = 30.0f,
        .color_matrix = 1u, /* BT.709 */
        .flags = YETTY_YVIDEO_FLAG_LOOP | YETTY_YVIDEO_FLAG_AUTOPLAY,
        .audio_codec = 0u,
        .audio_sample_rate = 0u,
        .audio_channels = 0u,
    };
    return yetty_yvideo_render(bytes, len, NULL, 0u, &cfg);
}
