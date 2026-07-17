/*
 * yvideo.c — high-level convenience wrapper around yvideo-gen's wire
 * serializer. Builds a drawable_list holding one yvideo prim from raw
 * H.264 Annex-B bytes + optional audio packets + config. v2 layout
 * (#198 item 2).
 *
 * Senders typically wrap the returned drawable_list in a CMD_GROUP(id) so
 * subsequent CMD_UPDATE envelopes can target the figure; this helper
 * stays low-level and leaves grouping to the caller.
 */

#include <yetty/yvideo/yvideo.h>

#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yetty_ydraw_drawable_list_result yetty_yvideo_render(
    const uint8_t *nal_bytes, size_t nal_len, const uint8_t *audio_bytes, size_t audio_len,
    const struct yetty_yvideo_render_config *config)
{
    if (!config || config->video_w == 0u || config->video_h == 0u) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: config NULL or video_w/h == 0");
    }
    if (config->audio_codec != 0u) {
        if (config->audio_sample_rate == 0u || config->audio_channels == 0u) {
            return YETTY_ERR(yetty_ydraw_drawable_list,
                             "yvideo: audio_codec set but sample_rate/channels not");
        }
    }
    /* nal_bytes==NULL && nal_len==0 is allowed (lets a sender open the
     * stream and append later via CMD_UPDATE). audio_bytes too. */

    struct yetty_yvideo_uniforms u = {0};
    u.bounds_x = config->bounds_x;
    u.bounds_y = config->bounds_y;
    u.bounds_w = (config->bounds_w > 0.0f) ? config->bounds_w : (float)config->video_w;
    u.bounds_h = (config->bounds_h > 0.0f) ? config->bounds_h : (float)config->video_h;
    u.video_w = config->video_w;
    u.video_h = config->video_h;
    /* YUV 4:2:0 chroma — half-res in both dimensions. */
    u.chroma_w = config->video_w / 2u;
    u.chroma_h = config->video_h / 2u;
    u.fps = (config->fps > 0.0f) ? config->fps : 30.0f;
    u.color_matrix = config->color_matrix;
    u.flags = (config->flags != 0u) ? config->flags
                                    : (YETTY_YVIDEO_FLAG_LOOP | YETTY_YVIDEO_FLAG_AUTOPLAY);
    u.audio_codec = config->audio_codec;
    u.audio_sample_rate = config->audio_sample_rate;
    u.audio_channels = config->audio_channels;

    /* Pack each byte stream into a u32-aligned buffer. Trailing 0..3
     * padding bytes are valid for NAL Annex-B (start codes ignored) and
     * for length-prefixed audio packets (the parser walks packet boundaries
     * by the leading u32 length, not by total buffer size). */
    size_t nal_words = (nal_len + 3u) / 4u;
    uint32_t *nal_words_buf = NULL;
    if (nal_len > 0u) {
        nal_words_buf = calloc(nal_words, sizeof(uint32_t));
        if (!nal_words_buf) {
            return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: nal-pack alloc failed");
        }
        if (nal_bytes) {
            memcpy(nal_words_buf, nal_bytes, nal_len);
        }
    }

    size_t audio_words = (audio_len + 3u) / 4u;
    uint32_t *audio_words_buf = NULL;
    if (audio_len > 0u) {
        audio_words_buf = calloc(audio_words, sizeof(uint32_t));
        if (!audio_words_buf) {
            free(nal_words_buf);
            return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: audio-pack alloc failed");
        }
        if (audio_bytes) {
            memcpy(audio_words_buf, audio_bytes, audio_len);
        }
    }

    struct yetty_yvideo_buffers bufs = {
        .nal_stream = nal_words_buf,
        .nal_stream_len = nal_words,
        .audio_stream = audio_words_buf,
        .audio_stream_len = audio_words,
    };

    size_t required = yetty_yvideo_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        free(nal_words_buf);
        free(audio_words_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yvideo_uniforms_serialize(&u, &bufs, prim_buf, required);
    free(nal_words_buf);
    free(audio_words_buf);
    if (YETTY_IS_ERR(ser)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: serialize failed", ser);
    }

    struct yetty_ydraw_drawable_list_config dlcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_x + u.bounds_w,
        .scene_max_y = u.bounds_y + u.bounds_h,
    };
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(br)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: drawable_list create failed", br);
    }

    struct yetty_ydraw_id_result idr =
        yetty_ydraw_drawable_list_add_prim(br.value, prim_buf, required);
    free(prim_buf);
    if (YETTY_IS_ERR(idr)) {
        yetty_ydraw_drawable_list_destroy(br.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yvideo: add_prim failed", idr);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, br.value);
}

struct yetty_ycore_size_result yetty_yvideo_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yvideo_dcs_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "yvideo_dcs_bin_emit: empty serialize");
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yvideo_dcs_bin_emit: yface_emit failed", r);
    }
    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
