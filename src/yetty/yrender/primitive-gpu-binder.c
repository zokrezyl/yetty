// Primitive GPU Binder - uses pre-compiled pipelines from factories
//
// This binder NEVER compiles shaders. Pipelines come from concrete factories.

#include <yetty/yrender/primitive-gpu-binder.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESOURCE_SETS 16
#define MAX_FLAT_BUFFERS 64
#define MAX_FLAT_TEXTURES 16
#define MAX_FLAT_UNIFORMS 32
#define MEGA_BUFFER_INITIAL_SIZE (1024 * 1024) // 1MB

//=============================================================================
// Internal structures
//=============================================================================

struct yetty_yrender_flat_buffer {
    const char *ns;
    struct yetty_yrender_buffer *src;
    size_t mega_offset;
};

struct yetty_yrender_flat_texture {
    const char *ns;
    struct yetty_yrender_texture *src;
    uint32_t atlas_index;
};

struct yetty_yrender_flat_uniform {
    const char *ns;
    struct yetty_yrender_uniform *src;
};

struct yetty_yrender_gpu_binder {
    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_ypaint_core_gpu_allocator *allocator;

    // Pre-compiled pipeline (from factory)
    WGPURenderPipeline pipeline;

    // Collected resource sets
    const struct yetty_ypaint_core_gpu_resource_set *resource_sets[MAX_RESOURCE_SETS];
    size_t resource_set_count;

    // Flattened resources
    struct yetty_yrender_flat_buffer flat_buffers[MAX_FLAT_BUFFERS];
    size_t flat_buffer_count;

    struct yetty_yrender_flat_texture flat_textures[MAX_FLAT_TEXTURES];
    size_t flat_texture_count;

    struct yetty_yrender_flat_uniform flat_uniforms[MAX_FLAT_UNIFORMS];
    size_t flat_uniform_count;

    // GPU resources
    WGPUBuffer mega_buffer;
    size_t mega_buffer_size;
    size_t mega_buffer_capacity;

    WGPUBuffer uniform_buffer;
    size_t uniform_buffer_size;

    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;
    WGPUSampler atlas_sampler;
    uint32_t atlas_width;
    uint32_t atlas_height;

    WGPUBindGroupLayout bind_group_layout;
    WGPUBindGroup bind_group;

    // Quad vertex buffer for instanced rendering
    WGPUBuffer quad_vertex_buffer;

    int finalized;
};

//=============================================================================
// Lifecycle
//=============================================================================

struct yetty_primitive_gpu_binder_ptr_result yetty_yrender_gpu_binder_create(
    WGPUDevice device, WGPUQueue queue, struct yetty_ypaint_core_gpu_allocator *allocator)
{
    if (!device || !queue) {
        return YETTY_ERR(yetty_primitive_gpu_binder_ptr, "device or queue is NULL");
    }

    struct yetty_yrender_gpu_binder *binder =
        calloc(1, sizeof(struct yetty_yrender_gpu_binder));
    if (!binder) {
        return YETTY_ERR(yetty_primitive_gpu_binder_ptr, "allocation failed");
    }

    binder->device = device;
    binder->queue = queue;
    binder->allocator = allocator;

    // Create quad vertex buffer for instanced rendering
    float quad_vertices[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
    };

    WGPUBufferDescriptor vb_desc = {
        .label = {.data = "quad_vertices", .length = 13},
        .size = sizeof(quad_vertices),
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
    };
    binder->quad_vertex_buffer = wgpuDeviceCreateBuffer(device, &vb_desc);
    if (binder->quad_vertex_buffer) {
        wgpuQueueWriteBuffer(queue, binder->quad_vertex_buffer, 0, quad_vertices,
                             sizeof(quad_vertices));
    }

    ydebug("primitive_gpu_binder: created");
    return YETTY_OK(yetty_primitive_gpu_binder_ptr, binder);
}

void yetty_yrender_gpu_binder_destroy(struct yetty_yrender_gpu_binder *binder)
{
    if (!binder) {
        return;
    }

    if (binder->bind_group) {
        wgpuBindGroupRelease(binder->bind_group);
    }
    if (binder->bind_group_layout) {
        wgpuBindGroupLayoutRelease(binder->bind_group_layout);
    }
    if (binder->mega_buffer) {
        wgpuBufferRelease(binder->mega_buffer);
    }
    if (binder->uniform_buffer) {
        wgpuBufferRelease(binder->uniform_buffer);
    }
    if (binder->atlas_sampler) {
        wgpuSamplerRelease(binder->atlas_sampler);
    }
    if (binder->atlas_view) {
        wgpuTextureViewRelease(binder->atlas_view);
    }
    if (binder->atlas_texture) {
        wgpuTextureRelease(binder->atlas_texture);
    }
    if (binder->quad_vertex_buffer) {
        wgpuBufferRelease(binder->quad_vertex_buffer);
    }
    // Note: pipeline is NOT released - it's owned by the factory

    free(binder);
    ydebug("primitive_gpu_binder: destroyed");
}

//=============================================================================
// Pipeline management
//=============================================================================

struct yetty_ycore_void_result yetty_yrender_gpu_binder_set_pipeline(
    struct yetty_yrender_gpu_binder *binder, WGPURenderPipeline pipeline)
{
    if (!binder) {
        return YETTY_ERR(yetty_ycore_void, "binder is NULL");
    }
    if (!pipeline) {
        return YETTY_ERR(yetty_ycore_void, "pipeline is NULL");
    }

    binder->pipeline = pipeline;
    binder->finalized = 0; // Need to re-finalize with new pipeline

    ydebug("primitive_gpu_binder: pipeline set");
    return YETTY_OK_VOID();
}

//=============================================================================
// Resource collection
//=============================================================================

static void collect_resources_recursive(struct yetty_yrender_gpu_binder *binder,
                                        const struct yetty_ypaint_core_gpu_resource_set *rs)
{
    if (!rs) {
        return;
    }

    // Collect children first
    for (size_t i = 0; i < rs->children_count; i++) {
        if (rs->children[i]) {
            collect_resources_recursive(binder, rs->children[i]);
        }
    }

    // Collect buffers
    for (size_t i = 0; i < rs->buffer_count && binder->flat_buffer_count < MAX_FLAT_BUFFERS; i++) {
        binder->flat_buffers[binder->flat_buffer_count++] = (struct yetty_yrender_flat_buffer){
            .ns = rs->namespace,
            .src = (struct yetty_yrender_buffer *)&rs->buffers[i],
        };
    }

    // Collect textures
    for (size_t i = 0; i < rs->texture_count && binder->flat_texture_count < MAX_FLAT_TEXTURES;
         i++) {
        binder->flat_textures[binder->flat_texture_count++] = (struct yetty_yrender_flat_texture){
            .ns = rs->namespace,
            .src = (struct yetty_yrender_texture *)&rs->textures[i],
        };
    }

    // Collect uniforms
    for (size_t i = 0; i < rs->uniform_count && binder->flat_uniform_count < MAX_FLAT_UNIFORMS;
         i++) {
        binder->flat_uniforms[binder->flat_uniform_count++] = (struct yetty_yrender_flat_uniform){
            .ns = rs->namespace,
            .src = (struct yetty_yrender_uniform *)&rs->uniforms[i],
        };
    }
}

struct yetty_ycore_void_result yetty_yrender_gpu_binder_add_resource_set(
    struct yetty_yrender_gpu_binder *binder, const struct yetty_ypaint_core_gpu_resource_set *rs)
{
    if (!binder) {
        return YETTY_ERR(yetty_ycore_void, "binder is NULL");
    }
    if (!rs) {
        return YETTY_ERR(yetty_ycore_void, "rs is NULL");
    }
    if (binder->resource_set_count >= MAX_RESOURCE_SETS) {
        return YETTY_ERR(yetty_ycore_void, "max resource sets reached");
    }

    binder->resource_sets[binder->resource_set_count++] = rs;
    return YETTY_OK_VOID();
}

//=============================================================================
// Finalize - create bind group (NO shader compilation)
//=============================================================================

struct yetty_ycore_void_result yetty_yrender_gpu_binder_finalize(
    struct yetty_yrender_gpu_binder *binder)
{
    if (!binder) {
        return YETTY_ERR(yetty_ycore_void, "binder is NULL");
    }
    if (!binder->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "pipeline not set");
    }

    if (binder->finalized) {
        ydebug("primitive_gpu_binder: already finalized");
        return YETTY_OK_VOID();
    }

    // Reset flattened resources
    binder->flat_buffer_count = 0;
    binder->flat_texture_count = 0;
    binder->flat_uniform_count = 0;

    // Collect all resources
    for (size_t i = 0; i < binder->resource_set_count; i++) {
        collect_resources_recursive(binder, binder->resource_sets[i]);
    }

    // Calculate mega buffer size
    size_t total_buffer_size = 0;
    for (size_t i = 0; i < binder->flat_buffer_count; i++) {
        binder->flat_buffers[i].mega_offset = total_buffer_size;
        total_buffer_size += binder->flat_buffers[i].src->size;
        // Align to 4 bytes
        total_buffer_size = (total_buffer_size + 3) & ~3;
    }

    // Create/resize mega buffer if needed
    if (total_buffer_size > binder->mega_buffer_capacity) {
        if (binder->mega_buffer) {
            wgpuBufferRelease(binder->mega_buffer);
        }

        size_t new_cap = total_buffer_size > MEGA_BUFFER_INITIAL_SIZE ? total_buffer_size
                                                                      : MEGA_BUFFER_INITIAL_SIZE;

        WGPUBufferDescriptor desc = {
            .label = {.data = "prim_mega_buffer", .length = 16},
            .size = new_cap,
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        };
        binder->mega_buffer = wgpuDeviceCreateBuffer(binder->device, &desc);
        binder->mega_buffer_capacity = new_cap;
        ydebug("primitive_gpu_binder: created mega buffer %zu bytes", new_cap);
    }
    binder->mega_buffer_size = total_buffer_size;

    // Calculate uniform buffer size
    size_t uniform_size = 0;
    for (size_t i = 0; i < binder->flat_uniform_count; i++) {
        uniform_size += 16; // Each uniform padded to 16 bytes
    }

    // Create/resize uniform buffer if needed
    if (uniform_size > 0 && !binder->uniform_buffer) {
        WGPUBufferDescriptor desc = {
            .label = {.data = "prim_uniforms", .length = 13},
            .size = uniform_size > 256 ? uniform_size : 256,
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        };
        binder->uniform_buffer = wgpuDeviceCreateBuffer(binder->device, &desc);
        binder->uniform_buffer_size = desc.size;
    }

    // Get bind group layout from pipeline
    if (binder->bind_group_layout) {
        wgpuBindGroupLayoutRelease(binder->bind_group_layout);
    }
    binder->bind_group_layout = wgpuRenderPipelineGetBindGroupLayout(binder->pipeline, 0);

    // Create bind group
    if (binder->bind_group) {
        wgpuBindGroupRelease(binder->bind_group);
    }

    WGPUBindGroupEntry entries[3] = {0};
    size_t entry_count = 0;

    // Storage buffer
    if (binder->mega_buffer) {
        entries[entry_count++] = (WGPUBindGroupEntry){
            .binding = 0,
            .buffer = binder->mega_buffer,
            .size = binder->mega_buffer_capacity,
        };
    }

    // Uniform buffer
    if (binder->uniform_buffer) {
        entries[entry_count++] = (WGPUBindGroupEntry){
            .binding = 1,
            .buffer = binder->uniform_buffer,
            .size = binder->uniform_buffer_size,
        };
    }

    WGPUBindGroupDescriptor bg_desc = {
        .label = {.data = "prim_bind_group", .length = 15},
        .layout = binder->bind_group_layout,
        .entryCount = entry_count,
        .entries = entries,
    };
    binder->bind_group = wgpuDeviceCreateBindGroup(binder->device, &bg_desc);

    binder->finalized = 1;
    ydebug("primitive_gpu_binder: finalized with %zu buffers, %zu uniforms",
           binder->flat_buffer_count, binder->flat_uniform_count);

    return YETTY_OK_VOID();
}

//=============================================================================
// Update - upload dirty data (NO recompilation ever)
//=============================================================================

struct yetty_ycore_void_result yetty_yrender_gpu_binder_update(
    struct yetty_yrender_gpu_binder *binder)
{
    if (!binder) {
        return YETTY_ERR(yetty_ycore_void, "binder is NULL");
    }
    if (!binder->finalized) {
        return YETTY_ERR(yetty_ycore_void, "not finalized");
    }

    // Upload dirty buffers
    for (size_t i = 0; i < binder->flat_buffer_count; i++) {
        struct yetty_yrender_flat_buffer *fb = &binder->flat_buffers[i];
        if (!fb->src->dirty) {
            continue;
        }

        if (fb->src->data && fb->src->size > 0 && binder->mega_buffer) {
            wgpuQueueWriteBuffer(binder->queue, binder->mega_buffer, fb->mega_offset, fb->src->data,
                                 fb->src->size);
        }
        fb->src->dirty = 0;
    }

    // Upload uniforms
    if (binder->uniform_buffer && binder->flat_uniform_count > 0) {
        uint8_t uniform_data[1024] = {0};
        size_t offset = 0;

        for (size_t i = 0; i < binder->flat_uniform_count && offset < sizeof(uniform_data); i++) {
            struct yetty_yrender_uniform *u = binder->flat_uniforms[i].src;
            memcpy(uniform_data + offset, &u->u32, 16);
            offset += 16;
        }

        if (offset > 0) {
            wgpuQueueWriteBuffer(binder->queue, binder->uniform_buffer, 0, uniform_data, offset);
        }
    }

    return YETTY_OK_VOID();
}

//=============================================================================
// Render
//=============================================================================

struct yetty_ycore_void_result yetty_yrender_gpu_binder_render(
    struct yetty_yrender_gpu_binder *binder, WGPURenderPassEncoder pass, uint32_t instance_count)
{
    if (!binder) {
        return YETTY_ERR(yetty_ycore_void, "binder is NULL");
    }
    if (!binder->finalized) {
        return YETTY_ERR(yetty_ycore_void, "not finalized");
    }
    if (!pass) {
        return YETTY_ERR(yetty_ycore_void, "pass is NULL");
    }

    wgpuRenderPassEncoderSetPipeline(pass, binder->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, binder->bind_group, 0, NULL);

    if (binder->quad_vertex_buffer) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, binder->quad_vertex_buffer, 0,
                                             6 * 2 * sizeof(float));
    }

    wgpuRenderPassEncoderDraw(pass, 6, instance_count, 0, 0);

    return YETTY_OK_VOID();
}

//=============================================================================
// Reset
//=============================================================================

void yetty_yrender_gpu_binder_reset(struct yetty_yrender_gpu_binder *binder)
{
    if (!binder) {
        return;
    }

    binder->resource_set_count = 0;
    // Keep finalized state and GPU resources - just clear resource set list
}
