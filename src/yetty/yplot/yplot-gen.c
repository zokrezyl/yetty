// Auto-generated from yplot.yaml - DO NOT EDIT
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

#include <yetty/yplot/yplot-gen.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/pipeline.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yfsvm/shader-rs.h>

/* yplot-time.c — animates the `time` uniform when the compiled
 * bytecode references LOAD_T. Forward-declared here instead of a
 * header since the only call sites are below in this same TU. */
struct yetty_ydraw_figure;
struct yetty_ycore_void_result yetty_yplot_time_attach(
    struct yetty_ydraw_figure *instance);
void yetty_yplot_time_detach(struct yetty_ydraw_figure *instance);

extern const unsigned char gyplot_shaderData[];
extern const unsigned int gyplot_shaderSize;
extern const unsigned char gyplot_lib_shaderData[];
extern const unsigned int gyplot_lib_shaderSize;

/* Static resource set for accessor library (yplot-gen.wgsl).
 * Read-only after init; safely shared across all instances as a child. */
static struct yetty_ydraw_gpu_resource_set yplot_lib_rs;
static bool yplot_lib_rs_initialized = false;

static void yplot_init_lib_rs(void)
{
    if (yplot_lib_rs_initialized) {
        return;
    }
    memset(&yplot_lib_rs, 0, sizeof(yplot_lib_rs));
    yetty_yrender_shader_code_set(&yplot_lib_rs.shader, (const char *)gyplot_lib_shaderData,
                                  gyplot_lib_shaderSize);
    yplot_lib_rs_initialized = true;
}

struct yetty_yplot_factory {
    struct yetty_ydraw_concrete_factory base;
    /* Shared, compiled once. NULL until compile_pipeline. */
    struct yetty_yrender_pipeline *pipeline;
    /* Template RS: shape definition for both the pipeline and per-instance
     * RSes. Children point to the shared static library RSes. */
    struct yetty_ydraw_gpu_resource_set template_rs;
    int template_initialized;

    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Zoom state — written by the canvas into the factory, read by each
     * instance render() and pushed into the instance's own RS uniforms. */
    float visual_zoom_scale;
    float visual_zoom_off_x;
    float visual_zoom_off_y;
    float cell_zoom_scale;
    float cell_zoom_off_x;
    float cell_zoom_off_y;
};

static struct yetty_yplot_factory *yetty_yplot_factory_from_base(
    struct yetty_ydraw_concrete_factory *base)
{
    return (struct yetty_yplot_factory *)base;
}

// Wire-format serialize helpers live in yplot-gen-wire.c (yetty_yplot_core).

//=============================================================================
// Resource Set Setup — populates a target RS with this prim's structure
// (uniform names/types, buffer descriptor, library children + own shader
// code). Same shape used for the factory's template_rs (pipeline-build) and
// for each per-instance RS (binder-build) — they're memcpy clones.
//=============================================================================

static void yplot_populate_rs(struct yetty_ydraw_gpu_resource_set *rs)
{
    yplot_init_lib_rs();

    memset(rs, 0, sizeof(*rs));
    strncpy(rs->namespace, "yplot", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&rs->shader, (const char *)gyplot_shaderData, gyplot_shaderSize);

    // Accessor library (generated uniforms accessors)
    rs->children[0] = (struct yetty_ydraw_gpu_resource_set *)&yplot_lib_rs;
    rs->children_count = 1;
    // Library: yfsvm
    const struct yetty_ydraw_gpu_resource_set *yfsvm_rs =
        yetty_yfsvm_get_shader_resource_set();
    if (yfsvm_rs) {
        rs->children[1] = (struct yetty_ydraw_gpu_resource_set *)yfsvm_rs;
        rs->children_count = 2;
    }

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
    strncpy(rs->uniforms[4].name, "x_min", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[4].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[4].u32 = 0;
    strncpy(rs->uniforms[5].name, "x_max", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[5].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[5].u32 = 0;
    strncpy(rs->uniforms[6].name, "y_min", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[6].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[6].u32 = 0;
    strncpy(rs->uniforms[7].name, "y_max", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[7].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[7].u32 = 0;
    strncpy(rs->uniforms[8].name, "flags", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[8].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[8].u32 = 0;
    strncpy(rs->uniforms[9].name, "function_count", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[9].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[9].u32 = 0;
    strncpy(rs->uniforms[10].name, "colors_0", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[10].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[10].u32 = 0;
    strncpy(rs->uniforms[11].name, "colors_1", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[11].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[11].u32 = 0;
    strncpy(rs->uniforms[12].name, "colors_2", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[12].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[12].u32 = 0;
    strncpy(rs->uniforms[13].name, "colors_3", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[13].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[13].u32 = 0;
    strncpy(rs->uniforms[14].name, "colors_4", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[14].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[14].u32 = 0;
    strncpy(rs->uniforms[15].name, "colors_5", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[15].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[15].u32 = 0;
    strncpy(rs->uniforms[16].name, "colors_6", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[16].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[16].u32 = 0;
    strncpy(rs->uniforms[17].name, "colors_7", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[17].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[17].u32 = 0;
    strncpy(rs->uniforms[18].name, "visual_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[18].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[18].f32 = 1.0f;
    strncpy(rs->uniforms[19].name, "visual_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[19].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[19].f32 = 0.0f;
    strncpy(rs->uniforms[20].name, "visual_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[20].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[20].f32 = 0.0f;
    strncpy(rs->uniforms[21].name, "cell_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[21].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[21].f32 = 1.0f;
    strncpy(rs->uniforms[22].name, "cell_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[22].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[22].f32 = 0.0f;
    strncpy(rs->uniforms[23].name, "cell_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[23].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[23].f32 = 0.0f;
    strncpy(rs->uniforms[24].name, "viewport_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[24].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[24].f32 = 0.0f;
    strncpy(rs->uniforms[25].name, "viewport_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[25].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[25].f32 = 0.0f;
    /* Slot 26: `time` — server-only uniform written by yplot-time.c's
     * tick handler when the compiled bytecode references LOAD_T. The
     * wire format doesn't carry this; it's filled at runtime. Slot
     * index hard-coded in YETTY_YPLOT_TIME_UNIFORM_SLOT (yplot-time.h)
     * so both sites stay in sync. */
    strncpy(rs->uniforms[26].name, "time", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[26].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[26].f32 = 0.0f;
    rs->uniform_count = 27;

    // Setup storage buffer for buffer data
    rs->buffer_count = 1;
    strncpy(rs->buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->buffers[0].readonly = 1;
}

//=============================================================================
// Instance Rendering — uses self->resource_set + self->binder; the factory
// supplies only the shared pipeline + zoom state.
//=============================================================================

static struct yetty_ycore_void_result yplot_instance_render(
    struct yetty_ydraw_figure *self, struct yetty_ydraw_target *target,
    float x, float y)
{
    if (!self || !self->buffer_data || !self->factory) {
        return YETTY_ERR(yetty_ycore_void, "invalid instance");
    }
    if (!self->resource_set || !self->binder) {
        return YETTY_ERR(yetty_ycore_void, "instance not finalised");
    }

    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self->factory);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "factory pipeline not initialized");
    }

    struct yetty_ydraw_gpu_resource_set *rs = self->resource_set;

    /* Wire layout (see yplot-gen-wire.c for the serializer):
     *   [0]  type_id
     *   [1]  payload_size (bytes after this header)
     *   [2 .. 2+UN-1]  uniforms (UN = YETTY_YPLOT_UNIFORMS_WORDS = 18)
     *   [2+UN .. end]  storage payload — handed verbatim to storage_buffer */
    const uint32_t *data = (const uint32_t *)self->buffer_data;
    const uint32_t *payload = data + 2;
    enum { UN = YETTY_YPLOT_UNIFORMS_WORDS };

    /* Uniforms 0..7: f32 bounds + ranges. */
    rs->uniforms[0].f32 = *(const float *)&payload[0];
    rs->uniforms[1].f32 = *(const float *)&payload[1];
    rs->uniforms[2].f32 = *(const float *)&payload[2];
    rs->uniforms[3].f32 = *(const float *)&payload[3];
    rs->uniforms[4].f32 = *(const float *)&payload[4];
    rs->uniforms[5].f32 = *(const float *)&payload[5];
    rs->uniforms[6].f32 = *(const float *)&payload[6];
    rs->uniforms[7].f32 = *(const float *)&payload[7];
    /* Uniforms 8..17: u32 flags + function_count + colors[8]. */
    rs->uniforms[8].u32 = payload[8];
    rs->uniforms[9].u32 = payload[9];
    rs->uniforms[10].u32 = payload[10];
    rs->uniforms[11].u32 = payload[11];
    rs->uniforms[12].u32 = payload[12];
    rs->uniforms[13].u32 = payload[13];
    rs->uniforms[14].u32 = payload[14];
    rs->uniforms[15].u32 = payload[15];
    rs->uniforms[16].u32 = payload[16];
    rs->uniforms[17].u32 = payload[17];

    /* Per-frame factory-side zoom state (uniforms 18..25 — outside the wire). */
    rs->uniforms[18].f32 = factory->visual_zoom_scale > 0.0f ? factory->visual_zoom_scale : 1.0f;
    rs->uniforms[19].f32 = factory->visual_zoom_off_x;
    rs->uniforms[20].f32 = factory->visual_zoom_off_y;
    rs->uniforms[21].f32 = factory->cell_zoom_scale > 0.0f ? factory->cell_zoom_scale : 1.0f;
    rs->uniforms[22].f32 = factory->cell_zoom_off_x;
    rs->uniforms[23].f32 = factory->cell_zoom_off_y;
    rs->uniforms[24].f32 = target->viewport.w;
    rs->uniforms[25].f32 = target->viewport.h;

    /* Override bounds_x / bounds_y with the canvas-provided position. */
    rs->uniforms[0].f32 = x;
    rs->uniforms[1].f32 = y;

    /* Storage buffer: the bytes immediately after the uniforms ARE the
     * storage payload (bytecode + variable data buffers, self-describing
     * via the [bytecode_len][...][data_count][...] header the shader walks).
     * The pointer doesn't move between renders, so we leave `dirty` alone —
     * the initial submit/finalize uploaded everything; subsequent chunk
     * updates write directly to GPU via the binder's write_buffer_chunk op. */
    const uint32_t payload_bytes = data[1];
    const uint32_t *storage = payload + UN;
    size_t storage_size = (size_t)payload_bytes - UN * sizeof(uint32_t);
    rs->buffers[0].data = (uint8_t *)(uintptr_t)storage;
    rs->buffers[0].size = storage_size;

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

    ydebug("yplot_instance_render: rendered at (%.1f, %.1f) size (%.1f x %.1f) inst=%p", x, y, w, h,
           (void *)self);
    return YETTY_OK_VOID();
}

//=============================================================================
// Factory Implementation
//=============================================================================

static struct yetty_ycore_void_result yplot_compile_pipeline(
    struct yetty_ydraw_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
    WGPUTextureFormat target_format, struct yetty_ydraw_gpu_allocator *allocator)
{
    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);

    if (factory->pipeline) {
        ydebug("yplot: factory pipeline already initialized");
        return YETTY_OK_VOID();
    }

    factory->device = device;
    factory->queue = queue;
    factory->allocator = allocator;
    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    yplot_populate_rs(&factory->template_rs);
    factory->template_initialized = 1;

    struct yetty_yrender_pipeline_ptr_result pr =
        yetty_yrender_pipeline_create(device, target_format, allocator, &factory->template_rs);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ycore_void, "yplot pipeline_create failed", pr);
    }
    factory->pipeline = pr.value;

    yinfo("yplot: pipeline compiled (shared across all instances)");
    return YETTY_OK_VOID();
}

static WGPURenderPipeline yplot_get_pipeline(struct yetty_ydraw_concrete_factory *self)
{
    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);
    return factory->pipeline ? yetty_yrender_pipeline_get_pipeline(factory->pipeline) : NULL;
}

static struct yetty_ydraw_figure_ptr_result yplot_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!buffer_data || size < sizeof(struct yetty_ydraw_raw_figure)) {
        return YETTY_ERR(yetty_ydraw_figure_ptr, "invalid buffer data");
    }

    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ydraw_figure_ptr,
                         "yplot factory pipeline not compiled");
    }

    struct yetty_ydraw_figure *instance =
        calloc(1, sizeof(struct yetty_ydraw_figure));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_figure_ptr, "allocation failed");
    }

    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "buffer alloc failed");
    }
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = YETTY_YPLOT_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = yplot_instance_render;

    struct rectangle_result aabb_res = yetty_ydraw_raw_figure_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res)) {
        instance->bounds = aabb_res.value;
    }

    /* Per-instance RS. Same shape as the factory template (so the binder
     * flattens to the same layout the pipeline was compiled against), but
     * with per-instance buffer/uniform values (set in render). */
    instance->resource_set = malloc(sizeof(struct yetty_ydraw_gpu_resource_set));
    if (!instance->resource_set) {
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "rs alloc failed");
    }
    memcpy(instance->resource_set, &factory->template_rs,
           sizeof(struct yetty_ydraw_gpu_resource_set));

    /* Point the storage buffer descriptor at the wire's storage payload
     * (bytes after the uniforms), so the binder's first finalize allocates
     * a GPU buffer of the right size and queueWriteBuffers the data. */
    {
        const uint32_t *data = (const uint32_t *)instance->buffer_data;
        const uint32_t *payload = data + 2;
        uint32_t payload_bytes = data[1];
        const uint32_t *storage = payload + YETTY_YPLOT_UNIFORMS_WORDS;
        size_t storage_size =
            (size_t)payload_bytes - YETTY_YPLOT_UNIFORMS_WORDS * sizeof(uint32_t);
        instance->resource_set->buffers[0].data = (uint8_t *)(uintptr_t)storage;
        instance->resource_set->buffers[0].size = storage_size;
        instance->resource_set->buffers[0].dirty = 1;
    }

    /* Per-instance binder bound to the factory's shared pipeline. Owns
     * its OWN uniform_buffer / storage_buffer / bind_group. */
    struct yetty_yrender_gpu_resource_binder_result br =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(
            factory->device, factory->queue, factory->allocator, factory->pipeline);
    if (YETTY_IS_ERR(br)) {
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr,
                         "instance binder create failed", br);
    }
    instance->binder = br.value;

    struct yetty_ycore_void_result sr =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(sr)) {
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "binder submit failed", sr);
    }

    struct yetty_ycore_void_result fr = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(fr)) {
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "binder finalize failed", fr);
    }

    /* yplot-time.c hooks the instance into the shared animation timer
     * iff the wire flags carry YETTY_YPLOT_FLAG_USES_TIME. No-op on
     * static plots. Failure is non-fatal — the plot still renders, it
     * just stays frozen at t=0. */
    {
        struct yetty_ycore_void_result tr = yetty_yplot_time_attach(instance);
        if (YETTY_IS_ERR(tr)) {
            ywarn("yplot: time-attach failed: %s", tr.error.msg);
            yetty_ycore_error_destroy(tr.error);
        }
    }

    return YETTY_OK(yetty_ydraw_figure_ptr, instance);
}

/* CMD_UPDATE payload schema (defined by yplot):
 *   u32 buffer_index   — index into the `data` array of the yplot
 *   u32 sample_offset  — first sample to overwrite (in f32s into THAT buffer)
 *   u32 count          — number of f32 samples
 *   f32 samples[count] — new sample values
 * Total header = 12 bytes, plus count * 4 bytes of samples. */
static struct yetty_ycore_void_result yplot_update_instance(
    struct yetty_ydraw_concrete_factory *self, struct yetty_ydraw_figure *instance,
    const void *payload, size_t size)
{
    (void)self;
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "yplot update_instance: instance NULL");
    }
    if (!payload || size < 12u) {
        return YETTY_ERR(yetty_ycore_void, "yplot update_instance: payload header truncated");
    }
    const uint32_t *header = (const uint32_t *)payload;
    uint32_t buffer_index = header[0];
    uint32_t sample_offset = header[1];
    uint32_t count = header[2];
    size_t expected = 12u + (size_t)count * sizeof(float);
    if (size < expected) {
        return YETTY_ERR(yetty_ycore_void, "yplot update_instance: payload samples truncated");
    }
    const float *samples = (const float *)((const uint8_t *)payload + 12u);
    return yetty_yplot_update_data_chunk(instance, buffer_index, sample_offset, samples, count);
}

static void yplot_destroy_instance(struct yetty_ydraw_concrete_factory *self,
                                   struct yetty_ydraw_figure *instance)
{
    (void)self;
    if (!instance) {
        return;
    }
    /* yplot-time.c — drop the timer listener BEFORE freeing the
     * instance struct (its embedded listener pointer is what the
     * timer's listener-list holds). */
    yetty_yplot_time_detach(instance);
    if (instance->binder) {
        instance->binder->ops->destroy(instance->binder);
    }
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}

static struct yetty_ydraw_gpu_resource_set *yplot_get_shared_rs(
    struct yetty_ydraw_concrete_factory *self)
{
    /* Returns the structural template, NOT a mutable per-instance RS. */
    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);
    return factory->template_initialized ? &factory->template_rs : NULL;
}

static struct yetty_ycore_void_result yplot_set_visual_zoom(
    struct yetty_ydraw_concrete_factory *self, float scale, float off_x, float off_y)
{
    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);
    factory->visual_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->visual_zoom_off_x = off_x;
    factory->visual_zoom_off_y = off_y;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yplot_set_cell_zoom(
    struct yetty_ydraw_concrete_factory *self, float scale, float off_x, float off_y)
{
    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);
    factory->cell_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->cell_zoom_off_x = off_x;
    factory->cell_zoom_off_y = off_y;
    ydebug("yplot_set_cell_zoom: scale=%.3f off=(%.1f,%.1f)", scale, off_x, off_y);
    return YETTY_OK_VOID();
}

struct yetty_ydraw_concrete_factory *yetty_yplot_factory_create(void)
{
    struct yetty_yplot_factory *factory = calloc(1, sizeof(struct yetty_yplot_factory));
    if (!factory) {
        return NULL;
    }

    factory->base.type_id = YETTY_YPLOT_TYPE_ID;
    factory->base.compile_pipeline = yplot_compile_pipeline;
    factory->base.get_pipeline = yplot_get_pipeline;
    factory->base.create_instance = yplot_create_instance;
    factory->base.destroy_instance = yplot_destroy_instance;
    factory->base.update_instance = yplot_update_instance;
    factory->base.get_shared_rs = yplot_get_shared_rs;
    factory->base.set_visual_zoom = yplot_set_visual_zoom;
    factory->base.set_cell_zoom = yplot_set_cell_zoom;

    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    return &factory->base;
}

void yetty_yplot_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    if (!self) {
        return;
    }

    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);

    if (factory->pipeline) {
        yetty_yrender_pipeline_destroy(factory->pipeline);
    }
    free(factory);
}

//=============================================================================
// Chunk-update API — push a fresh slice of samples into one of the
// instance's data buffers. Writes both into the in-memory wire (so the next
// re-finalize / re-upload picks up the same bytes) and directly to GPU via
// the binder's write_buffer_chunk op (so no whole-buffer re-upload occurs).
//=============================================================================

struct yetty_ycore_void_result yetty_yplot_update_data_chunk(
    struct yetty_ydraw_figure *instance,
    uint32_t buffer_index, uint32_t sample_offset,
    const float *data, size_t count)
{
    if (!instance || !instance->buffer_data || !instance->binder) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: invalid instance");
    }
    if (!data || count == 0) {
        return YETTY_OK_VOID();
    }
    if (instance->type != YETTY_YPLOT_TYPE_ID) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: not a yplot instance");
    }

    /* Walk the storage payload to find buffer_index's [len][samples...]
     * slot inside the merged region. Storage starts right after the
     * uniforms in the wire payload. */
    uint32_t *wire = (uint32_t *)instance->buffer_data;
    uint32_t *storage = wire + 2 + YETTY_YPLOT_UNIFORMS_WORDS;

    uint32_t bytecode_len = storage[0];
    uint32_t *p = storage + 1u + bytecode_len; /* points at data_count */
    uint32_t data_count = *p++;
    if (buffer_index >= data_count) {
        return YETTY_ERR(yetty_ycore_void, "update_data_chunk: buffer_index out of range");
    }
    for (uint32_t i = 0; i < buffer_index; i++) {
        uint32_t li = *p++;
        p += li;
    }
    uint32_t this_len = *p; /* len_buffer_index */
    if ((size_t)sample_offset + count > (size_t)this_len) {
        return YETTY_ERR(yetty_ycore_void,
                         "update_data_chunk: chunk would overflow buffer length");
    }

    /* Destination word in the merged storage region (in u32 units, then × 4
     * for bytes — every word is a 32-bit float bitcast). */
    uint32_t *dst_samples = p + 1u + sample_offset;
    size_t bytes = count * sizeof(float);

    /* 1) Keep the in-memory wire consistent — any re-finalize / cold
     *    re-upload (e.g. after the binder resizes its slot) will see the
     *    latest samples without going back through the high-level
     *    yetty_yplot_render path. */
    memcpy(dst_samples, data, bytes);

    /* 2) Push the same bytes to GPU directly — single wgpuQueueWriteBuffer
     *    at the precomputed offset, no whole-buffer re-upload. */
    size_t byte_offset_in_storage = (size_t)((uint8_t *)dst_samples - (uint8_t *)storage);
    struct yetty_ycore_void_result wr = instance->binder->ops->write_buffer_chunk(
        instance->binder, /*buffer_index=*/0, byte_offset_in_storage, data, bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "update_data_chunk: binder write failed");
    return YETTY_OK_VOID();
}
