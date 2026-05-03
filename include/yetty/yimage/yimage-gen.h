// Auto-generated from yimage.yaml - DO NOT EDIT
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/complex-prim-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YIMAGE_TYPE_ID 0x80000004u

// Uniforms struct (goes to GPU uniform buffer)
struct yetty_yimage_uniforms {
    float bounds_x;
    float bounds_y;
    float bounds_w;
    float bounds_h;
    uint32_t image_w;
    uint32_t image_h;
};

// Buffers struct (goes to GPU storage buffer)
struct yetty_yimage_buffers {
    const uint32_t *pixels;
    size_t pixels_len;
};

//=============================================================================
// Serialization API
//=============================================================================

size_t yetty_yimage_uniforms_serialized_size(
    const struct yetty_yimage_uniforms *uniforms,
    const struct yetty_yimage_buffers *buffers);

struct yetty_ycore_size_result yetty_yimage_uniforms_serialize(
    const struct yetty_yimage_uniforms *uniforms,
    const struct yetty_yimage_buffers *buffers,
    uint8_t *out, size_t out_capacity);

//=============================================================================
// Factory API (creates binder with pre-compiled pipeline)
//=============================================================================

struct yetty_ypaint_core_concrete_factory *yetty_yimage_factory_create(void);
void yetty_yimage_factory_destroy(struct yetty_ypaint_core_concrete_factory *factory);

#ifdef __cplusplus
}
#endif

