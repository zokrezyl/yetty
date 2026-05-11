// Auto-generated from yimage.yaml - DO NOT EDIT
//
// Wire-format helpers for the yimage complex primitive. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout used
// by ycat / ylexbor / demos. Lives in yetty_yimage_core (no Dawn, no
// WebGPU, safe for riscv64 / wasm / any cross-target without a GPU).

#include <yetty/yimage/yimage-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <string.h>

size_t yetty_yimage_uniforms_serialized_size(const struct yetty_yimage_uniforms *uniforms,
                                             const struct yetty_yimage_buffers *buffers)
{
    (void)uniforms;
    // Wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    size_t total_buf_words = buffers->pixels_len;
    return (2 + 6 + 1 + total_buf_words) * sizeof(uint32_t);
}

struct yetty_ycore_size_result yetty_yimage_uniforms_serialize(
    const struct yetty_yimage_uniforms *uniforms, const struct yetty_yimage_buffers *buffers,
    uint8_t *out, size_t out_capacity)
{
    if (!uniforms || !buffers) {
        return YETTY_ERR(yetty_ycore_size, "null argument");
    }
    if (!out) {
        return YETTY_ERR(yetty_ycore_size, "out is NULL");
    }

    size_t total_buf_words = buffers->pixels_len;
    size_t required = (2 + 6 + 1 + total_buf_words) * sizeof(uint32_t);
    if (out_capacity < required) {
        return YETTY_ERR(yetty_ycore_size, "buffer too small");
    }

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_YIMAGE_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    // Copy uniforms as raw words
    memcpy(p, uniforms, sizeof(struct yetty_yimage_uniforms));
    p += 6;

    // Write buffer lengths
    *p++ = (uint32_t)buffers->pixels_len;

    // Copy buffer data
    if (buffers->pixels && buffers->pixels_len > 0) {
        memcpy(p, buffers->pixels, buffers->pixels_len * sizeof(uint32_t));
    }
    p += buffers->pixels_len;

    return YETTY_OK(yetty_ycore_size, required);
}
