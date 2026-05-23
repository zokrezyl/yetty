/*
 * figure.c — yrdawn rendered as a yfigure_figure subclass.
 *
 * Pipeline + frame-texture lifecycle + OSC HELLO/CMD/BULK/BYE handling
 * + handle table are all the same shape as the old yrdawn terminal
 * layer (src/yetty/yterm/yrdawn-layer.c). The only structural change is
 * what surrounds them:
 *
 *   - base struct embed: yfigure_figure (not yrender_terminal_layer)
 *   - ops vtable: {destroy, render} only — no is_dirty/is_empty/
 *     resize_grid/get_gpu_resource_set/on_key/on_char
 *   - render placement: self->rect (absolute target pixel space)
 *     instead of target->viewport. This is what lets the figure occupy
 *     either the whole host surface OR an arbitrary sub-rect.
 *   - OSC dispatch is one-shot per envelope (handle_osc), not a long-
 *     lived coroutine. The host (ycompositor::process_input) drives
 *     envelope assembly; the figure decodes the assembled payload.
 *
 * The server-side callbacks the codegen dispatcher needs
 * (yrdawn_server_handle_get/set/release, yrdawn_server_get_shared_*,
 * yrdawn_server_emit_reply, yrdawn_server_bulk_take, yrdawn_server_set_frame,
 * plus yrdawn_arena_alloc/free) are exposed here unchanged in signature.
 * Linking this translation unit against the same binary as
 * yrdawn-layer.c will give multiple-definition errors — pick one.
 */
#include <yetty/yrdawn/figure.h>

#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yrdawn/server.h>
#include <yetty/yrdawn/wire.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Per-handle / per-bulk slot
 *=========================================================================*/

struct yrdawn_handle_entry {
    uint64_t handle;
    void *ptr;     /* WGPU* object — filled in by codegen dispatch */
    uint32_t kind; /* codegen-assigned WGPUObjectKind */
};

struct yrdawn_bulk_entry {
    uint32_t ref;        /* 0 = free slot */
    uint32_t next_seq;   /* expected next chunk index */
    struct yetty_ycore_buffer buf;
};

/*===========================================================================
 * Figure struct
 *=========================================================================*/

struct yetty_yrdawn_figure {
    struct yetty_yfigure_figure base;

    /* Shared Dawn handles borrowed from context. The bridge shares
     * yetty's Dawn instance/adapter/device/queue — a second in-process
     * Dawn instance deadlocks Vulkan during pipeline creation. */
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;

    /* Pipeline + per-frame texture. Pipeline samples a single texture
     * across a fullscreen quad fabricated in the vertex shader from
     * gl_VertexIndex — no vertex buffer, no uniforms. */
    int pipeline_ready;
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;

    WGPUTexture frame_texture;
    WGPUTextureView frame_view;
    WGPUBindGroup frame_bind_group;

    /* OSC session state. */
    int connected;
    int disconnected;
    int has_frame;       /* set on first set_frame */

    /* Outbound emit callback wired by the host. */
    yetty_yrdawn_emit_osc_fn emit_osc_fn;
    void *emit_osc_user;

    /* Optional repaint nudge. */
    yetty_yrdawn_request_render_fn request_render_fn;
    void *request_render_user;

    /* Handle table — backs the yrdawn_server_handle_* callbacks the
     * codegen dispatcher calls. Linear scan, first cut. */
    struct yrdawn_handle_entry *handles;
    size_t handle_count;
    size_t handle_cap;

    /* BULK reassembly. */
    struct yrdawn_bulk_entry *bulks;
    size_t bulk_count;
    size_t bulk_cap;
};

/*===========================================================================
 * Inline WGSL — fullscreen textured quad, no vertex buffer, no uniforms.
 *=========================================================================*/

static const char *yrdawn_figure_wgsl(void)
{
    static const char src[] =
        "@group(0) @binding(0) var yrdawn_samp: sampler;\n"
        "@group(0) @binding(1) var yrdawn_tex: texture_2d<f32>;\n"
        "\n"
        "struct VSOut {\n"
        "    @builtin(position) pos: vec4f,\n"
        "    @location(0) uv: vec2f,\n"
        "}\n"
        "\n"
        "@vertex\n"
        "fn vs_main(@builtin(vertex_index) vid: u32) -> VSOut {\n"
        "    var positions = array<vec2f, 6>(\n"
        "        vec2f(-1.0, -1.0),\n"
        "        vec2f( 1.0, -1.0),\n"
        "        vec2f(-1.0,  1.0),\n"
        "        vec2f(-1.0,  1.0),\n"
        "        vec2f( 1.0, -1.0),\n"
        "        vec2f( 1.0,  1.0)\n"
        "    );\n"
        "    var uvs = array<vec2f, 6>(\n"
        "        vec2f(0.0, 1.0),\n"
        "        vec2f(1.0, 1.0),\n"
        "        vec2f(0.0, 0.0),\n"
        "        vec2f(0.0, 0.0),\n"
        "        vec2f(1.0, 1.0),\n"
        "        vec2f(1.0, 0.0)\n"
        "    );\n"
        "    var out: VSOut;\n"
        "    out.pos = vec4f(positions[vid], 0.0, 1.0);\n"
        "    out.uv = uvs[vid];\n"
        "    return out;\n"
        "}\n"
        "\n"
        "@fragment\n"
        "fn fs_main(in: VSOut) -> @location(0) vec4f {\n"
        "    return textureSample(yrdawn_tex, yrdawn_samp, in.uv);\n"
        "}\n";
    return src;
}

/*===========================================================================
 * GPU pipeline build / teardown — identical to the layer's.
 *=========================================================================*/

static int build_pipeline(struct yetty_yrdawn_figure *f)
{
    const char *wgsl_src = yrdawn_figure_wgsl();
    size_t wgsl_len = strlen(wgsl_src);

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){wgsl_src, wgsl_len};

    WGPUShaderModuleDescriptor sm_desc = {0};
    sm_desc.nextInChain = &wgsl.chain;
    f->shader_module = wgpuDeviceCreateShaderModule(f->device, &sm_desc);
    if (!f->shader_module)
        return 0;

    WGPUBindGroupLayoutEntry bgl_entries[2] = {0};
    bgl_entries[0].binding = 0;
    bgl_entries[0].visibility = WGPUShaderStage_Fragment;
    bgl_entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
    bgl_entries[1].binding = 1;
    bgl_entries[1].visibility = WGPUShaderStage_Fragment;
    bgl_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bgl_entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 2;
    bgl_desc.entries = bgl_entries;
    f->bind_group_layout = wgpuDeviceCreateBindGroupLayout(f->device, &bgl_desc);
    if (!f->bind_group_layout)
        return 0;

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &f->bind_group_layout;
    f->pipeline_layout = wgpuDeviceCreatePipelineLayout(f->device, &pl_desc);
    if (!f->pipeline_layout)
        return 0;

    WGPUBlendComponent blend_color = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_SrcAlpha,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
    };
    WGPUBlendComponent blend_alpha = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
    };
    WGPUBlendState blend = {.color = blend_color, .alpha = blend_alpha};

    WGPUColorTargetState color_target = {0};
    color_target.format = f->target_format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {0};
    fs.module = f->shader_module;
    fs.entryPoint = (WGPUStringView){"fs_main", 7};
    fs.targetCount = 1;
    fs.targets = &color_target;

    WGPURenderPipelineDescriptor rpd = {0};
    rpd.layout = f->pipeline_layout;
    rpd.vertex.module = f->shader_module;
    rpd.vertex.entryPoint = (WGPUStringView){"vs_main", 7};
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode = WGPUCullMode_None;
    rpd.fragment = &fs;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xFFFFFFFFu;

    f->pipeline = wgpuDeviceCreateRenderPipeline(f->device, &rpd);
    if (!f->pipeline)
        return 0;

    WGPUSamplerDescriptor sd = {0};
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.lodMinClamp = 0.0f;
    sd.lodMaxClamp = 0.0f;
    sd.maxAnisotropy = 1;
    f->sampler = wgpuDeviceCreateSampler(f->device, &sd);
    if (!f->sampler)
        return 0;

    f->pipeline_ready = 1;
    return 1;
}

static int build_frame_resources(struct yetty_yrdawn_figure *f,
                                 uint32_t width, uint32_t height)
{
    WGPUTextureDescriptor td = {0};
    td.size = (WGPUExtent3D){width, height, 1};
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture tex = wgpuDeviceCreateTexture(f->device, &td);
    if (!tex)
        return 0;

    WGPUTextureViewDescriptor tvd = {0};
    tvd.format = WGPUTextureFormat_RGBA8Unorm;
    tvd.dimension = WGPUTextureViewDimension_2D;
    tvd.baseMipLevel = 0;
    tvd.mipLevelCount = 1;
    tvd.baseArrayLayer = 0;
    tvd.arrayLayerCount = 1;
    tvd.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(tex, &tvd);
    if (!view) {
        wgpuTextureDestroy(tex);
        wgpuTextureRelease(tex);
        return 0;
    }

    WGPUBindGroupEntry bge[2] = {0};
    bge[0].binding = 0;
    bge[0].sampler = f->sampler;
    bge[1].binding = 1;
    bge[1].textureView = view;

    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = f->bind_group_layout;
    bgd.entryCount = 2;
    bgd.entries = bge;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(f->device, &bgd);
    if (!bg) {
        wgpuTextureViewRelease(view);
        wgpuTextureDestroy(tex);
        wgpuTextureRelease(tex);
        return 0;
    }

    if (f->frame_bind_group) wgpuBindGroupRelease(f->frame_bind_group);
    if (f->frame_view) wgpuTextureViewRelease(f->frame_view);
    if (f->frame_texture) {
        wgpuTextureDestroy(f->frame_texture);
        wgpuTextureRelease(f->frame_texture);
    }
    f->frame_texture = tex;
    f->frame_view = view;
    f->frame_bind_group = bg;
    return 1;
}

static void release_gpu(struct yetty_yrdawn_figure *f)
{
    if (f->frame_bind_group) wgpuBindGroupRelease(f->frame_bind_group);
    if (f->frame_view) wgpuTextureViewRelease(f->frame_view);
    if (f->frame_texture) {
        wgpuTextureDestroy(f->frame_texture);
        wgpuTextureRelease(f->frame_texture);
    }
    if (f->sampler) wgpuSamplerRelease(f->sampler);
    if (f->pipeline) wgpuRenderPipelineRelease(f->pipeline);
    if (f->pipeline_layout) wgpuPipelineLayoutRelease(f->pipeline_layout);
    if (f->bind_group_layout) wgpuBindGroupLayoutRelease(f->bind_group_layout);
    if (f->shader_module) wgpuShaderModuleRelease(f->shader_module);
    f->frame_bind_group = NULL;
    f->frame_view = NULL;
    f->frame_texture = NULL;
    f->sampler = NULL;
    f->pipeline = NULL;
    f->pipeline_layout = NULL;
    f->bind_group_layout = NULL;
    f->shader_module = NULL;
    f->pipeline_ready = 0;
}

/*===========================================================================
 * Outbound emit helper
 *=========================================================================*/

static struct yetty_ycore_void_result emit(struct yetty_yrdawn_figure *f, int osc_code,
                                           const void *payload, size_t len)
{
    if (!f->emit_osc_fn) {
        ywarn("yrdawn-figure: no emit_osc_fn wired, dropping OSC %d", osc_code);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r =
        f->emit_osc_fn(osc_code, payload, len, f->emit_osc_user);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yrdawn-figure: emit_osc_fn");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Inbound handlers — same shape as the layer's.
 *=========================================================================*/

static struct yetty_ycore_void_result handle_hello(struct yetty_yrdawn_figure *f,
                                                   const uint8_t *payload, size_t len)
{
    if (len < sizeof(struct yetty_yrdawn_wire_hello))
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: HELLO truncated");
    const struct yetty_yrdawn_wire_hello *h =
        (const struct yetty_yrdawn_wire_hello *)payload;
    if (h->magic != YETTY_YRDAWN_MAGIC_HELLO)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: HELLO bad magic");

    struct yetty_yrdawn_wire_hello_ack ack = {
        .magic = YETTY_YRDAWN_MAGIC_HELLO_ACK,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(ack),
        .status = (h->version == YETTY_YRDAWN_WIRE_VERSION)
            ? YETTY_YRDAWN_HELLO_OK : YETTY_YRDAWN_HELLO_REJECTED,
    };

    struct yetty_ycore_void_result e =
        emit(f, YETTY_YRDAWN_OSC_SC_HELLO_ACK, &ack, sizeof(ack));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, e, "yrdawn-figure: HELLO_ACK");

    if (ack.status == YETTY_YRDAWN_HELLO_OK)
        f->connected = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_cmd(struct yetty_yrdawn_figure *f,
                                                 const uint8_t *payload, size_t len)
{
    if (len < sizeof(struct yetty_yrdawn_wire_cmd_hdr))
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: CMD truncated");
    const struct yetty_yrdawn_wire_cmd_hdr *hdr =
        (const struct yetty_yrdawn_wire_cmd_hdr *)payload;
    if (hdr->magic != YETTY_YRDAWN_MAGIC_CMD)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: CMD bad magic");
    if (!f->connected)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: CMD before HELLO_ACK");

    const uint8_t *args_body = payload + sizeof(*hdr);
    size_t args_body_len = len - sizeof(*hdr);
    ydebug("yrdawn-figure: dispatching method_id=%u req_id=%u body=%zu",
           hdr->method_id, hdr->req_id, args_body_len);
    uint32_t status = yrdawn_server_dispatch(f, hdr->method_id, hdr->req_id,
                                            args_body, args_body_len);
    ydebug("yrdawn-figure: dispatch status=%u method_id=%u", status, hdr->method_id);

    if (status == YRDAWN_DISPATCH_DEFERRED)
        return YETTY_OK_VOID();
    if (hdr->req_id == 0)
        return YETTY_OK_VOID();

    struct yetty_yrdawn_wire_reply_hdr reply = {
        .magic = YETTY_YRDAWN_MAGIC_REPLY,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(reply),
        .req_id = hdr->req_id,
        .method_id = hdr->method_id,
        .kind = YETTY_YRDAWN_REPLY_ASYNC,
        .status = status,
        .payload_ref = 0,
    };
    return emit(f, YETTY_YRDAWN_OSC_SC_REPLY, &reply, sizeof(reply));
}

static struct yrdawn_bulk_entry *bulk_find_or_alloc(struct yetty_yrdawn_figure *f,
                                                    uint32_t ref)
{
    for (size_t i = 0; i < f->bulk_count; ++i) {
        if (f->bulks[i].ref == ref)
            return &f->bulks[i];
    }
    for (size_t i = 0; i < f->bulk_count; ++i) {
        if (f->bulks[i].ref == 0) {
            f->bulks[i].ref = ref;
            f->bulks[i].next_seq = 0;
            yetty_ycore_buffer_clear(&f->bulks[i].buf);
            return &f->bulks[i];
        }
    }
    if (f->bulk_count == f->bulk_cap) {
        size_t cap = f->bulk_cap ? f->bulk_cap * 2u : 4u;
        struct yrdawn_bulk_entry *grown =
            (struct yrdawn_bulk_entry *)realloc(f->bulks, cap * sizeof(*grown));
        if (!grown)
            return NULL;
        memset(grown + f->bulk_count, 0, (cap - f->bulk_count) * sizeof(*grown));
        f->bulks = grown;
        f->bulk_cap = cap;
    }
    struct yrdawn_bulk_entry *e = &f->bulks[f->bulk_count++];
    e->ref = ref;
    e->next_seq = 0;
    e->buf = (struct yetty_ycore_buffer){0};
    return e;
}

static void bulk_release(struct yetty_yrdawn_figure *f, uint32_t ref)
{
    for (size_t i = 0; i < f->bulk_count; ++i) {
        if (f->bulks[i].ref == ref) {
            yetty_ycore_buffer_destroy(&f->bulks[i].buf);
            f->bulks[i] = (struct yrdawn_bulk_entry){0};
            return;
        }
    }
}

static struct yetty_ycore_void_result handle_bulk(struct yetty_yrdawn_figure *f,
                                                  const uint8_t *payload, size_t len)
{
    if (len < sizeof(struct yetty_yrdawn_wire_bulk_hdr))
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK truncated");
    const struct yetty_yrdawn_wire_bulk_hdr *hdr =
        (const struct yetty_yrdawn_wire_bulk_hdr *)payload;
    if (hdr->magic != YETTY_YRDAWN_MAGIC_BULK)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK bad magic");
    if (hdr->ref == 0)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK ref=0");
    if (len < sizeof(*hdr) + hdr->chunk_size)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK chunk_size > payload");

    struct yrdawn_bulk_entry *e = bulk_find_or_alloc(f, hdr->ref);
    if (!e)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK table oom");
    if (hdr->seq != e->next_seq) {
        bulk_release(f, hdr->ref);
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK out-of-order seq");
    }

    if (hdr->chunk_size) {
        struct yetty_ycore_void_result w =
            yetty_ycore_buffer_write(&e->buf, payload + sizeof(*hdr), hdr->chunk_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, w, "yrdawn-figure: BULK accum");
    }
    e->next_seq++;

    if (hdr->flags & YETTY_YRDAWN_BULK_FLAG_LAST) {
        /* Park next_seq at UINT32_MAX so the dispatcher can find this
         * ref via bulk_take and further chunks are rejected. */
        e->next_seq = UINT32_MAX;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_bye(struct yetty_yrdawn_figure *f,
                                                 const uint8_t *payload, size_t len)
{
    (void)payload;
    (void)len;
    f->disconnected = 1;
    f->connected = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Per-dispatch arena used by codegen-emitted descriptor decoders.
 *=========================================================================*/

void *yrdawn_arena_alloc(struct yrdawn_arena *a, size_t bytes)
{
    if (!a || bytes == 0)
        return NULL;
    if (a->count == a->cap) {
        size_t cap = a->cap ? a->cap * 2u : 8u;
        void **grown = (void **)realloc(a->chunks, cap * sizeof(*grown));
        if (!grown)
            return NULL;
        a->chunks = grown;
        a->cap = cap;
    }
    void *p = calloc(1u, bytes);
    if (!p)
        return NULL;
    a->chunks[a->count++] = p;
    return p;
}

void yrdawn_arena_free(struct yrdawn_arena *a)
{
    if (!a)
        return;
    for (size_t i = 0; i < a->count; ++i)
        free(a->chunks[i]);
    free(a->chunks);
    a->chunks = NULL;
    a->count = 0;
    a->cap = 0;
}

/*===========================================================================
 * Server callbacks called by server-dispatch.gen.c. `ctx` is the figure.
 *=========================================================================*/

void *yrdawn_server_get_shared_instance(void *ctx)
{
    return ((struct yetty_yrdawn_figure *)ctx)->instance;
}
void *yrdawn_server_get_shared_adapter(void *ctx)
{
    return ((struct yetty_yrdawn_figure *)ctx)->adapter;
}
void *yrdawn_server_get_shared_device(void *ctx)
{
    return ((struct yetty_yrdawn_figure *)ctx)->device;
}
void *yrdawn_server_get_shared_queue(void *ctx)
{
    return ((struct yetty_yrdawn_figure *)ctx)->queue;
}

static struct yrdawn_handle_entry *handle_find(struct yetty_yrdawn_figure *f,
                                               uint64_t handle)
{
    for (size_t i = 0; i < f->handle_count; ++i) {
        if (f->handles[i].handle == handle)
            return &f->handles[i];
    }
    return NULL;
}

void *yrdawn_server_handle_get(void *ctx, uint64_t handle)
{
    if (!ctx || handle == YETTY_YRDAWN_HANDLE_NULL)
        return NULL;
    struct yrdawn_handle_entry *e =
        handle_find((struct yetty_yrdawn_figure *)ctx, handle);
    return e ? e->ptr : NULL;
}

struct yetty_ycore_void_result yrdawn_server_handle_set(void *ctx, uint64_t handle, void *ptr)
{
    if (!ctx || handle == YETTY_YRDAWN_HANDLE_NULL)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_handle_set: bad ctx/handle");
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)ctx;
    struct yrdawn_handle_entry *e = handle_find(f, handle);
    if (e) {
        e->ptr = ptr;
        return YETTY_OK_VOID();
    }
    if (f->handle_count == f->handle_cap) {
        size_t cap = f->handle_cap ? f->handle_cap * 2u : 16u;
        struct yrdawn_handle_entry *grown =
            (struct yrdawn_handle_entry *)realloc(f->handles, cap * sizeof(*grown));
        if (!grown)
            return YETTY_ERR(yetty_ycore_void, "yrdawn_handle_set: handle table oom");
        f->handles = grown;
        f->handle_cap = cap;
    }
    f->handles[f->handle_count++] = (struct yrdawn_handle_entry){
        .handle = handle, .ptr = ptr, .kind = 0,
    };
    return YETTY_OK_VOID();
}

void yrdawn_server_handle_release(void *ctx, uint64_t handle)
{
    if (!ctx)
        return;
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)ctx;
    for (size_t i = 0; i < f->handle_count; ++i) {
        if (f->handles[i].handle == handle) {
            f->handles[i] = f->handles[--f->handle_count];
            return;
        }
    }
}

int yrdawn_server_bulk_take(void *ctx, uint32_t ref, struct yetty_ycore_buffer *out)
{
    if (!ctx || !out)
        return 0;
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)ctx;
    for (size_t i = 0; i < f->bulk_count; ++i) {
        if (f->bulks[i].ref == ref && f->bulks[i].next_seq == UINT32_MAX) {
            *out = f->bulks[i].buf;
            f->bulks[i] = (struct yrdawn_bulk_entry){0};
            return 1;
        }
    }
    return 0;
}

struct yetty_ycore_void_result yrdawn_server_emit_reply(
    void *ctx, uint32_t req_id, uint32_t method_id, uint32_t status,
    const void *payload, size_t payload_len)
{
    if (!ctx)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_server_emit_reply: NULL ctx");
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)ctx;
    struct yetty_yrdawn_wire_reply_hdr reply = {
        .magic = YETTY_YRDAWN_MAGIC_REPLY,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)(sizeof(reply) + payload_len),
        .req_id = req_id,
        .method_id = method_id,
        .kind = YETTY_YRDAWN_REPLY_ASYNC,
        .status = status,
        .payload_ref = 0,
    };
    if (payload_len == 0 || !payload)
        return emit(f, YETTY_YRDAWN_OSC_SC_REPLY, &reply, sizeof(reply));
    size_t total = sizeof(reply) + payload_len;
    uint8_t *frame = (uint8_t *)malloc(total);
    if (!frame)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_server_emit_reply: oom");
    memcpy(frame, &reply, sizeof(reply));
    memcpy(frame + sizeof(reply), payload, payload_len);
    struct yetty_ycore_void_result r =
        emit(f, YETTY_YRDAWN_OSC_SC_REPLY, frame, total);
    free(frame);
    return r;
}

struct yetty_ycore_void_result yrdawn_server_set_frame(
    void *ctx, uint32_t width, uint32_t height,
    const uint8_t *pixels, size_t bytes)
{
    if (!ctx || !pixels)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: NULL ctx or pixels");
    if (width == 0 || height == 0)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: zero dim");
    size_t want = (size_t)width * (size_t)height * 4u;
    if (bytes != want)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: byte count != w*h*4");

    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)ctx;
    if (!f->frame_texture ||
        wgpuTextureGetWidth(f->frame_texture) != width ||
        wgpuTextureGetHeight(f->frame_texture) != height) {
        if (!build_frame_resources(f, width, height))
            return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: rebuild texture failed");
    }

    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = f->frame_texture;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {0};
    layout.bytesPerRow = width * 4u;
    layout.rowsPerImage = height;
    WGPUExtent3D extent = {width, height, 1};
    wgpuQueueWriteTexture(f->queue, &dst, pixels, bytes, &layout, &extent);

    ydebug("yrdawn-figure: set_frame %ux%u %zu bytes -> texture updated",
           width, height, bytes);

    f->has_frame = 1;
    f->base.dirty = 1;
    if (f->request_render_fn) {
        struct yetty_ycore_void_result rr =
            f->request_render_fn(f->request_render_user);
        if (YETTY_IS_ERR(rr))
            yetty_ycore_error_destroy(rr.error);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure ops — destroy + render
 *=========================================================================*/

static struct yetty_ycore_void_result yrdawn_figure_destroy(
    struct yetty_yfigure_figure *self)
{
    if (!self)
        return YETTY_OK_VOID();
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)self;
    release_gpu(f);
    for (size_t i = 0; i < f->bulk_count; ++i)
        yetty_ycore_buffer_destroy(&f->bulks[i].buf);
    free(f->bulks);
    free(f->handles);
    free(f);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yrdawn_figure_render(
    struct yetty_yfigure_figure *self, struct yetty_ydraw_target *target)
{
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)self;
    if (!f->pipeline_ready || !f->frame_bind_group || !f->has_frame)
        return YETTY_OK_VOID();

    WGPUTextureView view = target->ops->get_view(target);
    if (!view)
        return YETTY_OK_VOID();

    float x = self->rect.min.x;
    float y = self->rect.min.y;
    float w = self->rect.max.x - self->rect.min.x;
    float h = self->rect.max.y - self->rect.min.y;
    if (w <= 0.0f || h <= 0.0f)
        return YETTY_OK_VOID();

    ydebug("yrdawn-figure: render rect=(%.0f,%.0f,%.0fx%.0f)", x, y, w, h);

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(f->device, &enc_desc);
    if (!enc)
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: create encoder");

    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, f->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, f->frame_bind_group, 0, NULL);

    /* Placement: the figure's own rect (absolute target pixel space).
     * This is where "figure can be full screen or a viewport in the
     * host's surface" lives — set_figure_rect on the compositor moves
     * us; nothing else about render changes. */
    wgpuRenderPassEncoderSetViewport(pass, x, y, w, h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)x, (uint32_t)y,
                                        (uint32_t)w, (uint32_t)h);

    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cb_desc);
    wgpuQueueSubmit(f->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_create(
    struct yetty_ycore_rectangle rect,
    const struct yetty_context *context)
{
    if (!context)
        return YETTY_ERR(yetty_yrdawn_figure_ptr,
                         "yrdawn_figure_create: NULL context");

    struct yetty_yrdawn_figure *f =
        (struct yetty_yrdawn_figure *)calloc(1, sizeof(*f));
    if (!f)
        return YETTY_ERR(yetty_yrdawn_figure_ptr, "yrdawn_figure_create: oom");

    static const struct yetty_yfigure_figure_ops ops = {
        .destroy = yrdawn_figure_destroy,
        .render = yrdawn_figure_render,
    };
    f->base.ops = &ops;
    f->base.rect = rect;
    f->base.dirty = 1;

    f->instance = context->runtime->gpu.app_gpu_context.instance;
    f->adapter = context->runtime->gpu.adapter;
    f->device = context->runtime->gpu.device;
    f->queue = context->runtime->gpu.queue;
    f->target_format = context->runtime->gpu.surface_format;

    if (!build_pipeline(f)) {
        release_gpu(f);
        free(f);
        return YETTY_ERR(yetty_yrdawn_figure_ptr,
                         "yrdawn_figure_create: pipeline build failed");
    }
    /* No placeholder texture: the figure renders nothing until the
     * first set_frame call builds frame resources at the real size. */
    return YETTY_OK(yetty_yrdawn_figure_ptr, f);
}

struct yetty_yfigure_figure *yetty_yrdawn_figure_as_figure(
    struct yetty_yrdawn_figure *f)
{
    return &f->base;
}

void yetty_yrdawn_figure_set_emit_osc(
    struct yetty_yrdawn_figure *f,
    yetty_yrdawn_emit_osc_fn fn, void *user)
{
    if (!f) return;
    f->emit_osc_fn = fn;
    f->emit_osc_user = user;
}

void yetty_yrdawn_figure_set_request_render(
    struct yetty_yrdawn_figure *f,
    yetty_yrdawn_request_render_fn fn, void *user)
{
    if (!f) return;
    f->request_render_fn = fn;
    f->request_render_user = user;
}

struct yetty_ycore_void_result yetty_yrdawn_figure_handle_osc(
    struct yetty_yrdawn_figure *f,
    int osc_code, const uint8_t *payload, size_t payload_len)
{
    if (!f)
        return YETTY_ERR(yetty_ycore_void, "yrdawn_figure_handle_osc: NULL figure");
    switch (osc_code) {
    case YETTY_YRDAWN_OSC_CS_HELLO:
        return handle_hello(f, payload, payload_len);
    case YETTY_YRDAWN_OSC_CS_CMD:
        return handle_cmd(f, payload, payload_len);
    case YETTY_YRDAWN_OSC_CS_BULK:
        return handle_bulk(f, payload, payload_len);
    case YETTY_YRDAWN_OSC_CS_BYE:
        return handle_bye(f, payload, payload_len);
    default:
        ywarn("yrdawn-figure: unknown OSC code %d", osc_code);
        return YETTY_OK_VOID();
    }
}
