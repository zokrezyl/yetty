// Auto-generated from yplot.yaml - DO NOT EDIT
//
// Two-tier composite model:
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
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yfsvm/shader-rs.h>

/* Legacy factory adapter — kept so the abstract factory's
 * update_instance slot still resolves. scene-canvas now routes through
 * fi->ops->update directly, so this is dead in the runtime path but
 * gets removed when the factory loses the slot. The instance_update
 * itself is hand-written in a companion TU (see the generated header). */
static struct yetty_ycore_void_result yplot_update_dispatch(
    struct yetty_ydraw_concrete_factory *self, struct yetty_ydraw_composite *instance,
    const void *payload, size_t size)
{
    (void)self;
    if (!payload || size < 4u) {
        return YETTY_ERR(yetty_ycore_void, "yplot update_dispatch: payload header truncated");
    }
    uint32_t target_field = ((const uint32_t *)payload)[0];
    return yetty_yplot_instance_update(instance, target_field, (const uint8_t *)payload + 4u,
                                       size - 4u);
}

extern const unsigned char gyplot_shaderData[];
extern const unsigned int gyplot_shaderSize;
extern const unsigned char gyplot_lib_shaderData[];
extern const unsigned int gyplot_lib_shaderSize;

/* Static resource set for accessor library (yplot-gen.wgsl).
 * Read-only after init; safely shared across all instances as a child. */
static struct yetty_yrender_gpu_resource_set yplot_lib_rs;
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
    struct yetty_yrender_gpu_resource_set template_rs;
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

// Wire-format serialize helpers live in yplot-gen-wire.c
// (yetty_yplot_core, GPU-less, riscv64-safe).

//=============================================================================
// Resource Set Setup — populates a target RS with this prim's structure
// (uniform names/types, buffer descriptor, library children + own shader
// code). Same shape used for the factory's template_rs (pipeline-build) and
// for each per-instance RS (binder-build) — they're memcpy clones.
//=============================================================================

static void yplot_populate_rs(struct yetty_yrender_gpu_resource_set *rs)
{
    yplot_init_lib_rs();

    memset(rs, 0, sizeof(*rs));
    strncpy(rs->namespace, "yplot", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&rs->shader, (const char *)gyplot_shaderData, gyplot_shaderSize);

    // Accessor library (generated uniforms accessors)
    rs->children[0] = (struct yetty_yrender_gpu_resource_set *)&yplot_lib_rs;
    rs->children_count = 1;
    // Library: yfsvm
    const struct yetty_yrender_gpu_resource_set *yfsvm_rs = yetty_yfsvm_get_shader_resource_set();
    if (yfsvm_rs) {
        rs->children[1] = (struct yetty_yrender_gpu_resource_set *)yfsvm_rs;
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
    strncpy(rs->uniforms[8].name, "x_step", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[8].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[8].u32 = 0;
    strncpy(rs->uniforms[9].name, "y_step", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[9].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[9].u32 = 0;
    strncpy(rs->uniforms[10].name, "colormap_id", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[10].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[10].u32 = 0;
    strncpy(rs->uniforms[11].name, "field_min", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[11].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[11].u32 = 0;
    strncpy(rs->uniforms[12].name, "field_max", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[12].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[12].u32 = 0;
    strncpy(rs->uniforms[13].name, "flags", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[13].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[13].u32 = 0;
    strncpy(rs->uniforms[14].name, "function_count", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[14].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[14].u32 = 0;
    strncpy(rs->uniforms[15].name, "colors_0", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[15].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[15].u32 = 0;
    strncpy(rs->uniforms[16].name, "colors_1", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[16].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[16].u32 = 0;
    strncpy(rs->uniforms[17].name, "colors_2", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[17].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[17].u32 = 0;
    strncpy(rs->uniforms[18].name, "colors_3", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[18].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[18].u32 = 0;
    strncpy(rs->uniforms[19].name, "colors_4", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[19].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[19].u32 = 0;
    strncpy(rs->uniforms[20].name, "colors_5", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[20].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[20].u32 = 0;
    strncpy(rs->uniforms[21].name, "colors_6", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[21].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[21].u32 = 0;
    strncpy(rs->uniforms[22].name, "colors_7", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[22].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[22].u32 = 0;
    strncpy(rs->uniforms[23].name, "band_slots_0", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[23].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[23].u32 = 0;
    strncpy(rs->uniforms[24].name, "band_slots_1", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[24].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[24].u32 = 0;
    strncpy(rs->uniforms[25].name, "band_slots_2", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[25].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[25].u32 = 0;
    strncpy(rs->uniforms[26].name, "band_slots_3", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[26].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[26].u32 = 0;
    strncpy(rs->uniforms[27].name, "band_slots_4", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[27].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[27].u32 = 0;
    strncpy(rs->uniforms[28].name, "band_slots_5", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[28].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[28].u32 = 0;
    strncpy(rs->uniforms[29].name, "band_slots_6", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[29].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[29].u32 = 0;
    strncpy(rs->uniforms[30].name, "band_slots_7", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[30].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[30].u32 = 0;
    strncpy(rs->uniforms[31].name, "hidden_mask", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[31].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[31].u32 = 0;
    strncpy(rs->uniforms[32].name, "ring_heads_0", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[32].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[32].u32 = 0;
    strncpy(rs->uniforms[33].name, "ring_heads_1", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[33].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[33].u32 = 0;
    strncpy(rs->uniforms[34].name, "ring_heads_2", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[34].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[34].u32 = 0;
    strncpy(rs->uniforms[35].name, "ring_heads_3", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[35].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[35].u32 = 0;
    strncpy(rs->uniforms[36].name, "ring_heads_4", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[36].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[36].u32 = 0;
    strncpy(rs->uniforms[37].name, "ring_heads_5", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[37].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[37].u32 = 0;
    strncpy(rs->uniforms[38].name, "ring_heads_6", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[38].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[38].u32 = 0;
    strncpy(rs->uniforms[39].name, "ring_heads_7", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[39].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[39].u32 = 0;
    strncpy(rs->uniforms[40].name, "visual_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[40].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[40].f32 = 1.0f;
    strncpy(rs->uniforms[41].name, "visual_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[41].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[41].f32 = 0.0f;
    strncpy(rs->uniforms[42].name, "visual_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[42].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[42].f32 = 0.0f;
    strncpy(rs->uniforms[43].name, "cell_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[43].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[43].f32 = 1.0f;
    strncpy(rs->uniforms[44].name, "cell_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[44].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[44].f32 = 0.0f;
    strncpy(rs->uniforms[45].name, "cell_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[45].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[45].f32 = 0.0f;
    strncpy(rs->uniforms[46].name, "viewport_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[46].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[46].f32 = 0.0f;
    strncpy(rs->uniforms[47].name, "viewport_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[47].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[47].f32 = 0.0f;
    /* `time` — server-side, not on the wire. */
    strncpy(rs->uniforms[48].name, "time", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[48].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[48].f32 = 0.0f;
    rs->uniform_count = 49;

    // Single merged storage buffer — the shader walks the
    // self-describing [len][data]... layout at runtime.
    rs->buffer_count = 1;
    strncpy(rs->buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->buffers[0].readonly = 1;
}

//=============================================================================
// Instance Rendering — uses self->resource_set + self->binder; the factory
// supplies only the shared pipeline + zoom state.
//=============================================================================

static struct yetty_ycore_void_result yplot_instance_render(struct yetty_ydraw_composite *self,
                                                            struct yetty_ydraw_target *target,
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

    struct yetty_yrender_gpu_resource_set *rs = self->resource_set;

    // Parse wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    const uint32_t *data = (const uint32_t *)self->buffer_data;
    const uint32_t *payload = data + 2; // skip type_id and payload_size

    // Update uniforms from wire format
    rs->uniforms[0].f32 = *(float *)&payload[0];   /* bounds_x */
    rs->uniforms[1].f32 = *(float *)&payload[1];   /* bounds_y */
    rs->uniforms[2].f32 = *(float *)&payload[2];   /* bounds_w */
    rs->uniforms[3].f32 = *(float *)&payload[3];   /* bounds_h */
    rs->uniforms[4].f32 = *(float *)&payload[4];   /* x_min */
    rs->uniforms[5].f32 = *(float *)&payload[5];   /* x_max */
    rs->uniforms[6].f32 = *(float *)&payload[6];   /* y_min */
    rs->uniforms[7].f32 = *(float *)&payload[7];   /* y_max */
    rs->uniforms[8].f32 = *(float *)&payload[8];   /* x_step */
    rs->uniforms[9].f32 = *(float *)&payload[9];   /* y_step */
    rs->uniforms[10].u32 = payload[10];            /* colormap_id */
    rs->uniforms[11].f32 = *(float *)&payload[11]; /* field_min */
    rs->uniforms[12].f32 = *(float *)&payload[12]; /* field_max */
    rs->uniforms[13].u32 = payload[13];            /* flags */
    rs->uniforms[14].u32 = payload[14];            /* function_count */
    rs->uniforms[15].u32 = payload[15];            /* colors_0 */
    rs->uniforms[16].u32 = payload[16];            /* colors_1 */
    rs->uniforms[17].u32 = payload[17];            /* colors_2 */
    rs->uniforms[18].u32 = payload[18];            /* colors_3 */
    rs->uniforms[19].u32 = payload[19];            /* colors_4 */
    rs->uniforms[20].u32 = payload[20];            /* colors_5 */
    rs->uniforms[21].u32 = payload[21];            /* colors_6 */
    rs->uniforms[22].u32 = payload[22];            /* colors_7 */
    rs->uniforms[23].u32 = payload[23];            /* band_slots_0 */
    rs->uniforms[24].u32 = payload[24];            /* band_slots_1 */
    rs->uniforms[25].u32 = payload[25];            /* band_slots_2 */
    rs->uniforms[26].u32 = payload[26];            /* band_slots_3 */
    rs->uniforms[27].u32 = payload[27];            /* band_slots_4 */
    rs->uniforms[28].u32 = payload[28];            /* band_slots_5 */
    rs->uniforms[29].u32 = payload[29];            /* band_slots_6 */
    rs->uniforms[30].u32 = payload[30];            /* band_slots_7 */
    rs->uniforms[31].u32 = payload[31];            /* hidden_mask */
    rs->uniforms[32].u32 = payload[32];            /* ring_heads_0 */
    rs->uniforms[33].u32 = payload[33];            /* ring_heads_1 */
    rs->uniforms[34].u32 = payload[34];            /* ring_heads_2 */
    rs->uniforms[35].u32 = payload[35];            /* ring_heads_3 */
    rs->uniforms[36].u32 = payload[36];            /* ring_heads_4 */
    rs->uniforms[37].u32 = payload[37];            /* ring_heads_5 */
    rs->uniforms[38].u32 = payload[38];            /* ring_heads_6 */
    rs->uniforms[39].u32 = payload[39];            /* ring_heads_7 */

    // Pull current zoom state from the factory into this instance's RS.
    rs->uniforms[40].f32 = factory->visual_zoom_scale > 0.0f ? factory->visual_zoom_scale : 1.0f;
    rs->uniforms[41].f32 = factory->visual_zoom_off_x;
    rs->uniforms[42].f32 = factory->visual_zoom_off_y;
    rs->uniforms[43].f32 = factory->cell_zoom_scale > 0.0f ? factory->cell_zoom_scale : 1.0f;
    rs->uniforms[44].f32 = factory->cell_zoom_off_x;
    rs->uniforms[45].f32 = factory->cell_zoom_off_y;

    // Visual-zoom viewport — read from the target every frame.
    rs->uniforms[46].f32 = target->viewport.w;
    rs->uniforms[47].f32 = target->viewport.h;

    // Compose the caller-provided pane position with the record's own
    // ENVELOPE-LOCAL origin: a multi-figure envelope (a browser page's
    // images) lays its figures out internally, so each record's wire
    // bounds_x/y is its offset inside the block anchored at (x, y).
    // Single-figure producers (ycat) ship a 0,0 origin — for them this
    // reduces to the plain caller position. bounds_w/h are LOGICAL pixels;
    // the offset and body both scale by content_scale (1.0 in the
    // terminal's local compositor).
    {
        float figure_content_scale = self->content_scale > 0.0f ? self->content_scale : 1.0f;
        rs->uniforms[0].f32 = x + rs->uniforms[0].f32 * figure_content_scale;
        rs->uniforms[1].f32 = y + rs->uniforms[1].f32 * figure_content_scale;
        rs->uniforms[2].f32 *= figure_content_scale;
        rs->uniforms[3].f32 *= figure_content_scale;
    }

    /* Merged storage region: everything after the uniforms, handed
     * verbatim to the storage buffer (the shader walks its header).
     * Deliberately NOT marked dirty here — the initial upload happens at
     * create, and incremental sample updates write directly to GPU via
     * the binder's write_buffer_chunk op; a per-frame dirty would
     * re-upload the whole region every frame. */
    uint32_t payload_bytes = data[1];
    const uint32_t *storage = payload + 40;
    size_t storage_size = (size_t)payload_bytes - 40u * sizeof(uint32_t);
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

    /* The pane's render target may sit at a non-zero offset inside the
     * big surface (e.g. yui pushes the terminal pane down by the titlebar
     * height). The layer's simple-prim pass already draws to
     * (vp.x, vp.y, vp.w, vp.h); the complex prim must use the same rect,
     * otherwise its fullscreen triangle covers a different region of the
     * framebuffer than the rest of the layer and the FS would compare
     * canvas-local bounds against the wrong coordinate system — see the
     * pane_pixel comment in the matching shader. */
    wgpuRenderPassEncoderSetViewport(pass, target->viewport.x, target->viewport.y,
                                     target->viewport.w, target->viewport.h, 0.0f, 1.0f);

    /* Scissor to the viewport, intersected with the compositor's clip rect
     * when one is set (e.g. a scrolling ygrid's scroll-area bounds) so the
     * figure is clipped to its container instead of bleeding over
     * surrounding chrome such as the tab bar. */
    float scissor_x0 = target->viewport.x;
    float scissor_y0 = target->viewport.y;
    float scissor_x1 = target->viewport.x + target->viewport.w;
    float scissor_y1 = target->viewport.y + target->viewport.h;
    if (target->clip.w > 0.0f && target->clip.h > 0.0f) {
        if (target->clip.x > scissor_x0) {
            scissor_x0 = target->clip.x;
        }
        if (target->clip.y > scissor_y0) {
            scissor_y0 = target->clip.y;
        }
        if (target->clip.x + target->clip.w < scissor_x1) {
            scissor_x1 = target->clip.x + target->clip.w;
        }
        if (target->clip.y + target->clip.h < scissor_y1) {
            scissor_y1 = target->clip.y + target->clip.h;
        }
    }
    if (scissor_x1 < scissor_x0) {
        scissor_x1 = scissor_x0;
    }
    if (scissor_y1 < scissor_y0) {
        scissor_y1 = scissor_y0;
    }
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)scissor_x0, (uint32_t)scissor_y0,
                                        (uint32_t)(scissor_x1 - scissor_x0),
                                        (uint32_t)(scissor_y1 - scissor_y0));

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

/* Forward decl — vtable definition lives below; create_instance just
 * needs its address. */
static const struct yetty_ydraw_composite_ops yplot_figure_ops;

static struct yetty_ydraw_composite_ptr_result yplot_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!buffer_data || size < sizeof(struct yetty_ydraw_composite_record)) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "invalid buffer data");
    }

    /* Bounds-check the wire record against its declared payload_size. */
    {
        if (size < 2u * sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ydraw_composite_ptr, "yplot: record too small for header");
        }
        uint64_t declared_payload = (uint64_t)((const uint32_t *)buffer_data)[1];
        if (2u * sizeof(uint32_t) + declared_payload > (uint64_t)size) {
            return YETTY_ERR(yetty_ydraw_composite_ptr, "yplot: payload exceeds wire record");
        }
    }

    struct yetty_yplot_factory *factory = yetty_yplot_factory_from_base(self);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yplot factory pipeline not compiled");
    }

    struct yetty_ydraw_composite *instance = calloc(1, sizeof(struct yetty_ydraw_composite));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "allocation failed");
    }

    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "buffer alloc failed");
    }
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = YETTY_YPLOT_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = yplot_instance_render;
    instance->ops = &yplot_figure_ops;

    struct rectangle_result aabb_res = yetty_ydraw_composite_record_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res)) {
        instance->bounds = aabb_res.value;
    }

    /* Per-instance RS. Same shape as the factory template (so the binder
     * flattens to the same layout the pipeline was compiled against), but
     * with per-instance buffer/uniform values (set in render). */
    instance->resource_set = malloc(sizeof(struct yetty_yrender_gpu_resource_set));
    if (!instance->resource_set) {
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "rs alloc failed");
    }
    memcpy(instance->resource_set, &factory->template_rs,
           sizeof(struct yetty_yrender_gpu_resource_set));

    /* Point the storage buffer descriptor at this instance's bytecode now,
     * so the binder's first finalize allocates a GPU buffer of the right
     * size and queueWriteBuffers the data. */
    {
        const uint32_t *data = (const uint32_t *)instance->buffer_data;
        const uint32_t *payload = data + 2;
        uint32_t payload_bytes = data[1];
        const uint32_t *storage = payload + 40;
        size_t storage_size = (size_t)payload_bytes - 40u * sizeof(uint32_t);
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
        ydebug("yplot_create_instance: binder_create FAILED: %s", br.error.msg);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "instance binder create failed", br);
    }
    instance->binder = br.value;

    struct yetty_ycore_void_result sr =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(sr)) {
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "binder submit failed", sr);
    }

    struct yetty_ycore_void_result fr = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(fr)) {
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "binder finalize failed", fr);
    }

    /* Companion-TU lifecycle callout (e.g. hooking the instance into a
     * shared animation timer). Runs after the binder is fully finalized.
     * Failure is non-fatal — the figure still renders, it just skips
     * whatever the callout would have enabled. */
    {
        struct yetty_ycore_void_result created_res = yetty_yplot_instance_created(instance);
        if (YETTY_IS_ERR(created_res)) {
            ywarn("yplot: instance_created callout failed: %s", created_res.error.msg);
            yetty_ycore_error_destroy(created_res.error);
        }
    }
    ydebug("yplot_create_instance: OK bounds=(%.0f,%.0f,%.0f,%.0f)", instance->bounds.min.x,
           instance->bounds.min.y, instance->bounds.max.x, instance->bounds.max.y);
    return YETTY_OK(yetty_ydraw_composite_ptr, instance);
}

static void yplot_instance_destroy(struct yetty_ydraw_composite *instance)
{
    if (!instance) {
        return;
    }
    /* Companion-TU teardown FIRST — e.g. dropping a timer listener
	 * whose list holds a pointer into this instance. */
    yetty_yplot_instance_destroying(instance);
    if (instance->binder) {
        instance->binder->ops->destroy(instance->binder);
    }
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}

/* Per-instance vtable installed on every yplot figure_instance. */
static const struct yetty_ydraw_composite_ops yplot_figure_ops = {
    .destroy = yplot_instance_destroy,
    .update = yetty_yplot_instance_update,
};

/* Legacy factory adapter — kept for the factory->destroy_instance
 * fallback path. */
static void yplot_destroy_instance(struct yetty_ydraw_concrete_factory *self,
                                   struct yetty_ydraw_composite *instance)
{
    (void)self;
    yplot_instance_destroy(instance);
}

static struct yetty_yrender_gpu_resource_set *yplot_get_shared_rs(
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

static struct yetty_ycore_void_result yplot_set_cell_zoom(struct yetty_ydraw_concrete_factory *self,
                                                          float scale, float off_x, float off_y)
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
    factory->base.destroy = yetty_yplot_factory_destroy;
    factory->base.compile_pipeline = yplot_compile_pipeline;
    factory->base.get_pipeline = yplot_get_pipeline;
    factory->base.create_instance = yplot_create_instance;
    factory->base.destroy_instance = yplot_destroy_instance;
    factory->base.update_instance = yplot_update_dispatch;
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
