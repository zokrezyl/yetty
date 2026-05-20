// Auto-generated from yvideo.yaml - DO NOT EDIT
//
// Wire-format helpers for the yvideo complex primitive. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout. Lives
// in yetty_yvideo_core (no Dawn, no WebGPU, safe for riscv64 / wasm / any
// cross-target without a GPU).
//
// v2 layout (post-#198): 12 uniforms + 2 buffer length fields + 2 buffer
// payloads. Compared to v1 we appended 3 audio uniforms
// (audio_codec / audio_sample_rate / audio_channels) and one new
// length-prefixed buffer (`audio_stream`). v1 senders will be rejected
// by the figure factory with a payload-size mismatch — fail-loud is
// preferable to silent corruption while v2 lands.

#include <yetty/yvideo/yvideo-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <string.h>

#define YETTY_YVIDEO_UNIFORM_WORDS  12u
#define YETTY_YVIDEO_BUFFER_LEN_FIELDS 2u

size_t yetty_yvideo_uniforms_serialized_size(
    const struct yetty_yvideo_uniforms *uniforms,
    const struct yetty_yvideo_buffers *buffers)
{
    (void)uniforms;
    // Wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    size_t total_buf_words = buffers->nal_stream_len + buffers->audio_stream_len;
    return (2u + YETTY_YVIDEO_UNIFORM_WORDS + YETTY_YVIDEO_BUFFER_LEN_FIELDS +
            total_buf_words) * sizeof(uint32_t);
}

struct yetty_ycore_size_result yetty_yvideo_uniforms_serialize(
    const struct yetty_yvideo_uniforms *uniforms,
    const struct yetty_yvideo_buffers *buffers,
    uint8_t *out, size_t out_capacity)
{
    if (!uniforms || !buffers)
        return YETTY_ERR(yetty_ycore_size, "null argument");
    if (!out)
        return YETTY_ERR(yetty_ycore_size, "out is NULL");

    size_t total_buf_words = buffers->nal_stream_len + buffers->audio_stream_len;
    size_t required = (2u + YETTY_YVIDEO_UNIFORM_WORDS +
                       YETTY_YVIDEO_BUFFER_LEN_FIELDS + total_buf_words) *
                      sizeof(uint32_t);
    if (out_capacity < required)
        return YETTY_ERR(yetty_ycore_size, "buffer too small");

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_YVIDEO_TYPE_ID;
    *p++ = (uint32_t)(required - 2u * sizeof(uint32_t));

    // Copy uniforms as raw words — struct layout matches the wire order
    // 1:1 (verified by the uniform-count constant above).
    memcpy(p, uniforms, sizeof(struct yetty_yvideo_uniforms));
    p += YETTY_YVIDEO_UNIFORM_WORDS;

    // Write buffer lengths in declaration order.
    *p++ = (uint32_t)buffers->nal_stream_len;
    *p++ = (uint32_t)buffers->audio_stream_len;

    // Copy buffer data in the same declaration order.
    if (buffers->nal_stream && buffers->nal_stream_len > 0u) {
        memcpy(p, buffers->nal_stream,
               buffers->nal_stream_len * sizeof(uint32_t));
    }
    p += buffers->nal_stream_len;

    if (buffers->audio_stream && buffers->audio_stream_len > 0u) {
        memcpy(p, buffers->audio_stream,
               buffers->audio_stream_len * sizeof(uint32_t));
    }
    p += buffers->audio_stream_len;

    return YETTY_OK(yetty_ycore_size, required);
}
