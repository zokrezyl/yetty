// Auto-generated from yplot.yaml - DO NOT EDIT
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared so this header stays GPU-less and can be included by
 * client-side wire emitters that don't link Dawn. The full types live in
 * yetty/ydraw-factory/figure-factory.h (server side). */
struct yetty_ydraw_concrete_factory;
struct yetty_ydraw_figure;

#define YETTY_YPLOT_TYPE_ID 0x80000003u

// Uniforms struct (goes to GPU uniform buffer)
struct yetty_yplot_uniforms {
    float bounds_x;
    float bounds_y;
    float bounds_w;
    float bounds_h;
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
    uint32_t function_count;
    uint32_t colors[8];
};

/* One data buffer entry. The wire encoding for the `data` array repeats
 * [len][samples...] per entry, prefixed by a single [data_count]. */
struct yetty_yplot_data_buffer {
    const float *samples;
    size_t count; /* in f32 samples */
};

/* Buffers struct — packs everything that goes into the storage payload:
 *   - bytecode  : the yfsvm program (one buffer)
 *   - data[]    : variable-count list of sample buffers */
struct yetty_yplot_buffers {
    /* Single buffer: yfsvm bytecode (in u32 words). */
    const uint32_t *bytecode;
    size_t bytecode_len;

    /* Array of data buffers. data_count == 0 is valid (expression-only). */
    const struct yetty_yplot_data_buffer *data;
    size_t data_count;
};

//=============================================================================
// Serialization API — produces the wire envelope:
//   [type_id u32][payload_size u32]
//   [uniforms × 18 u32]
//   [bytecode_len u32][bytecode...]
//   [data_count u32][len_0 u32][samples_0...][len_1 u32][samples_1...]...
//
// The bytes after the uniforms become the GPU storage_buffer content
// verbatim — the shader walks the same header at render time, so no const
// offsets are baked into the pipeline.
//=============================================================================

size_t yetty_yplot_uniforms_serialized_size(const struct yetty_yplot_uniforms *uniforms,
                                            const struct yetty_yplot_buffers *buffers);

struct yetty_ycore_size_result yetty_yplot_uniforms_serialize(
    const struct yetty_yplot_uniforms *uniforms, const struct yetty_yplot_buffers *buffers,
    uint8_t *out, size_t out_capacity);

/* Number of u32 words the uniforms occupy in the wire (and as a prefix in
 * the storage payload). Exposed so wire readers can locate the storage
 * region without duplicating the layout knowledge. */
#define YETTY_YPLOT_UNIFORMS_WORDS 18u

//=============================================================================
// Factory API (creates binder with pre-compiled pipeline)
//=============================================================================

struct yetty_ydraw_concrete_factory *yetty_yplot_factory_create(void);
void yetty_yplot_factory_destroy(struct yetty_ydraw_concrete_factory *factory);

//=============================================================================
// Chunk-update API — push fresh samples into one of an instance's data
// buffers without rebuilding the wire or re-finalizing the binder.
// Resolves (buffer_index, sample_offset) to a byte offset inside the
// merged storage region and queues one wgpuQueueWriteBuffer.
//=============================================================================

struct yetty_ycore_void_result yetty_yplot_update_data_chunk(struct yetty_ydraw_figure *instance,
                                                             uint32_t buffer_index,
                                                             uint32_t sample_offset,
                                                             const float *data, size_t count);

//=============================================================================
// YAML parser registration
//=============================================================================

struct yetty_ydraw_yaml_parser;
struct yetty_ycore_void_result yetty_yplot_register_yaml_factory(
    struct yetty_ydraw_yaml_parser *parser);

#ifdef __cplusplus
}
#endif
