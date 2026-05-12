// Auto-generated from yimage.yaml - DO NOT EDIT
//
// Two-tier complex-prim model:
//   - factory owns ONE shared yetty_yrender_pipeline (compiled once at
//     compile_pipeline time from a template resource_set; the pipeline
//     carries the WGPUShaderModule + bind_group_layout + WGPURenderPipeline
//     + shared quad VB).
//   - each instance owns its OWN heap-allocated yetty_yrender_gpu_resource_set
//     (per-instance uniform values + storage buffer pointer) and its own
//     gpu_resource_binder (per-instance WGPUUniformBuffer + WGPUStorageBuffer
//     + WGPUBindGroup), referencing the factory's pipeline by const pointer.
//   - factory holds zoom state as plain floats; instances read it at render
//     time and write into their own RS uniforms (no shared mutable RS).

#include <yetty/yimage/yimage-gen.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/pipeline.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/ypaint-factory/complex-prim-factory.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char gyimage_shaderData[];
extern const unsigned int gyimage_shaderSize;
extern const unsigned char gyimage_lib_shaderData[];
extern const unsigned int gyimage_lib_shaderSize;

/* Static resource set for accessor library (yimage-gen.wgsl).
 * Read-only after init; safely shared across all instances as a child. */
static struct yetty_ypaint_core_gpu_resource_set yimage_lib_rs;
static bool yimage_lib_rs_initialized = false;

static void yimage_init_lib_rs(void)
{
    if (yimage_lib_rs_initialized) {
        return;
    }
    memset(&yimage_lib_rs, 0, sizeof(yimage_lib_rs));
    yetty_yrender_shader_code_set(&yimage_lib_rs.shader, (const char *)gyimage_lib_shaderData,
                                  gyimage_lib_shaderSize);
    yimage_lib_rs_initialized = true;
}

struct yetty_yimage_factory {
    struct yetty_ypaint_core_concrete_factory base;
    /* Shared, compiled once. NULL until compile_pipeline. */
    struct yetty_yrender_pipeline *pipeline;
    /* Template RS: shape definition for both the pipeline and per-instance
     * RSes. Children point to the shared static library RSes. */
    struct yetty_ypaint_core_gpu_resource_set template_rs;
    int template_initialized;

    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_ypaint_core_gpu_allocator *allocator;

    /* Zoom state — written by the canvas into the factory, read by each
     * instance render() and pushed into the instance's own RS uniforms. */
    float visual_zoom_scale;
    float visual_zoom_off_x;
    float visual_zoom_off_y;
    float cell_zoom_scale;
    float cell_zoom_off_x;
    float cell_zoom_off_y;
};

static struct yetty_yimage_factory *yetty_yimage_factory_from_base(
    struct yetty_ypaint_core_concrete_factory *base)
{
    return (struct yetty_yimage_factory *)base;
}

// Wire-format serialize helpers live in yimage-gen-wire.c (yetty_yimage_core).

//=============================================================================
// Resource Set Setup — populates a target RS with this prim's structure
// (uniform names/types, buffer descriptor, library children + own shader
// code). Same shape used for the factory's template_rs (pipeline-build) and
// for each per-instance RS (binder-build) — they're memcpy clones.
//=============================================================================

static void yimage_populate_rs(struct yetty_ypaint_core_gpu_resource_set *rs)
{
    yimage_init_lib_rs();

    memset(rs, 0, sizeof(*rs));
    strncpy(rs->namespace, "yimage", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&rs->shader, (const char *)gyimage_shaderData,
                                  gyimage_shaderSize);

    // Accessor library (generated uniforms accessors)
    rs->children[0] = (struct yetty_ypaint_core_gpu_resource_set *)&yimage_lib_rs;
    rs->children_count = 1;

    // Setup uniforms (values set later during render)
    strncpy(rs->uniforms[0].name, "bounds_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[0].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[0].u32 = 0;
    strncpy(rs->uniforms[1].name, "bounds_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[1].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[1].u32 = 0;
    strncpy(rs->uniforms[2].name, "bounds_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[2].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[2].u32 = 0;
    strncpy(rs->uniforms[3].name, "bounds_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[3].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[3].u32 = 0;
    strncpy(rs->uniforms[4].name, "image_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[4].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[4].u32 = 0;
    strncpy(rs->uniforms[5].name, "image_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[5].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[5].u32 = 0;
    strncpy(rs->uniforms[6].name, "visual_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[6].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[6].f32 = 1.0f;
    strncpy(rs->uniforms[7].name, "visual_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[7].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[7].f32 = 0.0f;
    strncpy(rs->uniforms[8].name, "visual_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[8].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[8].f32 = 0.0f;
    strncpy(rs->uniforms[9].name, "cell_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[9].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[9].f32 = 1.0f;
    strncpy(rs->uniforms[10].name, "cell_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[10].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[10].f32 = 0.0f;
    strncpy(rs->uniforms[11].name, "cell_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[11].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[11].f32 = 0.0f;
    strncpy(rs->uniforms[12].name, "viewport_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[12].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[12].f32 = 0.0f;
    strncpy(rs->uniforms[13].name, "viewport_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[13].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[13].f32 = 0.0f;
    strncpy(rs->uniforms[14].name, "image_region", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[14].type = YETTY_YRENDER_UNIFORM_VEC4;
    rs->uniforms[14].vec4[0] = 0.0f;
    rs->uniforms[14].vec4[1] = 0.0f;
    rs->uniforms[14].vec4[2] = 1.0f;
    rs->uniforms[14].vec4[3] = 1.0f;
    rs->uniform_count = 15;

    // No storage buffers (all buffers diverted to textures)
    rs->buffer_count = 0;

    /* Texture: image (format=rgba8, sampler=linear) */
    strncpy(rs->textures[0].name, "image", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->textures[0].wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->textures[0].format = WGPUTextureFormat_RGBA8Unorm;
    rs->textures[0].sampler_filter = 1;
    rs->textures[0].width = 1; /* placeholder for pipeline compile; overwritten per-instance */
    rs->textures[0].height = 1;
    rs->textures[0].data = NULL;
    rs->texture_count = 1;
}

//=============================================================================
// Instance Rendering — uses self->resource_set + self->binder; the factory
// supplies only the shared pipeline + zoom state.
//=============================================================================

static struct yetty_ycore_void_result yimage_instance_render(
    struct yetty_ypaint_core_complex_prim_instance *self, struct yetty_ypaint_core_target *target,
    float x, float y)
{
    if (!self || !self->buffer_data || !self->factory) {
        return YETTY_ERR(yetty_ycore_void, "invalid instance");
    }
    if (!self->resource_set || !self->binder) {
        return YETTY_ERR(yetty_ycore_void, "instance not finalised");
    }

    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self->factory);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "factory pipeline not initialized");
    }

    struct yetty_ypaint_core_gpu_resource_set *rs = self->resource_set;

    // Parse wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    const uint32_t *data = (const uint32_t *)self->buffer_data;
    const uint32_t *payload = data + 2; // skip type_id and payload_size

    // Update uniforms from wire format
    rs->uniforms[0].f32 = *(float *)&payload[0];
    rs->uniforms[1].f32 = *(float *)&payload[1];
    rs->uniforms[2].f32 = *(float *)&payload[2];
    rs->uniforms[3].f32 = *(float *)&payload[3];
    rs->uniforms[4].u32 = payload[4];
    rs->uniforms[5].u32 = payload[5];

    // Pull current zoom state from the factory into this instance's RS.
    rs->uniforms[6].f32 = factory->visual_zoom_scale > 0.0f ? factory->visual_zoom_scale : 1.0f;
    rs->uniforms[7].f32 = factory->visual_zoom_off_x;
    rs->uniforms[8].f32 = factory->visual_zoom_off_y;
    rs->uniforms[9].f32 = factory->cell_zoom_scale > 0.0f ? factory->cell_zoom_scale : 1.0f;
    rs->uniforms[10].f32 = factory->cell_zoom_off_x;
    rs->uniforms[11].f32 = factory->cell_zoom_off_y;

    // Visual-zoom viewport — read from the target every frame.
    rs->uniforms[12].f32 = target->viewport.w;
    rs->uniforms[13].f32 = target->viewport.h;

    // Override bounds_x / bounds_y with the caller-provided screen position
    // (wire bounds are the pre-scroll origin; x,y are the post-scroll pane
    // position the instance should render at).
    rs->uniforms[0].f32 = x;
    rs->uniforms[1].f32 = y;

    /* Texture 'image' — keep dimensions/data in sync with wire. */
    {
        const uint32_t *pixels_data = payload + 7;
        uint32_t tex_w = payload[4];
        uint32_t tex_h = payload[5];
        rs->textures[0].data = (uint8_t *)pixels_data;
        rs->textures[0].width = tex_w;
        rs->textures[0].height = tex_h;
        rs->textures[0].dirty = 1;
    }

    // Update the per-instance binder. Each instance has its own GPU
    // uniform_buffer / storage_buffer / bind_group, so concurrent renders
    // of multiple instances do NOT trample each other's data.
    struct yetty_ycore_void_result res = self->binder->ops->update(self->binder);
    if (YETTY_IS_ERR(res)) {
        return YETTY_ERR(yetty_ycore_void, "binder update failed", res);
    }

    // Get target view and create render pass
    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "failed to get target view");
    }

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(factory->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");
    }

    // Render pass with LoadOp=Load to preserve existing content
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = view;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }

    wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, target->viewport.w, target->viewport.h, 0.0f,
                                     1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, (uint32_t)target->viewport.w,
                                        (uint32_t)target->viewport.h);

    float w = self->bounds.max.x - self->bounds.min.x;
    float h = self->bounds.max.y - self->bounds.min.y;

    // Pipeline + quad VB are shared (factory). Bind group is per-instance.
    yetty_yrender_pipeline_bind(factory->pipeline, pass);
    self->binder->ops->bind(self->binder, pass, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0); // fullscreen triangle

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(factory->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    ydebug("yimage_instance_render: rendered at (%.1f, %.1f) size (%.1f x %.1f) inst=%p", x, y, w,
           h, (void *)self);
    return YETTY_OK_VOID();
}

//=============================================================================
// Factory Implementation
//=============================================================================

static struct yetty_ycore_void_result yimage_compile_pipeline(
    struct yetty_ypaint_core_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
    WGPUTextureFormat target_format, struct yetty_ypaint_core_gpu_allocator *allocator)
{
    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);

    if (factory->pipeline) {
        ydebug("yimage: factory pipeline already initialized");
        return YETTY_OK_VOID();
    }

    factory->device = device;
    factory->queue = queue;
    factory->allocator = allocator;
    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    yimage_populate_rs(&factory->template_rs);
    factory->template_initialized = 1;

    struct yetty_yrender_pipeline_ptr_result pr =
        yetty_yrender_pipeline_create(device, target_format, allocator, &factory->template_rs);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ycore_void, "yimage pipeline_create failed", pr);
    }
    factory->pipeline = pr.value;

    yinfo("yimage: pipeline compiled (shared across all instances)");
    return YETTY_OK_VOID();
}

static WGPURenderPipeline yimage_get_pipeline(struct yetty_ypaint_core_concrete_factory *self)
{
    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);
    return factory->pipeline ? yetty_yrender_pipeline_get_pipeline(factory->pipeline) : NULL;
}

static struct yetty_ypaint_core_complex_prim_instance_ptr_result yimage_create_instance(
    struct yetty_ypaint_core_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!buffer_data || size < sizeof(struct yetty_ypaint_core_complex_prim)) {
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "invalid buffer data");
    }

    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "yimage factory pipeline not compiled");
    }

    struct yetty_ypaint_core_complex_prim_instance *instance =
        calloc(1, sizeof(struct yetty_ypaint_core_complex_prim_instance));
    if (!instance) {
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "allocation failed");
    }

    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "buffer alloc failed");
    }
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = YETTY_YIMAGE_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = yimage_instance_render;

    struct rectangle_result aabb_res = yetty_ypaint_core_complex_prim_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res)) {
        instance->bounds = aabb_res.value;
    }

    /* Per-instance RS. Same shape as the factory template (so the binder
     * flattens to the same layout the pipeline was compiled against), but
     * with per-instance buffer/uniform values (set in render). */
    instance->resource_set = malloc(sizeof(struct yetty_ypaint_core_gpu_resource_set));
    if (!instance->resource_set) {
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "rs alloc failed");
    }
    memcpy(instance->resource_set, &factory->template_rs,
           sizeof(struct yetty_ypaint_core_gpu_resource_set));

    /* Wire the per-instance RS to this instance's payload. Storage
     * buffers (if any) point into the wire bytes; textures whose
     * pixels_buffer was diverted have their data + dimensions populated
     * here BEFORE binder->submit so the first finalize sees real
     * dimensions and atlas-packs accordingly. */
    {
        const uint32_t *data = (const uint32_t *)instance->buffer_data;
        const uint32_t *payload = data + 2;
        /* Texture 'image' — pixels diverted from buffer 'pixels'. */
        {
            const uint32_t *pixels_data = payload + 7;
            uint32_t tex_w = payload[4];
            uint32_t tex_h = payload[5];
            instance->resource_set->textures[0].data = (uint8_t *)pixels_data;
            instance->resource_set->textures[0].width = tex_w;
            instance->resource_set->textures[0].height = tex_h;
            instance->resource_set->textures[0].dirty = 1;
        }
    }

    /* Per-instance binder bound to the factory's shared pipeline. Owns
     * its OWN uniform_buffer / storage_buffer / bind_group. */
    struct yetty_yrender_gpu_resource_binder_result br =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(
            factory->device, factory->queue, factory->allocator, factory->pipeline);
    if (YETTY_IS_ERR(br)) {
        ydebug("yimage_create_instance: binder_create FAILED for %ux%u: %s",
               instance->resource_set->textures[0].width,
               instance->resource_set->textures[0].height, br.error.msg);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "instance binder create failed", br);
    }
    instance->binder = br.value;

    struct yetty_ycore_void_result sr =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(sr)) {
        ydebug("yimage_create_instance: submit FAILED for %ux%u: %s",
               instance->resource_set->textures[0].width,
               instance->resource_set->textures[0].height, sr.error.msg);
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "binder submit failed", sr);
    }

    struct yetty_ycore_void_result fr = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(fr)) {
        ydebug("yimage_create_instance: finalize FAILED for %ux%u: %s",
               instance->resource_set->textures[0].width,
               instance->resource_set->textures[0].height, fr.error.msg);
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "binder finalize failed", fr);
    }

    ydebug("yimage_create_instance: OK %ux%u bounds=(%.0f,%.0f,%.0f,%.0f)",
           instance->resource_set->textures[0].width,
           instance->resource_set->textures[0].height,
           instance->bounds.min.x, instance->bounds.min.y,
           instance->bounds.max.x, instance->bounds.max.y);
    return YETTY_OK(yetty_ypaint_core_complex_prim_instance_ptr, instance);
}

static void yimage_destroy_instance(struct yetty_ypaint_core_concrete_factory *self,
                                    struct yetty_ypaint_core_complex_prim_instance *instance)
{
    (void)self;
    if (!instance) {
        return;
    }
    if (instance->binder) {
        instance->binder->ops->destroy(instance->binder);
    }
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}

static struct yetty_ypaint_core_gpu_resource_set *yimage_get_shared_rs(
    struct yetty_ypaint_core_concrete_factory *self)
{
    /* Returns the structural template, NOT a mutable per-instance RS. */
    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);
    return factory->template_initialized ? &factory->template_rs : NULL;
}

static struct yetty_ycore_void_result yimage_set_visual_zoom(
    struct yetty_ypaint_core_concrete_factory *self, float scale, float off_x, float off_y)
{
    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);
    factory->visual_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->visual_zoom_off_x = off_x;
    factory->visual_zoom_off_y = off_y;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yimage_set_cell_zoom(
    struct yetty_ypaint_core_concrete_factory *self, float scale, float off_x, float off_y)
{
    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);
    factory->cell_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->cell_zoom_off_x = off_x;
    factory->cell_zoom_off_y = off_y;
    ydebug("yimage_set_cell_zoom: scale=%.3f off=(%.1f,%.1f)", scale, off_x, off_y);
    return YETTY_OK_VOID();
}

struct yetty_ypaint_core_concrete_factory *yetty_yimage_factory_create(void)
{
    struct yetty_yimage_factory *factory = calloc(1, sizeof(struct yetty_yimage_factory));
    if (!factory) {
        return NULL;
    }

    factory->base.type_id = YETTY_YIMAGE_TYPE_ID;
    factory->base.compile_pipeline = yimage_compile_pipeline;
    factory->base.get_pipeline = yimage_get_pipeline;
    factory->base.create_instance = yimage_create_instance;
    factory->base.destroy_instance = yimage_destroy_instance;
    factory->base.get_shared_rs = yimage_get_shared_rs;
    factory->base.set_visual_zoom = yimage_set_visual_zoom;
    factory->base.set_cell_zoom = yimage_set_cell_zoom;

    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    return &factory->base;
}

void yetty_yimage_factory_destroy(struct yetty_ypaint_core_concrete_factory *self)
{
    if (!self) {
        return;
    }

    struct yetty_yimage_factory *factory = yetty_yimage_factory_from_base(self);

    if (factory->pipeline) {
        yetty_yrender_pipeline_destroy(factory->pipeline);
    }
    free(factory);
}
