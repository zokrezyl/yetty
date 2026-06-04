// YDraw Complex Primitive - base interface for yplot, yimage, etc.
//
// Composites:
// - Embed this base struct as FIRST member
// - Implement ops interface
// - Have their own gpu_resource_set (collected as children of ydraw layer)

#pragma once

#include <stdint.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_figure;

struct yetty_ydraw_figure_ops {
    void (*destroy)(struct yetty_ydraw_figure *self);

    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        struct yetty_ydraw_figure *self);
};

struct yetty_ydraw_figure {
    const struct yetty_ydraw_figure_ops *ops;
    uint32_t type;
    uint32_t size;
};

#ifdef __cplusplus
}
#endif
