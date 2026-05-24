#ifndef YETTY_YRENDER_GPU_RESOURCE_BINDER_H
#define YETTY_YRENDER_GPU_RESOURCE_BINDER_H

#include <yetty/ycore/result.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/pipeline.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrender_gpu_resource_binder;
struct yetty_ydraw_gpu_allocator;

struct yetty_yrender_gpu_resource_binder_ops {
    void (*destroy)(struct yetty_yrender_gpu_resource_binder *self);
    struct yetty_ycore_void_result (*submit)(struct yetty_yrender_gpu_resource_binder *self,
                                             const struct yetty_ydraw_gpu_resource_set *rs);
    struct yetty_ycore_void_result (*finalize)(struct yetty_yrender_gpu_resource_binder *self);
    struct yetty_ycore_void_result (*update)(struct yetty_yrender_gpu_resource_binder *self);
    struct yetty_ycore_void_result (*bind)(struct yetty_yrender_gpu_resource_binder *self,
                                           WGPURenderPassEncoder pass, uint32_t group_index);
    WGPURenderPipeline (*get_pipeline)(const struct yetty_yrender_gpu_resource_binder *self);
    WGPUBuffer (*get_quad_vertex_buffer)(const struct yetty_yrender_gpu_resource_binder *self);

    /* Sub-region write into one of the flattened storage buffers. The
     * binder maps (buffer_index, byte_offset) to the right slot inside the
     * single GPU storage buffer and queues one wgpuQueueWriteBuffer — no
     * rebind, no re-finalize, no whole-buffer re-upload.
     *
     * Intended for streaming updates of large per-instance data (live audio
     * samples, scrolling time series, …) where the caller already knows
     * exactly which bytes changed.
     *
     * buffer_index   index into the same flattened buffer order the shader
     *                sees (matches the order rs->buffers[] declared them,
     *                children-first, then parent — i.e. exactly the order
     *                the binder collected them at finalize time).
     * byte_offset    offset INTO that buffer (not the merged region).
     * data, size     bytes to write.
     */
    struct yetty_ycore_void_result (*write_buffer_chunk)(
        struct yetty_yrender_gpu_resource_binder *self, size_t buffer_index, size_t byte_offset,
        const void *data, size_t size);
};

struct yetty_yrender_gpu_resource_binder {
    const struct yetty_yrender_gpu_resource_binder_ops *ops;
};

YETTY_YRESULT_DECLARE(yetty_yrender_gpu_resource_binder,
                      struct yetty_yrender_gpu_resource_binder *);

/* Legacy: binder owns its pipeline (compiles its own shader at finalize).
 * Used by render-target-texture / layer code where pipeline-per-binder is
 * fine (no instance-sharing). */
struct yetty_yrender_gpu_resource_binder_result yetty_yrender_gpu_resource_binder_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_ydraw_gpu_allocator *allocator);

/* Two-tier: binder uses an externally-owned pipeline. The binder owns only
 * the per-instance side (uniform_buffer, storage_buffer, bind_group). The
 * pipeline (shader_module + pipeline + layouts + quad_vb) is shared across
 * many binders. Used by complex prims (yplot, yimage, ...) so all instances
 * of one type share the compiled shader.
 *
 * The supplied pipeline must outlive the binder. */
struct yetty_yrender_gpu_resource_binder_result
yetty_yrender_gpu_resource_binder_create_with_pipeline(
    WGPUDevice device, WGPUQueue queue, struct yetty_ydraw_gpu_allocator *allocator,
    const struct yetty_yrender_pipeline *pipeline);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_GPU_RESOURCE_BINDER_H */
