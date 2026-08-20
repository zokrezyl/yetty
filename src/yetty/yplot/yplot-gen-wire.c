// Auto-generated from yplot.yaml - DO NOT EDIT
//
// Wire-format helpers for the yplot complex. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout. Lives
// in yetty_yplot_core (no Dawn, no WebGPU, safe for riscv64 / wasm / any
// cross-target without a GPU).
//
// Wire layout (u32 words):
//   [0]              type_id
//   [1]              payload_size (bytes after this header)
//   [2 ..]           uniforms (YETTY_YPLOT_UNIFORMS_WORDS words)
//   --- storage payload (handed verbatim to the GPU storage_buffer) ---
//   per fixed buffer: [len][data...]
//   [data_count], then per entry: [len_i][samples_i...]

#include <yetty/yplot/yplot-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <string.h>

/* Total words the storage payload occupies (everything after the uniforms,
 * i.e. what gets handed to the GPU storage_buffer). */
static size_t storage_words(const struct yetty_yplot_buffers *buffers)
{
    size_t w = 1 /* data_count */;
    w += 1 /* bytecode_len */ + buffers->bytecode_len;
    for (size_t i = 0; i < buffers->data_count; i++) {
        w += 1 /* len_i */ + buffers->data[i].count;
    }
    return w;
}

size_t yetty_yplot_uniforms_serialized_size(const struct yetty_yplot_uniforms *uniforms,
                                            const struct yetty_yplot_buffers *buffers)
{
    (void)uniforms;
    size_t total_words = 2 /* type_id + payload_size */
                         + YETTY_YPLOT_UNIFORMS_WORDS + storage_words(buffers);
    return total_words * sizeof(uint32_t);
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

    size_t required = yetty_yplot_uniforms_serialized_size(uniforms, buffers);
    if (out_capacity < required) {
        return YETTY_ERR(yetty_ycore_size, "buffer too small");
    }

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_YPLOT_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    /* Uniforms: copy as raw u32 words — all 4-byte scalars, packs without
     * padding, so a memcpy reproduces the wire layout exactly. */
    memcpy(p, uniforms, YETTY_YPLOT_UNIFORMS_WORDS * sizeof(uint32_t));
    p += YETTY_YPLOT_UNIFORMS_WORDS;

    /* Storage payload: fixed buffers first, then the variable list. */
    *p++ = (uint32_t)buffers->bytecode_len;
    if (buffers->bytecode && buffers->bytecode_len > 0) {
        memcpy(p, buffers->bytecode, buffers->bytecode_len * sizeof(uint32_t));
        p += buffers->bytecode_len;
    }

    *p++ = (uint32_t)buffers->data_count;
    for (size_t i = 0; i < buffers->data_count; i++) {
        const struct yetty_yplot_data_buffer *entry = &buffers->data[i];
        *p++ = (uint32_t)entry->count;
        if (entry->samples && entry->count > 0) {
            memcpy(p, entry->samples, entry->count * sizeof(float));
            p += entry->count;
        }
    }

    return YETTY_OK(yetty_ycore_size, required);
}
