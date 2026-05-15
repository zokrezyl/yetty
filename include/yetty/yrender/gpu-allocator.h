#ifndef YETTY_YRENDER_GPU_ALLOCATOR_H
#define YETTY_YRENDER_GPU_ALLOCATOR_H

#include <yetty/ycore/result.h>
#include <webgpu/webgpu.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_core_gpu_allocator;

struct yetty_yrender_gpu_allocator_ops {
    void (*destroy)(struct yetty_ydraw_core_gpu_allocator *self);
    WGPUBuffer (*create_buffer)(struct yetty_ydraw_core_gpu_allocator *self,
                                const WGPUBufferDescriptor *desc);
    void (*release_buffer)(struct yetty_ydraw_core_gpu_allocator *self, WGPUBuffer buffer);
    WGPUTexture (*create_texture)(struct yetty_ydraw_core_gpu_allocator *self,
                                  const WGPUTextureDescriptor *desc);
    void (*release_texture)(struct yetty_ydraw_core_gpu_allocator *self, WGPUTexture texture);
    uint64_t (*total_allocated_bytes)(const struct yetty_ydraw_core_gpu_allocator *self);
};

struct yetty_ydraw_core_gpu_allocator {
    const struct yetty_yrender_gpu_allocator_ops *ops;
};

YETTY_YRESULT_DECLARE(yetty_yrender_gpu_allocator, struct yetty_ydraw_core_gpu_allocator *);

struct yetty_yrender_gpu_allocator_result yetty_yrender_gpu_allocator_create(WGPUDevice device);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_GPU_ALLOCATOR_H */
