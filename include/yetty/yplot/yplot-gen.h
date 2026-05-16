// Auto-generated from yplot.yaml - DO NOT EDIT
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declared so this header stays GPU-less and can be included by
 * client-side wire emitters that don't link Dawn. The full type lives in
 * yetty/ydraw-factory/complex-prim-factory.h (server side). */
struct yetty_ydraw_concrete_factory;

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

// Buffers struct (goes to GPU storage buffer)
struct yetty_yplot_buffers {
    const uint32_t *bytecode;
    size_t bytecode_len;
};

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
