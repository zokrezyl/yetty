// Primitive GPU Binder - uses pre-compiled pipelines from factories
//
// Unlike gpu-resource-binder which compiles shaders, this binder:
// - Accepts pre-compiled pipelines from concrete factories
// - Only handles bind group creation and data upload
// - NEVER compiles shaders or creates pipelines
//
// Pipeline lifecycle:
//   Factory init → compile pipeline once → store in shared_rs
//   Render → primitive_gpu_binder uses pre-compiled pipeline → FAST

#pragma once

#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrender_gpu_allocator;

//=============================================================================
// Primitive GPU Binder
//=============================================================================

struct yetty_primitive_gpu_binder;

YETTY_YRESULT_DECLARE(yetty_primitive_gpu_binder_ptr, struct yetty_primitive_gpu_binder *);

// Create binder
struct yetty_primitive_gpu_binder_ptr_result yetty_primitive_gpu_binder_create(
    WGPUDevice device, WGPUQueue queue, struct yetty_yrender_gpu_allocator *allocator);

// Destroy binder
void yetty_primitive_gpu_binder_destroy(struct yetty_primitive_gpu_binder *binder);

// Set pre-compiled pipeline (from concrete factory)
// This pipeline will be used for all subsequent renders
struct yetty_ycore_void_result yetty_primitive_gpu_binder_set_pipeline(
    struct yetty_primitive_gpu_binder *binder, WGPURenderPipeline pipeline);

// Add resource set (buffers, textures, uniforms)
// Does NOT compile anything - just collects resources for bind group
struct yetty_ycore_void_result yetty_primitive_gpu_binder_add_resource_set(
    struct yetty_primitive_gpu_binder *binder, const struct yetty_yrender_gpu_resource_set *rs);

// Finalize - create bind group from collected resources
// Uses pre-set pipeline's bind group layout
struct yetty_ycore_void_result yetty_primitive_gpu_binder_finalize(
    struct yetty_primitive_gpu_binder *binder);

// Update - upload dirty buffers/textures (no recompilation ever)
struct yetty_ycore_void_result yetty_primitive_gpu_binder_update(
    struct yetty_primitive_gpu_binder *binder);

// Bind to render pass and draw
struct yetty_ycore_void_result yetty_primitive_gpu_binder_render(
    struct yetty_primitive_gpu_binder *binder, WGPURenderPassEncoder pass, uint32_t instance_count);

// Reset for next frame (clears resource sets, keeps pipeline)
void yetty_primitive_gpu_binder_reset(struct yetty_primitive_gpu_binder *binder);

#ifdef __cplusplus
}
#endif
