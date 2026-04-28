#ifndef YETTY_YRENDER_LAYER_RENDERER_H
#define YETTY_YRENDER_LAYER_RENDERER_H

#include <yetty/ycore/result.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/rendered-layer.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrender_layer_renderer;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yrender_layer_renderer, struct yetty_yrender_layer_renderer *);

/* Layer renderer vtable */
struct yetty_yrender_layer_renderer_ops {
    void (*destroy)(struct yetty_yrender_layer_renderer *self);

    struct yetty_yrender_rendered_layer_result (*render)(
        struct yetty_yrender_layer_renderer *self,
        const struct yetty_yrender_gpu_resource_set *resource_set);
};

/* Layer renderer base - embed as first member in implementations */
struct yetty_yrender_layer_renderer {
    const struct yetty_yrender_layer_renderer_ops *ops;
};

/* Surface specialization: renders to GPU intermediate texture via binder */
struct yetty_yrender_layer_renderer_result yetty_yrender_layer_renderer_surface_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_LAYER_RENDERER_H */
