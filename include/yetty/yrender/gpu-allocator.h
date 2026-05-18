#ifndef YETTY_YRENDER_GPU_ALLOCATOR_H
#define YETTY_YRENDER_GPU_ALLOCATOR_H

#include <yetty/ycore/result.h>
#include <webgpu/webgpu.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_gpu_allocator;

/* Per-snapshot stats. live_* are the current state; peak_* track the
 * high-water mark since allocator creation. Useful for the yui info
 * dialog and any future telemetry / leak hunting. */
struct yetty_yrender_gpu_allocator_stats {
    uint64_t total_bytes;       /* sum of all live allocations */
    uint32_t buffer_count;      /* number of live buffers */
    uint32_t texture_count;     /* number of live textures */
    uint64_t buffer_bytes;      /* sum of live buffer sizes */
    uint64_t texture_bytes;     /* sum of live texture sizes */
    uint32_t live_allocations;  /* buffer_count + texture_count */
    uint32_t peak_allocations;  /* highest live_allocations ever observed */
    uint64_t peak_total_bytes;  /* highest total_bytes ever observed */
    uint32_t capacity;          /* MAX_ALLOCATIONS — pool size cap */
};

struct yetty_yrender_gpu_allocator_ops {
    void (*destroy)(struct yetty_ydraw_gpu_allocator *self);
    WGPUBuffer (*create_buffer)(struct yetty_ydraw_gpu_allocator *self,
                                const WGPUBufferDescriptor *desc);
    void (*release_buffer)(struct yetty_ydraw_gpu_allocator *self, WGPUBuffer buffer);
    WGPUTexture (*create_texture)(struct yetty_ydraw_gpu_allocator *self,
                                  const WGPUTextureDescriptor *desc);
    void (*release_texture)(struct yetty_ydraw_gpu_allocator *self, WGPUTexture texture);
    uint64_t (*total_allocated_bytes)(const struct yetty_ydraw_gpu_allocator *self);
    /* Snapshot of detailed stats. Fills *out; safe to call from any
     * thread that owns a reference to the allocator (the impl uses no
     * locks because the create/release ops are already single-thread
     * on the render thread). */
    void (*get_stats)(const struct yetty_ydraw_gpu_allocator *self,
                      struct yetty_yrender_gpu_allocator_stats *out);
};

struct yetty_ydraw_gpu_allocator {
    const struct yetty_yrender_gpu_allocator_ops *ops;
};

YETTY_YRESULT_DECLARE(yetty_yrender_gpu_allocator, struct yetty_ydraw_gpu_allocator *);

struct yetty_yrender_gpu_allocator_result yetty_yrender_gpu_allocator_create(WGPUDevice device);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_GPU_ALLOCATOR_H */
