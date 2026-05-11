// Auto-generated from yplot.yaml - DO NOT EDIT
//
// Wire-format helpers for the yplot complex primitive. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout used
// by ycat / yecho / yplot CLI / demos. Lives in yetty_yplot_core (no Dawn,
// no WebGPU, safe for riscv64 / wasm / any cross-target without a GPU).

#include <yetty/yplot/yplot-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t yetty_yplot_uniforms_serialized_size(const struct yetty_yplot_uniforms *uniforms,
                                            const struct yetty_yplot_buffers *buffers)
{
    (void)uniforms;
    // Wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    size_t total_buf_words = buffers->bytecode_len;
    return (2 + 18 + 1 + total_buf_words) * sizeof(uint32_t);
}

struct yetty_ycore_size_result yetty_yplot_uniforms_serialize(
    const struct yetty_yplot_uniforms *uniforms, const struct yetty_yplot_buffers *buffers,
    uint8_t *out, size_t out_capacity)
{
    if (!uniforms || !buffers) {
        return YETTY_ERR(yetty_ycore_size, "null argument");
    }
    if (!out) {
        return YETTY_ERR(yetty_ycore_size, "out is NULL");
    }

    size_t total_buf_words = buffers->bytecode_len;
    size_t required = (2 + 18 + 1 + total_buf_words) * sizeof(uint32_t);
    if (out_capacity < required) {
        return YETTY_ERR(yetty_ycore_size, "buffer too small");
    }

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_YPLOT_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    // Copy uniforms as raw words
    memcpy(p, uniforms, sizeof(struct yetty_yplot_uniforms));
    p += 18;

    // Write buffer lengths
    *p++ = (uint32_t)buffers->bytecode_len;

    // Copy buffer data
    if (buffers->bytecode && buffers->bytecode_len > 0) {
        memcpy(p, buffers->bytecode, buffers->bytecode_len * sizeof(uint32_t));
    }
    p += buffers->bytecode_len;

    return YETTY_OK(yetty_ycore_size, required);
}
