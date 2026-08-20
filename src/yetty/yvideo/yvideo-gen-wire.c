// Auto-generated from yvideo.yaml - DO NOT EDIT
//
// Wire-format helpers for the yvideo complex. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout. Lives
// in yetty_yvideo_core (no Dawn, no WebGPU, safe for riscv64 / wasm / any
// cross-target without a GPU).

#include <yetty/yvideo/yvideo-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <string.h>

size_t yetty_yvideo_uniforms_serialized_size(const struct yetty_yvideo_uniforms *uniforms,
                                             const struct yetty_yvideo_buffers *buffers)
{
    (void)uniforms;
    // Wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    size_t total_buf_words = buffers->nal_stream_len + buffers->audio_stream_len;
    return (2 + 14 + 2 + total_buf_words) * sizeof(uint32_t);
}

struct yetty_ycore_size_result yetty_yvideo_uniforms_serialize(
    const struct yetty_yvideo_uniforms *uniforms, const struct yetty_yvideo_buffers *buffers,
    uint8_t *out, size_t out_capacity)
{
    if (!uniforms || !buffers) {
        return YETTY_ERR(yetty_ycore_size, "null argument");
    }
    if (!out) {
        return YETTY_ERR(yetty_ycore_size, "out is NULL");
    }

    size_t total_buf_words = buffers->nal_stream_len + buffers->audio_stream_len;
    size_t required = (2 + 14 + 2 + total_buf_words) * sizeof(uint32_t);
    if (out_capacity < required) {
        return YETTY_ERR(yetty_ycore_size, "buffer too small");
    }

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_YVIDEO_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    // Copy uniforms as raw words
    memcpy(p, uniforms, sizeof(struct yetty_yvideo_uniforms));
    p += 14;

    // Write buffer lengths
    *p++ = (uint32_t)buffers->nal_stream_len;
    *p++ = (uint32_t)buffers->audio_stream_len;

    // Copy buffer data
    if (buffers->nal_stream && buffers->nal_stream_len > 0) {
        memcpy(p, buffers->nal_stream, buffers->nal_stream_len * sizeof(uint32_t));
    }
    p += buffers->nal_stream_len;
    if (buffers->audio_stream && buffers->audio_stream_len > 0) {
        memcpy(p, buffers->audio_stream, buffers->audio_stream_len * sizeof(uint32_t));
    }
    p += buffers->audio_stream_len;

    return YETTY_OK(yetty_ycore_size, required);
}
