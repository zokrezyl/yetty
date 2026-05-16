#ifndef YETTY_YRENDER_PIPELINE_H
#define YETTY_YRENDER_PIPELINE_H

/*
 * yetty_yrender_pipeline — shared, type-level GPU pipeline.
 *
 * Built ONCE per shader-tree shape (typically per concrete-factory type:
 * yplot, yimage, yvideo, ...) from a "template" resource_set. Owns:
 *
 *   shader_module       compiled WGSL — expensive (tens of ms compile)
 *   bind_group_layout   describes the binding-slot SHAPE
 *   pipeline_layout     wraps bind_group_layout
 *   pipeline            the compiled render pipeline
 *   quad_vertex_buffer  shared full-screen quad
 *
 * Multiple `gpu_resource_binder` instances can share one pipeline; each
 * binder owns only the per-instance side (bind_group + GPU uniform_buffer
 * + storage_buffer) and references the pipeline by const pointer.
 *
 * The TEMPLATE rs supplies:
 *   - shader code (concatenated from children + parent)
 *   - bind-group LAYOUT shape (uniform/buffer/texture COUNT and TYPES;
 *     concrete data values are NOT used here)
 *
 * Per-instance VALUES (uniform numbers, buffer bytes) are supplied by the
 * binder later — they don't affect the pipeline.
 */

#include <yetty/ycore/result.h>
#include <yetty/yrender/types.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <webgpu/webgpu.h>

struct yetty_ydraw_gpu_allocator;
struct yetty_yrender_pipeline;

YETTY_YRESULT_DECLARE(yetty_yrender_pipeline_ptr, struct yetty_yrender_pipeline *);

/* Compile shader + create pipeline from `template_rs`. Returns owned pipeline.
 * `template_rs` only needs correct STRUCTURE (uniform/buffer/texture counts,
 * shader code, children); buffer/texture data values are ignored. */
struct yetty_yrender_pipeline_ptr_result yetty_yrender_pipeline_create(
    WGPUDevice device, WGPUTextureFormat target_format,
    struct yetty_ydraw_gpu_allocator *allocator,
    const struct yetty_ydraw_gpu_resource_set *template_rs);

void yetty_yrender_pipeline_destroy(struct yetty_yrender_pipeline *p);

WGPURenderPipeline yetty_yrender_pipeline_get_pipeline(const struct yetty_yrender_pipeline *p);
WGPUBindGroupLayout yetty_yrender_pipeline_get_bind_group_layout(
    const struct yetty_yrender_pipeline *p);
WGPUBuffer yetty_yrender_pipeline_get_quad_vertex_buffer(const struct yetty_yrender_pipeline *p);

/* Convenience: setPipeline + setVertexBuffer in one call. */
void yetty_yrender_pipeline_bind(const struct yetty_yrender_pipeline *p,
                                 WGPURenderPassEncoder pass);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_PIPELINE_H */
