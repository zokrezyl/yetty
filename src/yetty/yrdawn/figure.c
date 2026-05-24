/*
 * figure.c — yrdawn rendered as a yfigure_figure subclass.
 *
 * One figure == one remote canvas the wasm client opened. The figure
 * holds only per-canvas state (figure_id, pipeline, frame texture);
 * everything shared between canvases owned by the same client (WGPU
 * handle table, BULK reassembly slots, the borrowed Dawn handles)
 * lives on the session — see session.{h,c}.
 *
 * Wire entry:
 *   - CREATE_CHILD's init_payload arrives at process_bytes carrying
 *     a u32 SUB_HELLO + struct yetty_yrdawn_wire_hello. We bind the
 *     figure to its session (lazy-creating sessions as needed) and
 *     dispatch HELLO_ACK.
 *   - Subsequent records hit process_input, which reads u32 SUB_OP +
 *     wire struct and dispatches CMD / BULK / BYE.
 *
 * Server-side callbacks (yrdawn_server_handle_*, get_shared_*,
 * bulk_take, emit_reply, set_frame, arena_alloc/free) receive
 * ctx = figure*. Each one navigates figure->session for shared
 * state and uses figure->frame_texture for set_frame. The codegen
 * dispatcher needs no changes — its ctx is opaque.
 */
#include <yetty/yrdawn/figure.h>

#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yrdawn/server.h>
#include <yetty/yrdawn/session.h>
#include <yetty/yrdawn/wire.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

/*===========================================================================
 * Figure struct
 *=========================================================================*/

struct yetty_yrdawn_figure {
    struct yetty_yfigure_figure base;

    /* Set by SUB_HELLO. Until then the figure rejects CMD/BULK/BYE. */
    uint32_t figure_id;
    int connected;
    int disconnected;

    /* Borrowed. Bound by SUB_HELLO via the factory_args session table. */
    struct yetty_yrdawn_session *session;

    /* Borrowed. Source of outbound emit + per-host repaint nudge. */
    struct yetty_yrdawn_factory_args *args;

    /* Pipeline + per-frame texture. Pipeline samples a single texture
     * across a fullscreen quad fabricated in the vertex shader from
     * gl_VertexIndex — no vertex buffer, no uniforms. */
    int pipeline_ready;
    int has_frame; /* set on first set_frame */
    WGPUTextureFormat target_format;
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;
    WGPUTexture frame_texture;
    WGPUTextureView frame_view;
    WGPUBindGroup frame_bind_group;
};

/*===========================================================================
 * Factory state — opaque from the header
 *=========================================================================*/

struct session_entry {
    uint32_t session_id;
    struct yetty_yrdawn_session *session;
};

struct yetty_yrdawn_factory_state {
    struct session_entry *sessions;
    size_t count;
    size_t cap;
};

static struct yetty_yrdawn_session *state_find_session(struct yetty_yrdawn_factory_state *st,
                                                       uint32_t session_id)
{
    if (!st) {
        return NULL;
    }
    for (size_t i = 0; i < st->count; ++i) {
        if (st->sessions[i].session_id == session_id) {
            return st->sessions[i].session;
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result state_insert_session(struct yetty_yrdawn_factory_state *st,
                                                           uint32_t session_id,
                                                           struct yetty_yrdawn_session *s)
{
    if (st->count == st->cap) {
        size_t cap = st->cap ? st->cap * 2u : 4u;
        struct session_entry *grown = realloc(st->sessions, cap * sizeof(*grown));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yrdawn factory: session table oom");
        }
        st->sessions = grown;
        st->cap = cap;
    }
    st->sessions[st->count++] = (struct session_entry){.session_id = session_id, .session = s};
    return YETTY_OK_VOID();
}

static void state_remove_session(struct yetty_yrdawn_factory_state *st, uint32_t session_id)
{
    if (!st) {
        return;
    }
    for (size_t i = 0; i < st->count; ++i) {
        if (st->sessions[i].session_id == session_id) {
            st->sessions[i] = st->sessions[--st->count];
            return;
        }
    }
}

/*===========================================================================
 * Inline WGSL — fullscreen textured quad, no vertex buffer.
 *=========================================================================*/

static const char *yrdawn_figure_wgsl(void)
{
    static const char src[] = "@group(0) @binding(0) var yrdawn_samp: sampler;\n"
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

static int build_pipeline(struct yetty_yrdawn_figure *f)
{
    WGPUDevice device = yetty_yrdawn_session_shared_device(f->session);
    if (!device) {
        return 0;
    }
    const char *wgsl_src = yrdawn_figure_wgsl();
    size_t wgsl_len = strlen(wgsl_src);

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){wgsl_src, wgsl_len};

    WGPUShaderModuleDescriptor sm_desc = {0};
    sm_desc.nextInChain = &wgsl.chain;
    f->shader_module = wgpuDeviceCreateShaderModule(device, &sm_desc);
    if (!f->shader_module) {
        return 0;
    }

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
    f->bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &bgl_desc);
    if (!f->bind_group_layout) {
        return 0;
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &f->bind_group_layout;
    f->pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pl_desc);
    if (!f->pipeline_layout) {
        return 0;
    }

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

    f->pipeline = wgpuDeviceCreateRenderPipeline(device, &rpd);
    if (!f->pipeline) {
        return 0;
    }

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
    f->sampler = wgpuDeviceCreateSampler(device, &sd);
    if (!f->sampler) {
        return 0;
    }

    f->pipeline_ready = 1;
    return 1;
}

static int build_frame_resources(struct yetty_yrdawn_figure *f, uint32_t width, uint32_t height)
{
    WGPUDevice device = yetty_yrdawn_session_shared_device(f->session);
    if (!device) {
        return 0;
    }
    WGPUTextureDescriptor td = {0};
    td.size = (WGPUExtent3D){width, height, 1};
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture tex = wgpuDeviceCreateTexture(device, &td);
    if (!tex) {
        return 0;
    }

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
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device, &bgd);
    if (!bg) {
        wgpuTextureViewRelease(view);
        wgpuTextureDestroy(tex);
        wgpuTextureRelease(tex);
        return 0;
    }

    if (f->frame_bind_group) {
        wgpuBindGroupRelease(f->frame_bind_group);
    }
    if (f->frame_view) {
        wgpuTextureViewRelease(f->frame_view);
    }
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
    if (f->frame_bind_group) {
        wgpuBindGroupRelease(f->frame_bind_group);
    }
    if (f->frame_view) {
        wgpuTextureViewRelease(f->frame_view);
    }
    if (f->frame_texture) {
        wgpuTextureDestroy(f->frame_texture);
        wgpuTextureRelease(f->frame_texture);
    }
    if (f->sampler) {
        wgpuSamplerRelease(f->sampler);
    }
    if (f->pipeline) {
        wgpuRenderPipelineRelease(f->pipeline);
    }
    if (f->pipeline_layout) {
        wgpuPipelineLayoutRelease(f->pipeline_layout);
    }
    if (f->bind_group_layout) {
        wgpuBindGroupLayoutRelease(f->bind_group_layout);
    }
    if (f->shader_module) {
        wgpuShaderModuleRelease(f->shader_module);
    }
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
    if (!f->args || !f->args->emit_osc_fn) {
        ywarn("yrdawn-figure id=%u: no emit_osc_fn wired, dropping OSC %d", f->figure_id, osc_code);
        return YETTY_OK_VOID();
    }
    return f->args->emit_osc_fn(osc_code, payload, len, f->args->emit_osc_user);
}

/*===========================================================================
 * SUB_HELLO — runs during process_bytes (CREATE_CHILD init payload).
 *
 * Reads the session_id, looks up / creates the session, binds the
 * figure to it, builds the pipeline against the session's shared
 * device, and ships HELLO_ACK.
 *=========================================================================*/

static struct yetty_ycore_void_result handle_hello(struct yetty_yrdawn_figure *f,
                                                   const uint8_t *body, size_t body_len)
{
    if (body_len < sizeof(struct yetty_yrdawn_wire_hello)) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: HELLO truncated");
    }
    const struct yetty_yrdawn_wire_hello *h = (const struct yetty_yrdawn_wire_hello *)body;
    if (h->magic != YETTY_YRDAWN_MAGIC_HELLO) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: HELLO bad magic");
    }
    if (h->session_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: HELLO session_id=0");
    }
    if (h->figure_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: HELLO figure_id=0");
    }
    f->figure_id = h->figure_id;

    /* Build / find session, bind figure to it. */
    struct yetty_yrdawn_factory_state *st = f->args->state;
    struct yetty_yrdawn_session *s = state_find_session(st, h->session_id);
    if (!s) {
        struct yetty_yrdawn_session_ptr_result sr =
            yetty_yrdawn_session_create(h->session_id, f->args->context);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yrdawn-figure: session_create");
        s = sr.value;
        struct yetty_ycore_void_result ir = state_insert_session(st, h->session_id, s);
        if (YETTY_IS_ERR(ir)) {
            struct yetty_ycore_void_result dr = yetty_yrdawn_session_destroy(s);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: insert_session", ir);
        }
    }
    f->session = s;
    yetty_yrdawn_session_attach_figure(s, f);

    /* Build pipeline now that we have a device. */
    if (!build_pipeline(f)) {
        release_gpu(f);
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: pipeline build failed");
    }

    struct yetty_yrdawn_wire_hello_ack ack = {
        .magic = YETTY_YRDAWN_MAGIC_HELLO_ACK,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(ack),
        .status = (h->version == YETTY_YRDAWN_WIRE_VERSION) ? YETTY_YRDAWN_HELLO_OK
                                                            : YETTY_YRDAWN_HELLO_REJECTED,
        .session_id = h->session_id,
        .figure_id = f->figure_id,
    };
    struct yetty_ycore_void_result e = emit(f, YETTY_YRDAWN_OSC_SC_HELLO_ACK, &ack, sizeof(ack));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, e, "yrdawn-figure: HELLO_ACK emit");

    if (ack.status == YETTY_YRDAWN_HELLO_OK) {
        f->connected = 1;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * SUB_CMD — codegen dispatcher
 *=========================================================================*/

static struct yetty_ycore_void_result handle_cmd(struct yetty_yrdawn_figure *f, const uint8_t *body,
                                                 size_t body_len)
{
    if (body_len < sizeof(struct yetty_yrdawn_wire_cmd_hdr)) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: CMD truncated");
    }
    const struct yetty_yrdawn_wire_cmd_hdr *hdr = (const struct yetty_yrdawn_wire_cmd_hdr *)body;
    if (hdr->magic != YETTY_YRDAWN_MAGIC_CMD) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: CMD bad magic");
    }
    if (!f->connected) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: CMD before HELLO_ACK");
    }

    const uint8_t *args_body = body + sizeof(*hdr);
    size_t args_body_len = body_len - sizeof(*hdr);
    ydebug("yrdawn-figure id=%u: dispatching method_id=%u req_id=%u body=%zu", f->figure_id,
           hdr->method_id, hdr->req_id, args_body_len);
    uint32_t status =
        yrdawn_server_dispatch(f, hdr->method_id, hdr->req_id, args_body, args_body_len);
    ydebug("yrdawn-figure id=%u: dispatch status=%u method_id=%u", f->figure_id, status,
           hdr->method_id);

    if (status == YRDAWN_DISPATCH_DEFERRED) {
        return YETTY_OK_VOID();
    }
    if (hdr->req_id == 0) {
        return YETTY_OK_VOID();
    }

    struct yetty_yrdawn_wire_reply_hdr reply = {
        .magic = YETTY_YRDAWN_MAGIC_REPLY,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(reply),
        .req_id = hdr->req_id,
        .method_id = hdr->method_id,
        .kind = YETTY_YRDAWN_REPLY_ASYNC,
        .status = status,
        .payload_ref = 0,
        .figure_id = f->figure_id,
    };
    return emit(f, YETTY_YRDAWN_OSC_SC_REPLY, &reply, sizeof(reply));
}

/*===========================================================================
 * SUB_BULK — push chunk into session reassembler
 *=========================================================================*/

static struct yetty_ycore_void_result handle_bulk(struct yetty_yrdawn_figure *f,
                                                  const uint8_t *body, size_t body_len)
{
    if (body_len < sizeof(struct yetty_yrdawn_wire_bulk_hdr)) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK truncated");
    }
    const struct yetty_yrdawn_wire_bulk_hdr *hdr = (const struct yetty_yrdawn_wire_bulk_hdr *)body;
    if (hdr->magic != YETTY_YRDAWN_MAGIC_BULK) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK bad magic");
    }
    if (!f->connected) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK before HELLO_ACK");
    }
    if (body_len < sizeof(*hdr) + hdr->chunk_size) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: BULK chunk_size > body");
    }
    int last = (hdr->flags & YETTY_YRDAWN_BULK_FLAG_LAST) ? 1 : 0;
    return yetty_yrdawn_session_bulk_append(f->session, hdr->ref, hdr->seq, body + sizeof(*hdr),
                                            hdr->chunk_size, last);
}

static struct yetty_ycore_void_result handle_bye(struct yetty_yrdawn_figure *f, const uint8_t *body,
                                                 size_t body_len)
{
    (void)body;
    (void)body_len;
    f->disconnected = 1;
    f->connected = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Sub-op dispatch — shared between process_bytes and process_input
 *=========================================================================*/

static struct yetty_ycore_void_result dispatch_sub(struct yetty_yrdawn_figure *f, uint32_t sub_op,
                                                   const uint8_t *body, size_t body_len)
{
    switch (sub_op) {
    case YETTY_YRDAWN_FIGURE_SUB_HELLO:
        return handle_hello(f, body, body_len);
    case YETTY_YRDAWN_FIGURE_SUB_CMD:
        return handle_cmd(f, body, body_len);
    case YETTY_YRDAWN_FIGURE_SUB_BULK:
        return handle_bulk(f, body, body_len);
    case YETTY_YRDAWN_FIGURE_SUB_BYE:
        return handle_bye(f, body, body_len);
    default:
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: unknown sub_op");
    }
}

/*===========================================================================
 * process_bytes — CREATE_CHILD init payload and any in-memory body
 *=========================================================================*/

static struct yetty_ycore_void_result yrdawn_figure_process_bytes(struct yetty_yfigure_figure *self,
                                                                  const uint8_t *bytes,
                                                                  size_t bytes_len)
{
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)self;
    if (bytes_len < 4) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn process_bytes: too small for sub_op");
    }
    uint32_t sub_op;
    memcpy(&sub_op, bytes, 4);
    return dispatch_sub(f, sub_op, bytes + 4, bytes_len - 4);
}

/*===========================================================================
 * process_input — streaming sub-op decode off the wire SM.
 *
 * The container has already consumed `{u32 length, u32 id}` and bound
 * us to this record. We own `length` bytes — sub_op first, then the
 * wire struct body. Buffer the whole record into a small malloc because
 * the existing per-sub handlers expect contiguous bytes.
 *=========================================================================*/

static struct yetty_ycore_void_result yrdawn_figure_process_input(
    struct yetty_yfigure_figure *self, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)self;

    /* The container handed us one record's body bytes via the SM. We
     * drain everything available right now into a buffer, then dispatch.
     * The container framing (length, id) is already consumed; what's
     * left for us is `length` bytes of body that the wire SM will
     * deliver until at_end. */
    struct yetty_ycore_buffer accum = {0};
    uint8_t scratch[4096];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, scratch, sizeof(scratch));
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_buffer_destroy(&accum);
            return YETTY_ERR(yetty_ycore_void, "yrdawn process_input: sm read", rr);
        }
        if (rr.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                break;
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(&accum, scratch, rr.value);
        if (YETTY_IS_ERR(wr)) {
            yetty_ycore_buffer_destroy(&accum);
            return YETTY_ERR(yetty_ycore_void, "yrdawn process_input: accum write", wr);
        }
    }

    if (accum.size < 4) {
        yetty_ycore_buffer_destroy(&accum);
        return YETTY_ERR(yetty_ycore_void, "yrdawn process_input: record too small for sub_op");
    }
    uint32_t sub_op;
    memcpy(&sub_op, accum.data, 4);
    struct yetty_ycore_void_result dr = dispatch_sub(f, sub_op, accum.data + 4, accum.size - 4);
    yetty_ycore_buffer_destroy(&accum);
    return dr;
}

/*===========================================================================
 * Render
 *=========================================================================*/

static struct yetty_ycore_void_result yrdawn_figure_render(struct yetty_yfigure_figure *self,
                                                           struct yetty_ydraw_target *target)
{
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)self;
    if (!f->pipeline_ready || !f->frame_bind_group || !f->has_frame) {
        return YETTY_OK_VOID();
    }

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_OK_VOID();
    }

    float x = self->rect.min.x;
    float y = self->rect.min.y;
    float w = self->rect.max.x - self->rect.min.x;
    float h = self->rect.max.y - self->rect.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* WebGPU validates that scissor rect lies fully inside the render
     * area. The figure's authored rect may extend past the host's
     * surface (the producer doesn't know the host's pane size when it
     * picks the canvas size). Clamp the scissor against the target's
     * viewport; viewport stays at the authored rect so the texture
     * mapping is unchanged for the visible portion. */
    float target_w = target->viewport.w;
    float target_h = target->viewport.h;
    float scissor_x = x < 0.0f ? 0.0f : x;
    float scissor_y = y < 0.0f ? 0.0f : y;
    float scissor_right = x + w > target_w ? target_w : x + w;
    float scissor_bottom = y + h > target_h ? target_h : y + h;
    if (scissor_right <= scissor_x || scissor_bottom <= scissor_y) {
        return YETTY_OK_VOID();
    }
    uint32_t sx = (uint32_t)scissor_x;
    uint32_t sy = (uint32_t)scissor_y;
    uint32_t sw = (uint32_t)(scissor_right - scissor_x);
    uint32_t sh = (uint32_t)(scissor_bottom - scissor_y);

    ydebug("yrdawn-figure id=%u: render rect=(%.0f,%.0f,%.0fx%.0f) scissor=(%u,%u,%ux%u)",
           f->figure_id, x, y, w, h, sx, sy, sw, sh);

    WGPUDevice device = yetty_yrdawn_session_shared_device(f->session);
    WGPUQueue queue = yetty_yrdawn_session_shared_queue(f->session);

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
    if (!enc) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn-figure: create encoder");
    }

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
    wgpuRenderPassEncoderSetViewport(pass, x, y, w, h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, sx, sy, sw, sh);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cb_desc);
    wgpuQueueSubmit(queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Destroy
 *=========================================================================*/

static struct yetty_ycore_void_result yrdawn_figure_destroy(struct yetty_yfigure_figure *self)
{
    if (!self) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)self;
    release_gpu(f);

    if (f->session) {
        yetty_yrdawn_session_detach_figure(f->session, f);
        if (yetty_yrdawn_session_figure_count(f->session) == 0) {
            uint32_t sid = yetty_yrdawn_session_id(f->session);
            if (f->args) {
                state_remove_session(f->args->state, sid);
            }
            struct yetty_ycore_void_result sr = yetty_yrdawn_session_destroy(f->session);
            if (YETTY_IS_ERR(sr)) {
                yetty_ycore_error_destroy(sr.error);
            }
        }
        f->session = NULL;
    }
    free(f);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Ops + factory
 *=========================================================================*/

static const struct yetty_yfigure_figure_ops *yrdawn_figure_ops(void)
{
    static const struct yetty_yfigure_figure_ops ops = {
        .destroy = yrdawn_figure_destroy,
        .render = yrdawn_figure_render,
        .process_input = yrdawn_figure_process_input,
        .process_bytes = yrdawn_figure_process_bytes,
    };
    return &ops;
}

struct yetty_yrdawn_figure *yetty_yrdawn_figure_from_base(struct yetty_yfigure_figure *base)
{
    if (!base || base->ops != yrdawn_figure_ops()) {
        return NULL;
    }
    return (struct yetty_yrdawn_figure *)base;
}

struct yetty_yfigure_figure *yetty_yrdawn_figure_as_figure(struct yetty_yrdawn_figure *f)
{
    return &f->base;
}

static struct yetty_yfigure_figure_ptr_result yrdawn_factory(struct yetty_ycore_rectangle rect,
                                                             const struct yetty_context *context,
                                                             void *user)
{
    struct yetty_yrdawn_factory_args *args = (struct yetty_yrdawn_factory_args *)user;
    if (!args) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yrdawn_factory: NULL args");
    }
    if (!context && !args->context) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yrdawn_factory: no context");
    }
    if (!args->state) {
        args->state = calloc(1, sizeof(*args->state));
        if (!args->state) {
            return YETTY_ERR(yetty_yfigure_figure_ptr, "yrdawn_factory: state oom");
        }
    }
    /* args->context may have been registered without a context (tooling).
     * Prefer the registry-provided context at mint time. */
    if (!args->context) {
        args->context = context;
    }

    struct yetty_yrdawn_figure *f = calloc(1, sizeof(*f));
    if (!f) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yrdawn_factory: figure oom");
    }
    f->base.ops = yrdawn_figure_ops();
    f->base.rect = rect;
    f->base.dirty = 1;
    f->args = args;
    f->target_format = args->context->runtime->gpu.surface_format;
    /* figure_id, session, pipeline assigned when SUB_HELLO arrives. */
    return YETTY_OK(yetty_yfigure_figure_ptr, &f->base);
}

struct yetty_ycore_void_result yetty_yrdawn_register_factory(
    struct yetty_yfigure_registry *registry, struct yetty_yrdawn_factory_args *args)
{
    if (!registry || !args) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrdawn_register_factory: NULL arg");
    }
    return yetty_yfigure_registry_register(registry, YETTY_YFIGURE_KIND_YRDAWN, yrdawn_factory,
                                           args);
}

struct yetty_ycore_void_result yetty_yrdawn_factory_args_release(
    struct yetty_yrdawn_factory_args *args)
{
    if (!args || !args->state) {
        return YETTY_OK_VOID();
    }
    /* Each figure's destroy already detached itself and freed its
     * session on the last detach. Any sessions left here mean figures
     * outlived the args — a programmer error, but free what's left so
     * we don't leak. */
    for (size_t i = 0; i < args->state->count; ++i) {
        struct yetty_ycore_void_result r =
            yetty_yrdawn_session_destroy(args->state->sessions[i].session);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
    free(args->state->sessions);
    free(args->state);
    args->state = NULL;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Server-dispatch callbacks — ctx = figure, navigate to session
 *=========================================================================*/

void *yrdawn_server_get_shared_instance(void *ctx)
{
    return yetty_yrdawn_session_shared_instance(((struct yetty_yrdawn_figure *)ctx)->session);
}
void *yrdawn_server_get_shared_adapter(void *ctx)
{
    return yetty_yrdawn_session_shared_adapter(((struct yetty_yrdawn_figure *)ctx)->session);
}
void *yrdawn_server_get_shared_device(void *ctx)
{
    return yetty_yrdawn_session_shared_device(((struct yetty_yrdawn_figure *)ctx)->session);
}
void *yrdawn_server_get_shared_queue(void *ctx)
{
    return yetty_yrdawn_session_shared_queue(((struct yetty_yrdawn_figure *)ctx)->session);
}

void *yrdawn_server_handle_get(void *ctx, uint64_t handle)
{
    return yetty_yrdawn_session_handle_get(((struct yetty_yrdawn_figure *)ctx)->session, handle);
}

struct yetty_ycore_void_result yrdawn_server_handle_set(void *ctx, uint64_t handle, void *ptr)
{
    return yetty_yrdawn_session_handle_set(((struct yetty_yrdawn_figure *)ctx)->session, handle,
                                           ptr);
}

void yrdawn_server_handle_release(void *ctx, uint64_t handle)
{
    yetty_yrdawn_session_handle_release(((struct yetty_yrdawn_figure *)ctx)->session, handle);
}

int yrdawn_server_bulk_take(void *ctx, uint32_t ref, struct yetty_ycore_buffer *out)
{
    return yetty_yrdawn_session_bulk_take(((struct yetty_yrdawn_figure *)ctx)->session, ref, out);
}

struct yetty_ycore_void_result yrdawn_server_emit_reply(void *ctx, uint32_t req_id,
                                                        uint32_t method_id, uint32_t status,
                                                        const void *payload, size_t payload_len)
{
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_server_emit_reply: NULL ctx");
    }
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
        .figure_id = f->figure_id,
    };
    if (payload_len == 0 || !payload) {
        return emit(f, YETTY_YRDAWN_OSC_SC_REPLY, &reply, sizeof(reply));
    }
    size_t total = sizeof(reply) + payload_len;
    uint8_t *frame = malloc(total);
    if (!frame) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_server_emit_reply: oom");
    }
    memcpy(frame, &reply, sizeof(reply));
    memcpy(frame + sizeof(reply), payload, payload_len);
    struct yetty_ycore_void_result r = emit(f, YETTY_YRDAWN_OSC_SC_REPLY, frame, total);
    free(frame);
    return r;
}

struct yetty_ycore_void_result yrdawn_server_set_frame(void *ctx, uint32_t width, uint32_t height,
                                                       const uint8_t *pixels, size_t bytes)
{
    if (!ctx || !pixels) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: NULL ctx or pixels");
    }
    if (width == 0 || height == 0) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: zero dim");
    }
    size_t want = (size_t)width * (size_t)height * 4u;
    if (bytes != want) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: byte count != w*h*4");
    }

    struct yetty_yrdawn_figure *f = (struct yetty_yrdawn_figure *)ctx;
    if (!f->frame_texture || wgpuTextureGetWidth(f->frame_texture) != width ||
        wgpuTextureGetHeight(f->frame_texture) != height) {
        if (!build_frame_resources(f, width, height)) {
            return YETTY_ERR(yetty_ycore_void, "yrdawn_set_frame: rebuild texture failed");
        }
    }

    WGPUQueue queue = yetty_yrdawn_session_shared_queue(f->session);
    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = f->frame_texture;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {0};
    layout.bytesPerRow = width * 4u;
    layout.rowsPerImage = height;
    WGPUExtent3D extent = {width, height, 1};
    wgpuQueueWriteTexture(queue, &dst, pixels, bytes, &layout, &extent);

    ydebug("yrdawn-figure id=%u: set_frame %ux%u %zu bytes -> texture updated", f->figure_id, width,
           height, bytes);

    f->has_frame = 1;
    f->base.dirty = 1;
    if (f->args && f->args->request_render_fn) {
        struct yetty_ycore_void_result rr =
            f->args->request_render_fn(f->args->request_render_user);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Per-dispatch arena used by codegen-emitted descriptor decoders.
 *=========================================================================*/

void *yrdawn_arena_alloc(struct yrdawn_arena *a, size_t bytes)
{
    if (!a || bytes == 0) {
        return NULL;
    }
    if (a->count == a->cap) {
        size_t cap = a->cap ? a->cap * 2u : 8u;
        void **grown = realloc(a->chunks, cap * sizeof(*grown));
        if (!grown) {
            return NULL;
        }
        a->chunks = grown;
        a->cap = cap;
    }
    void *p = calloc(1u, bytes);
    if (!p) {
        return NULL;
    }
    a->chunks[a->count++] = p;
    return p;
}

void yrdawn_arena_free(struct yrdawn_arena *a)
{
    if (!a) {
        return;
    }
    for (size_t i = 0; i < a->count; ++i) {
        free(a->chunks[i]);
    }
    free(a->chunks);
    a->chunks = NULL;
    a->count = 0;
    a->cap = 0;
}
