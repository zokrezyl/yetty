// Auto-generated from yvideo.yaml - DO NOT EDIT
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared so this header stays GPU-less and can be included by
 * client-side wire emitters that don't link Dawn. The full type lives in
 * yetty/ydraw-factory/figure-factory.h (server side). */
struct yetty_ydraw_concrete_factory;

#define YETTY_YVIDEO_TYPE_ID 0x80000006u

// Uniforms struct (goes to GPU uniform buffer). Field order must match
// yvideo.yaml `uniforms:` 1:1 — yvideo-gen-wire.c serialises the struct
// as raw u32 words.
struct yetty_yvideo_uniforms {
    float bounds_x;
    float bounds_y;
    float bounds_w;
    float bounds_h;
    uint32_t video_w;
    uint32_t video_h;
    uint32_t chroma_w; /* = video_w / 2 for YUV 4:2:0 */
    uint32_t chroma_h; /* = video_h / 2 for YUV 4:2:0 */
    float fps;
    uint32_t color_matrix;
    uint32_t flags;
    /* v2: audio. audio_codec=0 → video-only (audio_stream is empty). */
    uint32_t audio_codec;       /* yetty_yacodec_codec enum value */
    uint32_t audio_sample_rate; /* Hz */
    uint32_t audio_channels;    /* 1 or 2 */
};

// Buffers struct (goes to GPU storage buffer)
struct yetty_yvideo_buffers {
    const uint32_t *nal_stream;
    size_t nal_stream_len; /* count of u32 words */
    /* v2: interleaved length-prefixed audio packets — each is
     * (u32 length, length bytes padded to u32 alignment). The factory
     * walks them on decode. */
    const uint32_t *audio_stream;
    size_t audio_stream_len; /* count of u32 words */
};

//=============================================================================
// Serialization API
//=============================================================================

size_t yetty_yvideo_uniforms_serialized_size(const struct yetty_yvideo_uniforms *uniforms,
                                             const struct yetty_yvideo_buffers *buffers);

struct yetty_ycore_size_result yetty_yvideo_uniforms_serialize(
    const struct yetty_yvideo_uniforms *uniforms, const struct yetty_yvideo_buffers *buffers,
    uint8_t *out, size_t out_capacity);

//=============================================================================
// Factory API (creates binder with pre-compiled pipeline)
//=============================================================================

struct yetty_ydraw_concrete_factory *yetty_yvideo_factory_create(void);
void yetty_yvideo_factory_destroy(struct yetty_ydraw_concrete_factory *factory);

#ifdef __cplusplus
}
#endif
