#ifndef YETTY_YRENDER_GPU_RESOURCE_BINDER_H
#define YETTY_YRENDER_GPU_RESOURCE_BINDER_H

#include <yetty/ycore/result.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrender_gpu_resource_binder;
struct yetty_yrender_gpu_allocator;

struct yetty_yrender_gpu_resource_binder_ops {
    void (*destroy)(struct yetty_yrender_gpu_resource_binder *self);
    struct yetty_ycore_void_result (*submit)(struct yetty_yrender_gpu_resource_binder *self,
                                             const struct yetty_yrender_gpu_resource_set *rs);
    struct yetty_ycore_void_result (*finalize)(struct yetty_yrender_gpu_resource_binder *self);
    struct yetty_ycore_void_result (*update)(struct yetty_yrender_gpu_resource_binder *self);
    struct yetty_ycore_void_result (*bind)(struct yetty_yrender_gpu_resource_binder *self,
                                           WGPURenderPassEncoder pass, uint32_t group_index);
    WGPURenderPipeline (*get_pipeline)(const struct yetty_yrender_gpu_resource_binder *self);
    WGPUBuffer (*get_quad_vertex_buffer)(const struct yetty_yrender_gpu_resource_binder *self);
};

struct yetty_yrender_gpu_resource_binder {
    const struct yetty_yrender_gpu_resource_binder_ops *ops;
};

YETTY_YRESULT_DECLARE(yetty_yrender_gpu_resource_binder,
                      struct yetty_yrender_gpu_resource_binder *);

struct yetty_yrender_gpu_resource_binder_result yetty_yrender_gpu_resource_binder_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_yrender_gpu_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_GPU_RESOURCE_BINDER_H */
