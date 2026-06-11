/*
 * yetty_ymgui_pipeline — shared WebGPU pipeline + sampler for every
 * ymgui figure on a compositor.
 *
 * Private to the ymgui module: only pipeline.c (owns lifecycle) and
 * figure.c (reads handles to issue draws) include this. The compositor
 * builds one lazily on first ymgui traffic and hands the borrowed
 * pointer to every figure it creates.
 *
 * GPU layout:
 *   Bind group 0:
 *     binding 0 = uniform buffer (32 B: display_size, frame_top, pad)
 *     binding 1 = atlas texture view (R8)
 *     binding 2 = sampler
 *   Vertex (ImDrawVert, 20 B):
 *     loc 0 vec2 pos  (frame-local pixels)
 *     loc 1 vec2 uv
 *     loc 2 unorm8x4 col
 */
#ifndef YETTY_YMGUI_PIPELINE_H
#define YETTY_YMGUI_PIPELINE_H

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/yetty/yetty.h>

/* Shared pipeline lifecycle. The compositor (or any host that owns
 * multiple ymgui figures) builds one lazily on first ymgui OSC and
 * hands the borrowed pointer to every figure it creates. The pipeline
 * must outlive every figure that holds the pointer. The internal layout
 * (below) is private to the ymgui module — callers treat
 * `struct yetty_ymgui_pipeline *` as opaque.
 *
 * These declarations live in this internal header rather than the
 * generated public figure.h so that figure.c can consume them without
 * transitively pulling in its own generated header. */
struct yetty_ymgui_pipeline;
YETTY_YRESULT_DECLARE(yetty_ymgui_pipeline_ptr, struct yetty_ymgui_pipeline *);

struct yetty_ymgui_pipeline_ptr_result yetty_ymgui_pipeline_create(
    const struct yetty_context *context);

struct yetty_ycore_void_result yetty_ymgui_pipeline_destroy(struct yetty_ymgui_pipeline *pipeline);

struct yetty_ymgui_pipeline {
    /* Borrowed from yetty_context. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;

    /* Owned; released by yetty_ymgui_pipeline_destroy. */
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;
};

/* Public API (create/destroy + YRESULT_DECLARE) lives in
 * <yetty/ymgui/figure.h> — the compositor consumes it through that
 * single public header. This file only adds the struct layout for the
 * two translation units in this directory. */

#endif /* YETTY_YMGUI_PIPELINE_H */
