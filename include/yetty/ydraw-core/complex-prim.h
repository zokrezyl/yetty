// YDraw Complex Primitive - base interface for yplot, yimage, etc.
//
// Complex primitives:
// - Embed this base struct as FIRST member
// - Implement ops interface
// - Have their own gpu_resource_set (collected as children of ydraw layer)

#pragma once

#include <stdint.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_core_complex_prim;

struct yetty_ydraw_core_complex_prim_ops {
    void (*destroy)(struct yetty_ydraw_core_complex_prim *self);

    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        struct yetty_ydraw_core_complex_prim *self);
};

struct yetty_ydraw_core_complex_prim {
    const struct yetty_ydraw_core_complex_prim_ops *ops;
    uint32_t type;
    uint32_t size;
};

#ifdef __cplusplus
}
#endif
