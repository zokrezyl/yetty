// Auto-generated from yvideo.yaml - DO NOT EDIT
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

#include <yetty/yvideo/yvideo-gen.h>
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

/* Hook surface — see hooks_enabled() in ydraw-gen/generate.py. Implemented
 * in yvideo-hooks.c (hand-written). Missing symbols are a link error. */
extern struct yetty_ycore_void_result yvideo_hook_instance_create(
    struct yetty_ydraw_figure *instance, const void *buffer_data, size_t size);
extern void yvideo_hook_instance_destroy(struct yetty_ydraw_figure *instance);
extern struct yetty_ycore_void_result yvideo_hook_instance_update(
    struct yetty_ydraw_figure *instance, const void *payload, size_t size);
extern struct yetty_ycore_void_result yvideo_hook_instance_render_pre(
    struct yetty_ydraw_figure *instance, struct yetty_ydraw_target *target, float x, float y);

/* Instance update via the figure_ops vtable.
 *
 * yvideo's legacy wire payload is `[u8 op][u8 reserved×3][body…]` — the
 * first u32 of the payload is `op | reserved<<8…`, so under the new
 * generic CMD_UPDATE dispatcher (scene-canvas peels the first u32 off
 * as `target_field`) we get `target_field == op` (with the reserved
 * bytes folded into the upper bits, which were always zero on the
 * wire). To keep yvideo_hook_instance_update unmodified, repack the
 * header into the legacy shape before forwarding. The repack is
 * stack-sized + cheap; only the 4-byte header has to be re-emitted,
 * the body is referenced in place. */
static struct yetty_ycore_void_result yvideo_instance_update(struct yetty_ydraw_figure *instance,
                                                             uint32_t target_field,
                                                             const void *body, size_t body_size)
{
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update: instance NULL");
    }
    if (body_size > 0 && !body) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update: NULL body with non-zero size");
    }
    /* Reassemble the [op][reserved×3][body] payload the hook still
     * expects. Avoid a heap alloc for typical update sizes; fall back
     * to malloc only for unusually large bodies. */
    enum { STACK_PAYLOAD_MAX = 4096u };
    uint8_t stack_buf[STACK_PAYLOAD_MAX];
    size_t total = 4u + body_size;
    uint8_t *payload;
    bool heap = false;
    if (total <= sizeof(stack_buf)) {
        payload = stack_buf;
    } else {
        payload = (uint8_t *)malloc(total);
        if (!payload) {
            return YETTY_ERR(yetty_ycore_void, "yvideo update: payload alloc failed");
        }
        heap = true;
    }
    payload[0] = (uint8_t)(target_field & 0xFFu);
    payload[1] = (uint8_t)((target_field >> 8) & 0xFFu);
    payload[2] = (uint8_t)((target_field >> 16) & 0xFFu);
    payload[3] = (uint8_t)((target_field >> 24) & 0xFFu);
    if (body_size > 0) {
        memcpy(payload + 4u, body, body_size);
    }
    struct yetty_ycore_void_result r = yvideo_hook_instance_update(instance, payload, total);
    if (heap) {
        free(payload);
    }
    return r;
}

/* Forward decl — vtable definition lives below; the create + legacy
 * factory adapter both need its address. */
static const struct yetty_ydraw_figure_ops yvideo_figure_ops;

/* Legacy factory adapter — kept so the abstract factory's
 * update_instance slot still resolves. scene-canvas now routes through
 * fi->ops->update directly, so this is dead in the runtime path but
 * gets removed when the factory loses the slot. */
static struct yetty_ycore_void_result yvideo_update_dispatch(
    struct yetty_ydraw_concrete_factory *self, struct yetty_ydraw_figure *instance,
    const void *payload, size_t size)
{
    (void)self;
    if (!payload || size < 4u) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update_dispatch: payload header truncated");
    }
    uint32_t target_field = ((const uint32_t *)payload)[0];
    return yvideo_instance_update(instance, target_field, (const uint8_t *)payload + 4u, size - 4u);
}

extern const unsigned char gyvideo_shaderData[];
extern const unsigned int gyvideo_shaderSize;
extern const unsigned char gyvideo_lib_shaderData[];
extern const unsigned int gyvideo_lib_shaderSize;

/* Static resource set for accessor library (yvideo-gen.wgsl).
 * Read-only after init; safely shared across all instances as a child. */
static struct yetty_yrender_gpu_resource_set yvideo_lib_rs;
static bool yvideo_lib_rs_initialized = false;

static void yvideo_init_lib_rs(void)
{
    if (yvideo_lib_rs_initialized) {
        return;
    }
    memset(&yvideo_lib_rs, 0, sizeof(yvideo_lib_rs));
    yetty_yrender_shader_code_set(&yvideo_lib_rs.shader, (const char *)gyvideo_lib_shaderData,
                                  gyvideo_lib_shaderSize);
    yvideo_lib_rs_initialized = true;
}

struct yetty_yvideo_factory {
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

static struct yetty_yvideo_factory *yetty_yvideo_factory_from_base(
    struct yetty_ydraw_concrete_factory *base)
{
    return (struct yetty_yvideo_factory *)base;
}

// Wire-format serialize helpers live in yvideo-gen-wire.c
// (yetty_yvideo_core, GPU-less, riscv64-safe).

//=============================================================================
// Resource Set Setup — populates a target RS with this prim's structure
// (uniform names/types, buffer descriptor, library children + own shader
// code). Same shape used for the factory's template_rs (pipeline-build) and
// for each per-instance RS (binder-build) — they're memcpy clones.
//=============================================================================

static void yvideo_populate_rs(struct yetty_yrender_gpu_resource_set *rs)
{
    yvideo_init_lib_rs();

    memset(rs, 0, sizeof(*rs));
    strncpy(rs->namespace, "yvideo", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&rs->shader, (const char *)gyvideo_shaderData,
                                  gyvideo_shaderSize);

    // Accessor library (generated uniforms accessors)
    rs->children[0] = (struct yetty_yrender_gpu_resource_set *)&yvideo_lib_rs;
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
    strncpy(rs->uniforms[4].name, "video_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[4].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[4].u32 = 0;
    strncpy(rs->uniforms[5].name, "video_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[5].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[5].u32 = 0;
    strncpy(rs->uniforms[6].name, "chroma_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[6].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[6].u32 = 0;
    strncpy(rs->uniforms[7].name, "chroma_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[7].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[7].u32 = 0;
    strncpy(rs->uniforms[8].name, "fps", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[8].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[8].u32 = 0;
    strncpy(rs->uniforms[9].name, "color_matrix", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[9].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[9].u32 = 0;
    strncpy(rs->uniforms[10].name, "flags", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[10].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[10].u32 = 0;
    /* v2 audio uniforms — see yvideo.yaml. */
    strncpy(rs->uniforms[11].name, "audio_codec", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[11].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[11].u32 = 0;
    strncpy(rs->uniforms[12].name, "audio_sample_rate", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[12].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[12].u32 = 0;
    strncpy(rs->uniforms[13].name, "audio_channels", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[13].type = YETTY_YRENDER_UNIFORM_U32;
    rs->uniforms[13].u32 = 0;
    strncpy(rs->uniforms[14].name, "visual_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[14].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[14].f32 = 1.0f;
    strncpy(rs->uniforms[15].name, "visual_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[15].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[15].f32 = 0.0f;
    strncpy(rs->uniforms[16].name, "visual_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[16].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[16].f32 = 0.0f;
    strncpy(rs->uniforms[17].name, "cell_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[17].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[17].f32 = 1.0f;
    strncpy(rs->uniforms[18].name, "cell_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[18].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[18].f32 = 0.0f;
    strncpy(rs->uniforms[19].name, "cell_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[19].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[19].f32 = 0.0f;
    strncpy(rs->uniforms[20].name, "viewport_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[20].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[20].f32 = 0.0f;
    strncpy(rs->uniforms[21].name, "viewport_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[21].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[21].f32 = 0.0f;
    /* Per-texture atlas region uniforms — one vec4(u0,v0,u1,v1) each.
     * Order matches the textures: array below: y_plane, u_plane, v_plane. */
    strncpy(rs->uniforms[22].name, "y_plane_region", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[22].type = YETTY_YRENDER_UNIFORM_VEC4;
    rs->uniforms[22].vec4[0] = 0.0f;
    rs->uniforms[22].vec4[1] = 0.0f;
    rs->uniforms[22].vec4[2] = 1.0f;
    rs->uniforms[22].vec4[3] = 1.0f;
    strncpy(rs->uniforms[23].name, "u_plane_region", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[23].type = YETTY_YRENDER_UNIFORM_VEC4;
    rs->uniforms[23].vec4[0] = 0.0f;
    rs->uniforms[23].vec4[1] = 0.0f;
    rs->uniforms[23].vec4[2] = 1.0f;
    rs->uniforms[23].vec4[3] = 1.0f;
    strncpy(rs->uniforms[24].name, "v_plane_region", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[24].type = YETTY_YRENDER_UNIFORM_VEC4;
    rs->uniforms[24].vec4[0] = 0.0f;
    rs->uniforms[24].vec4[1] = 0.0f;
    rs->uniforms[24].vec4[2] = 1.0f;
    rs->uniforms[24].vec4[3] = 1.0f;
    rs->uniform_count = 25;

    // Setup storage buffers — nal_stream and audio_stream. The shader
    // doesn't read either today (decode happens host-side in
    // yvideo-hooks.c); they're bound as a side-effect of the wire-format
    // generator pattern. The `host_only` flag in ydraw-gen that would
    // elide the GPU binding is still pending.
    rs->buffer_count = 2;
    strncpy(rs->buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->buffers[0].readonly = 1;
    strncpy(rs->buffers[1].name, "audio_buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->buffers[1].readonly = 1;

    /* Three R8 plane textures — y_plane (full-res), u_plane and
     * v_plane (chroma 4:2:0 half-res). Each carries its own atlas
     * region uniform; the shader does the YUV→RGB matrix. Width/height
     * placeholders (1×1) are overwritten per-instance from the wire
     * video_w / video_h / chroma_w / chroma_h values before
     * binder->submit so atlas packing sees real dimensions. */
    strncpy(rs->textures[0].name, "y_plane", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->textures[0].wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->textures[0].format = WGPUTextureFormat_R8Unorm;
    rs->textures[0].sampler_filter = 1;
    rs->textures[0].width = 1;
    rs->textures[0].height = 1;
    rs->textures[0].data = NULL;
    strncpy(rs->textures[1].name, "u_plane", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->textures[1].wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->textures[1].format = WGPUTextureFormat_R8Unorm;
    rs->textures[1].sampler_filter = 1;
    rs->textures[1].width = 1;
    rs->textures[1].height = 1;
    rs->textures[1].data = NULL;
    strncpy(rs->textures[2].name, "v_plane", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->textures[2].wgsl_type, "texture_2d<f32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->textures[2].format = WGPUTextureFormat_R8Unorm;
    rs->textures[2].sampler_filter = 1;
    rs->textures[2].width = 1;
    rs->textures[2].height = 1;
    rs->textures[2].data = NULL;
    rs->texture_count = 3;
}

//=============================================================================
// Instance Rendering — uses self->resource_set + self->binder; the factory
// supplies only the shared pipeline + zoom state.
//=============================================================================

static struct yetty_ycore_void_result yvideo_instance_render(struct yetty_ydraw_figure *self,
                                                             struct yetty_ydraw_target *target,
                                                             float x, float y)
{
    if (!self || !self->buffer_data || !self->factory) {
        return YETTY_ERR(yetty_ycore_void, "invalid instance");
    }
    if (!self->resource_set || !self->binder) {
        return YETTY_ERR(yetty_ycore_void, "instance not finalised");
    }

    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self->factory);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "factory pipeline not initialized");
    }

    struct yetty_yrender_gpu_resource_set *rs = self->resource_set;

    // Parse wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    const uint32_t *data = (const uint32_t *)self->buffer_data;
    const uint32_t *payload = data + 2; // skip type_id and payload_size

    // Update uniforms from wire format (14 wire uniforms — v2 layout).
    rs->uniforms[0].f32 = *(float *)&payload[0];
    rs->uniforms[1].f32 = *(float *)&payload[1];
    rs->uniforms[2].f32 = *(float *)&payload[2];
    rs->uniforms[3].f32 = *(float *)&payload[3];
    rs->uniforms[4].u32 = payload[4];            /* video_w */
    rs->uniforms[5].u32 = payload[5];            /* video_h */
    rs->uniforms[6].u32 = payload[6];            /* chroma_w */
    rs->uniforms[7].u32 = payload[7];            /* chroma_h */
    rs->uniforms[8].f32 = *(float *)&payload[8]; /* fps */
    rs->uniforms[9].u32 = payload[9];            /* color_matrix */
    rs->uniforms[10].u32 = payload[10];          /* flags */
    rs->uniforms[11].u32 = payload[11];          /* audio_codec */
    rs->uniforms[12].u32 = payload[12];          /* audio_sample_rate */
    rs->uniforms[13].u32 = payload[13];          /* audio_channels */

    // Pull current zoom state from the factory into this instance's RS.
    rs->uniforms[14].f32 = factory->visual_zoom_scale > 0.0f ? factory->visual_zoom_scale : 1.0f;
    rs->uniforms[15].f32 = factory->visual_zoom_off_x;
    rs->uniforms[16].f32 = factory->visual_zoom_off_y;
    rs->uniforms[17].f32 = factory->cell_zoom_scale > 0.0f ? factory->cell_zoom_scale : 1.0f;
    rs->uniforms[18].f32 = factory->cell_zoom_off_x;
    rs->uniforms[19].f32 = factory->cell_zoom_off_y;

    // Visual-zoom viewport — read from the target every frame.
    rs->uniforms[20].f32 = target->viewport.w;
    rs->uniforms[21].f32 = target->viewport.h;

    // Override bounds_x / bounds_y with the caller-provided screen position
    // (wire bounds are the pre-scroll origin; x,y are the post-scroll pane
    // position the instance should render at).
    rs->uniforms[0].f32 = x;
    rs->uniforms[1].f32 = y;

    // Wire layout: 14 uniforms, then 2 buffer length fields, then both
    // buffer payloads in declaration order (nal_stream then audio_stream).
    size_t nal_words = payload[14];
    size_t audio_words = payload[15];
    const uint32_t *nal_payload = payload + 16;
    const uint32_t *audio_payload = nal_payload + nal_words;

    rs->buffers[0].data = (uint8_t *)nal_payload;
    rs->buffers[0].size = nal_words * sizeof(uint32_t);
    rs->buffers[0].dirty = 1;
    rs->buffers[1].data = (uint8_t *)audio_payload;
    rs->buffers[1].size = audio_words * sizeof(uint32_t);
    rs->buffers[1].dirty = 1;

    /* hook_instance_render_pre runs after the wire→RS uniform refresh
     * and before binder->update. The prim can write texture data,
     * set dirty flags, or otherwise mutate the RS using the freshly
     * decoded / state-derived inputs. */
    {
        struct yetty_ycore_void_result hrr = yvideo_hook_instance_render_pre(self, target, x, y);
        if (YETTY_IS_ERR(hrr)) {
            return YETTY_ERR(yetty_ycore_void, "yvideo: hook_render_pre failed", hrr);
        }
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

    /* The pane's render target may sit at a non-zero offset inside the
     * big surface (e.g. yui pushes the terminal pane down by the titlebar
     * height). The layer's simple-prim pass already draws to
     * (vp.x, vp.y, vp.w, vp.h); yvideo must use the same rect, otherwise
     * its fullscreen triangle covers a different region of the framebuffer
     * than the rest of the layer and the FS would compare canvas-local
     * bounds against the wrong coordinate system — see yvideo.wgsl
     * pane_pixel comment for the matching shader-side fix. */
    wgpuRenderPassEncoderSetViewport(pass, target->viewport.x, target->viewport.y,
                                     target->viewport.w, target->viewport.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)target->viewport.x,
                                        (uint32_t)target->viewport.y, (uint32_t)target->viewport.w,
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

    ydebug("yvideo_instance_render: rendered at (%.1f, %.1f) size (%.1f x %.1f) inst=%p", x, y, w,
           h, (void *)self);
    return YETTY_OK_VOID();
}

//=============================================================================
// Factory Implementation
//=============================================================================

static struct yetty_ycore_void_result yvideo_compile_pipeline(
    struct yetty_ydraw_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
    WGPUTextureFormat target_format, struct yetty_ydraw_gpu_allocator *allocator)
{
    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);

    if (factory->pipeline) {
        ydebug("yvideo: factory pipeline already initialized");
        return YETTY_OK_VOID();
    }

    factory->device = device;
    factory->queue = queue;
    factory->allocator = allocator;
    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    yvideo_populate_rs(&factory->template_rs);
    factory->template_initialized = 1;

    struct yetty_yrender_pipeline_ptr_result pr =
        yetty_yrender_pipeline_create(device, target_format, allocator, &factory->template_rs);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ycore_void, "yvideo pipeline_create failed", pr);
    }
    factory->pipeline = pr.value;

    yinfo("yvideo: pipeline compiled (shared across all instances)");
    return YETTY_OK_VOID();
}

static WGPURenderPipeline yvideo_get_pipeline(struct yetty_ydraw_concrete_factory *self)
{
    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);
    return factory->pipeline ? yetty_yrender_pipeline_get_pipeline(factory->pipeline) : NULL;
}

static struct yetty_ydraw_figure_ptr_result yvideo_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!buffer_data || size < sizeof(struct yetty_ydraw_composite)) {
        return YETTY_ERR(yetty_ydraw_figure_ptr, "invalid buffer data");
    }

    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);
    if (!factory->pipeline) {
        return YETTY_ERR(yetty_ydraw_figure_ptr, "yvideo factory pipeline not compiled");
    }

    struct yetty_ydraw_figure *instance = calloc(1, sizeof(struct yetty_ydraw_figure));
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
    instance->type = YETTY_YVIDEO_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = yvideo_instance_render;
    instance->ops = &yvideo_figure_ops;

    struct rectangle_result aabb_res = yetty_ydraw_composite_aabb(buffer_data);
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
        return YETTY_ERR(yetty_ydraw_figure_ptr, "rs alloc failed");
    }
    memcpy(instance->resource_set, &factory->template_rs,
           sizeof(struct yetty_yrender_gpu_resource_set));

    /* Wire the per-instance RS to this instance's payload. Storage
     * buffers (if any) point into the wire bytes; textures whose
     * pixels_buffer was diverted have their data + dimensions populated
     * here BEFORE binder->submit so the first finalize sees real
     * dimensions and atlas-packs accordingly. v2 wire layout: 14
     * uniform words, 2 buffer length fields, then payloads in order. */
    {
        const uint32_t *data = (const uint32_t *)instance->buffer_data;
        const uint32_t *payload = data + 2;
        size_t nal_words = payload[14];
        size_t audio_words = payload[15];
        const uint32_t *nal_payload = payload + 16;
        const uint32_t *audio_payload = nal_payload + nal_words;
        instance->resource_set->buffers[0].data = (uint8_t *)nal_payload;
        instance->resource_set->buffers[0].size = nal_words * sizeof(uint32_t);
        instance->resource_set->buffers[0].dirty = 1;
        instance->resource_set->buffers[1].data = (uint8_t *)audio_payload;
        instance->resource_set->buffers[1].size = audio_words * sizeof(uint32_t);
        instance->resource_set->buffers[1].dirty = 1;
    }

    /* hook_instance_create runs after RS clone + wire wiring, before
     * binder->submit. Lets the prim populate instance_data and set
     * per-instance texture dimensions before atlas pack. */
    {
        struct yetty_ycore_void_result hcr =
            yvideo_hook_instance_create(instance, buffer_data, size);
        if (YETTY_IS_ERR(hcr)) {
            free(instance->resource_set);
            free(instance->buffer_data);
            free(instance);
            return YETTY_ERR(yetty_ydraw_figure_ptr, "yvideo: hook_instance_create failed", hcr);
        }
    }

    /* Per-instance binder bound to the factory's shared pipeline. Owns
     * its OWN uniform_buffer / storage_buffer / bind_group. */
    struct yetty_yrender_gpu_resource_binder_result br =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(
            factory->device, factory->queue, factory->allocator, factory->pipeline);
    if (YETTY_IS_ERR(br)) {
        yvideo_hook_instance_destroy(instance);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "instance binder create failed", br);
    }
    instance->binder = br.value;

    struct yetty_ycore_void_result sr =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(sr)) {
        instance->binder->ops->destroy(instance->binder);
        yvideo_hook_instance_destroy(instance);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "binder submit failed", sr);
    }

    struct yetty_ycore_void_result fr = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(fr)) {
        instance->binder->ops->destroy(instance->binder);
        yvideo_hook_instance_destroy(instance);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_figure_ptr, "binder finalize failed", fr);
    }

    return YETTY_OK(yetty_ydraw_figure_ptr, instance);
}

static void yvideo_instance_destroy(struct yetty_ydraw_figure *instance)
{
    if (!instance) {
        return;
    }
    yvideo_hook_instance_destroy(instance);
    if (instance->binder) {
        instance->binder->ops->destroy(instance->binder);
    }
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}

/* Vtable installed on every yvideo figure_instance at create time. */
static const struct yetty_ydraw_figure_ops yvideo_figure_ops = {
    .destroy = yvideo_instance_destroy,
    .update = yvideo_instance_update,
};

/* Legacy factory adapter — kept so the abstract factory's
 * destroy_instance slot still resolves. yetty_ydraw_figure_destroy
 * now routes through fi->ops->destroy directly; this is reached only
 * from the few call sites that still go through the factory path. */
static void yvideo_destroy_instance(struct yetty_ydraw_concrete_factory *self,
                                    struct yetty_ydraw_figure *instance)
{
    (void)self;
    yvideo_instance_destroy(instance);
}

static struct yetty_yrender_gpu_resource_set *yvideo_get_shared_rs(
    struct yetty_ydraw_concrete_factory *self)
{
    /* Returns the structural template, NOT a mutable per-instance RS. */
    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);
    return factory->template_initialized ? &factory->template_rs : NULL;
}

static struct yetty_ycore_void_result yvideo_set_visual_zoom(
    struct yetty_ydraw_concrete_factory *self, float scale, float off_x, float off_y)
{
    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);
    factory->visual_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->visual_zoom_off_x = off_x;
    factory->visual_zoom_off_y = off_y;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yvideo_set_cell_zoom(
    struct yetty_ydraw_concrete_factory *self, float scale, float off_x, float off_y)
{
    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);
    factory->cell_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->cell_zoom_off_x = off_x;
    factory->cell_zoom_off_y = off_y;
    ydebug("yvideo_set_cell_zoom: scale=%.3f off=(%.1f,%.1f)", scale, off_x, off_y);
    return YETTY_OK_VOID();
}

struct yetty_ydraw_concrete_factory *yetty_yvideo_factory_create(void)
{
    struct yetty_yvideo_factory *factory = calloc(1, sizeof(struct yetty_yvideo_factory));
    if (!factory) {
        return NULL;
    }

    factory->base.type_id = YETTY_YVIDEO_TYPE_ID;
    factory->base.compile_pipeline = yvideo_compile_pipeline;
    factory->base.get_pipeline = yvideo_get_pipeline;
    factory->base.create_instance = yvideo_create_instance;
    factory->base.destroy_instance = yvideo_destroy_instance;
    factory->base.update_instance = yvideo_update_dispatch;
    factory->base.get_shared_rs = yvideo_get_shared_rs;
    factory->base.set_visual_zoom = yvideo_set_visual_zoom;
    factory->base.set_cell_zoom = yvideo_set_cell_zoom;

    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    return &factory->base;
}

void yetty_yvideo_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    if (!self) {
        return;
    }

    struct yetty_yvideo_factory *factory = yetty_yvideo_factory_from_base(self);

    if (factory->pipeline) {
        yetty_yrender_pipeline_destroy(factory->pipeline);
    }
    free(factory);
}
