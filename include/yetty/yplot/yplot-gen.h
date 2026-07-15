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
 * yetty/ydraw-factory/composite-factory.h (server side). */
struct yetty_ydraw_concrete_factory;
struct yetty_ydraw_composite;

#define YETTY_YPLOT_TYPE_ID 0x80000003u

/* Number of u32 words the uniforms occupy in the wire (and as a prefix in
 * the payload before the storage region). */
#define YETTY_YPLOT_UNIFORMS_WORDS 40u

/* RS slot of the server-side `time` uniform (not on the wire). */
#define YETTY_YPLOT_UNIFORM_TIME_SLOT 48u

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
    float x_step;
    float y_step;
    uint32_t colormap_id;
    float field_min;
    float field_max;
    uint32_t flags;
    uint32_t function_count;
    uint32_t colors[8];
    uint32_t band_slots[8];
    uint32_t hidden_mask;
    uint32_t ring_heads[8];
};

/* One `data` entry. The wire encoding repeats [len][samples...]
 * per entry, prefixed by a single [data_count]. */
struct yetty_yplot_data_buffer {
    const float *samples;
    size_t count; /* in float elements */
};
// Buffers struct (goes to GPU storage buffer)
struct yetty_yplot_buffers {
    const uint32_t *bytecode;
    size_t bytecode_len;
    const struct yetty_yplot_data_buffer *data;
    size_t data_count;
};

/* CMD_UPDATE entry point — implemented by hand in a companion TU (the
 * decode of target_field/body is yplot-specific). Installed as the
 * per-instance ops->update at create time. */
struct yetty_ycore_void_result yetty_yplot_instance_update(struct yetty_ydraw_composite *instance,
                                                           uint32_t target_field, const void *body,
                                                           size_t body_size);

/* Lifecycle callouts — implemented by hand in a companion TU. `created`
 * runs after a successful create (failure is non-fatal: the figure still
 * renders); `destroying` runs FIRST in instance destroy, before any
 * teardown. */
struct yetty_ycore_void_result yetty_yplot_instance_created(struct yetty_ydraw_composite *instance);
void yetty_yplot_instance_destroying(struct yetty_ydraw_composite *instance);

//=============================================================================
// Serialization API
//=============================================================================

size_t yetty_yplot_uniforms_serialized_size(const struct yetty_yplot_uniforms *uniforms,
                                            const struct yetty_yplot_buffers *buffers);

struct yetty_ycore_size_result yetty_yplot_uniforms_serialize(
    const struct yetty_yplot_uniforms *uniforms, const struct yetty_yplot_buffers *buffers,
    uint8_t *out, size_t out_capacity);

//=============================================================================
// Factory API (creates binder with pre-compiled pipeline)
//=============================================================================

struct yetty_ydraw_concrete_factory *yetty_yplot_factory_create(void);
void yetty_yplot_factory_destroy(struct yetty_ydraw_concrete_factory *factory);

//=============================================================================
// YAML parser registration
//=============================================================================

struct yetty_ydraw_yaml_parser;
struct yetty_ycore_void_result yetty_yplot_register_yaml_factory(
    struct yetty_ydraw_yaml_parser *parser);

#ifdef __cplusplus
}
#endif
