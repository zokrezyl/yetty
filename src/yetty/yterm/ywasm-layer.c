#include <yetty/yterm/ywasm-layer.h>

#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywasm/server.h>
#include <yetty/ywasm/wire.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yterm/terminal.h>

struct ywasm_handle_entry {
    uint64_t handle;
    void *ptr;     /* WGPU* object — filled in once Dawn dispatch lands */
    uint32_t kind; /* codegen-assigned WGPUObjectKind */
};

struct ywasm_bulk_entry {
    uint32_t ref;          /* 0 = free slot */
    uint32_t next_seq;     /* expected next chunk index */
    struct yetty_ycore_buffer buf;
};

struct yetty_yterm_ywasm_layer {
    struct yetty_yrender_terminal_layer base;

    /* GPU context — used by the render path and by the codegen-emitted
     * dispatcher once it starts driving Dawn. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;

    /* Pipeline + per-frame texture. The pipeline samples a single
     * texture across a fullscreen quad fabricated in the vertex shader
     * from gl_VertexIndex — no vertex buffer, no uniforms. */
    int pipeline_ready;
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;

    WGPUTexture frame_texture;
    WGPUTextureView frame_view;
    WGPUBindGroup frame_bind_group;

    struct yetty_ycore_buffer accum;
    int parse_active;
    int parse_code;

    int connected;
    int disconnected;
    int has_frame;       /* set on first set_frame; until then is_empty=1 */

    struct ywasm_handle_entry *handles;
    size_t handle_count;
    size_t handle_cap;

    struct ywasm_bulk_entry *bulks;
    size_t bulk_count;
    size_t bulk_cap;
};

/* Inline WGSL — fullscreen textured quad, no vertex buffer, no uniforms.
 * Texture+sampler at @group(0); the layer rebuilds frame_texture on
 * resize and rebinds frame_bind_group when it does. */
static const char *ywasm_layer_wgsl(void)
{
    static const char src[] =
        "@group(0) @binding(0) var ywasm_samp: sampler;\n"
        "@group(0) @binding(1) var ywasm_tex: texture_2d<f32>;\n"
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
        "    return textureSample(ywasm_tex, ywasm_samp, in.uv);\n"
        "}\n";
    return src;
}

/*===========================================================================
 * Forward declarations
 *=========================================================================*/

static struct yetty_ycore_void_result ywasm_destroy(struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ywasm_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *sm);
static struct yetty_ycore_void_result ywasm_resize_grid(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ycore_grid_size gs, struct yetty_ycore_pixel_size cs);
static struct yetty_yrender_gpu_resource_set_result ywasm_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_int_result ywasm_render(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ydraw_target *target, int force);
static int ywasm_is_dirty(const struct yetty_yrender_terminal_layer *self);
static int ywasm_is_empty(const struct yetty_yrender_terminal_layer *self);
static int ywasm_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int ywasm_on_char(struct yetty_yrender_terminal_layer *self, uint32_t cp, int mods);

static const struct yetty_yterm_terminal_layer_ops *ywasm_ops(void)
{
    static const struct yetty_yterm_terminal_layer_ops ops = {
        .destroy = ywasm_destroy,
        .process_input = ywasm_process_input,
        .resize_grid = ywasm_resize_grid,
        .get_gpu_resource_set = ywasm_get_gpu_resource_set,
        .render = ywasm_render,
        .is_dirty = ywasm_is_dirty,
        .is_empty = ywasm_is_empty,
        .on_key = ywasm_on_key,
        .on_char = ywasm_on_char,
    };
    return &ops;
}

/*===========================================================================
 * Outbound emit helper
 *=========================================================================*/

static struct yetty_ycore_void_result emit(struct yetty_yterm_ywasm_layer *l, int osc_code,
                                           const void *payload, size_t len)
{
    if (!l->base.emit_osc_fn) {
        ywarn("ywasm-layer: no emit_osc_fn wired, dropping OSC %d", osc_code);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r =
        l->base.emit_osc_fn(osc_code, payload, len, l->base.emit_osc_userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ywasm-layer: emit_osc_fn");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Inbound handlers
 *=========================================================================*/

static struct yetty_ycore_void_result handle_hello(struct yetty_yterm_ywasm_layer *l,
                                                   const uint8_t *payload, size_t len)
{
    if (len < sizeof(struct yetty_ywasm_wire_hello))
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: HELLO truncated");
    const struct yetty_ywasm_wire_hello *h = (const struct yetty_ywasm_wire_hello *)payload;
    if (h->magic != YETTY_YWASM_MAGIC_HELLO)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: HELLO bad magic");

    struct yetty_ywasm_wire_hello_ack ack = {
        .magic = YETTY_YWASM_MAGIC_HELLO_ACK,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(ack),
        .status = (h->version == YETTY_YWASM_WIRE_VERSION)
            ? YETTY_YWASM_HELLO_OK : YETTY_YWASM_HELLO_REJECTED,
    };

    struct yetty_ycore_void_result e =
        emit(l, YETTY_YWASM_OSC_SC_HELLO_ACK, &ack, sizeof(ack));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, e, "ywasm-layer: HELLO_ACK");

    if (ack.status == YETTY_YWASM_HELLO_OK)
        l->connected = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_cmd(struct yetty_yterm_ywasm_layer *l,
                                                 const uint8_t *payload, size_t len)
{
    if (len < sizeof(struct yetty_ywasm_wire_cmd_hdr))
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: CMD truncated");
    const struct yetty_ywasm_wire_cmd_hdr *hdr = (const struct yetty_ywasm_wire_cmd_hdr *)payload;
    if (hdr->magic != YETTY_YWASM_MAGIC_CMD)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: CMD bad magic");
    if (!l->connected)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: CMD before HELLO_ACK");

    const uint8_t *args_body = payload + sizeof(*hdr);
    size_t args_body_len = len - sizeof(*hdr);
    ydebug("ywasm-layer: dispatching method_id=%u req_id=%u body=%zu",
           hdr->method_id, hdr->req_id, args_body_len);
    uint32_t status = ywasm_server_dispatch(l, hdr->method_id, hdr->req_id,
                                            args_body, args_body_len);
    ydebug("ywasm-layer: dispatch status=%u method_id=%u", status, hdr->method_id);

    if (status == YWASM_DISPATCH_DEFERRED)
        return YETTY_OK_VOID();
    if (hdr->req_id == 0)
        return YETTY_OK_VOID();

    struct yetty_ywasm_wire_reply_hdr reply = {
        .magic = YETTY_YWASM_MAGIC_REPLY,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(reply),
        .req_id = hdr->req_id,
        .method_id = hdr->method_id,
        .kind = YETTY_YWASM_REPLY_ASYNC,
        .status = status,
        .payload_ref = 0,
    };
    return emit(l, YETTY_YWASM_OSC_SC_REPLY, &reply, sizeof(reply));
}

static struct ywasm_bulk_entry *bulk_find_or_alloc(struct yetty_yterm_ywasm_layer *l,
                                                   uint32_t ref)
{
    for (size_t i = 0; i < l->bulk_count; ++i) {
        if (l->bulks[i].ref == ref)
            return &l->bulks[i];
    }
    for (size_t i = 0; i < l->bulk_count; ++i) {
        if (l->bulks[i].ref == 0) {
            l->bulks[i].ref = ref;
            l->bulks[i].next_seq = 0;
            yetty_ycore_buffer_clear(&l->bulks[i].buf);
            return &l->bulks[i];
        }
    }
    if (l->bulk_count == l->bulk_cap) {
        size_t cap = l->bulk_cap ? l->bulk_cap * 2u : 4u;
        struct ywasm_bulk_entry *grown =
            (struct ywasm_bulk_entry *)realloc(l->bulks, cap * sizeof(*grown));
        if (!grown)
            return NULL;
        memset(grown + l->bulk_count, 0, (cap - l->bulk_count) * sizeof(*grown));
        l->bulks = grown;
        l->bulk_cap = cap;
    }
    struct ywasm_bulk_entry *e = &l->bulks[l->bulk_count++];
    e->ref = ref;
    e->next_seq = 0;
    e->buf = (struct yetty_ycore_buffer){0};
    return e;
}

static void bulk_release(struct yetty_yterm_ywasm_layer *l, uint32_t ref)
{
    for (size_t i = 0; i < l->bulk_count; ++i) {
        if (l->bulks[i].ref == ref) {
            yetty_ycore_buffer_destroy(&l->bulks[i].buf);
            l->bulks[i] = (struct ywasm_bulk_entry){0};
            return;
        }
    }
}

static struct yetty_ycore_void_result handle_bulk(struct yetty_yterm_ywasm_layer *l,
                                                  const uint8_t *payload, size_t len)
{
    if (len < sizeof(struct yetty_ywasm_wire_bulk_hdr))
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: BULK truncated");
    const struct yetty_ywasm_wire_bulk_hdr *hdr =
        (const struct yetty_ywasm_wire_bulk_hdr *)payload;
    if (hdr->magic != YETTY_YWASM_MAGIC_BULK)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: BULK bad magic");
    if (hdr->ref == 0)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: BULK ref=0");
    if (len < sizeof(*hdr) + hdr->chunk_size)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: BULK chunk_size > payload");

    struct ywasm_bulk_entry *e = bulk_find_or_alloc(l, hdr->ref);
    if (!e)
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: BULK table oom");
    if (hdr->seq != e->next_seq) {
        bulk_release(l, hdr->ref);
        return YETTY_ERR(yetty_ycore_void, "ywasm-layer: BULK out-of-order seq");
    }

    if (hdr->chunk_size) {
        struct yetty_ycore_void_result w =
            yetty_ycore_buffer_write(&e->buf, payload + sizeof(*hdr), hdr->chunk_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, w, "ywasm-layer: BULK accum");
    }
    e->next_seq++;

    if (hdr->flags & YETTY_YWASM_BULK_FLAG_LAST) {
        /* Payload sits in e->buf until a CMD references it via payload_ref
         * — the dispatcher takes it via ywasm_server_bulk_take. Mark the
         * entry "complete" by parking next_seq at UINT32_MAX so further
         * chunks against the same ref are rejected. */
        e->next_seq = UINT32_MAX;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Per-dispatch arena used by codegen-emitted descriptor decoders.
 *=========================================================================*/

void *ywasm_arena_alloc(struct ywasm_arena *a, size_t bytes)
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

void ywasm_arena_free(struct ywasm_arena *a)
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

struct yetty_ycore_void_result ywasm_server_emit_reply(void *ctx, uint32_t req_id,
                                                       uint32_t method_id, uint32_t status,
                                                       const void *payload, size_t payload_len)
{
    if (!ctx)
        return YETTY_ERR(yetty_ycore_void, "ywasm_server_emit_reply: NULL ctx");
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)ctx;
    struct yetty_ywasm_wire_reply_hdr reply = {
        .magic = YETTY_YWASM_MAGIC_REPLY,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)(sizeof(reply) + payload_len),
        .req_id = req_id,
        .method_id = method_id,
        .kind = YETTY_YWASM_REPLY_ASYNC,
        .status = status,
        .payload_ref = 0,
    };
    if (payload_len == 0 || !payload)
        return emit(l, YETTY_YWASM_OSC_SC_REPLY, &reply, sizeof(reply));
    size_t total = sizeof(reply) + payload_len;
    uint8_t *frame = (uint8_t *)malloc(total);
    if (!frame)
        return YETTY_ERR(yetty_ycore_void, "ywasm_server_emit_reply: oom");
    memcpy(frame, &reply, sizeof(reply));
    memcpy(frame + sizeof(reply), payload, payload_len);
    struct yetty_ycore_void_result r = emit(l, YETTY_YWASM_OSC_SC_REPLY, frame, total);
    free(frame);
    return r;
}

int ywasm_server_bulk_take(void *ctx, uint32_t ref, struct yetty_ycore_buffer *out)
{
    if (!ctx || !out)
        return 0;
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)ctx;
    for (size_t i = 0; i < l->bulk_count; ++i) {
        if (l->bulks[i].ref == ref && l->bulks[i].next_seq == UINT32_MAX) {
            *out = l->bulks[i].buf;
            l->bulks[i] = (struct ywasm_bulk_entry){0};
            return 1;
        }
    }
    return 0;
}

static struct yetty_ycore_void_result handle_bye(struct yetty_yterm_ywasm_layer *l,
                                                 const uint8_t *payload, size_t len)
{
    (void)payload;
    (void)len;
    l->disconnected = 1;
    l->connected = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dispatch(struct yetty_yterm_ywasm_layer *l, int code,
                                               const uint8_t *payload, size_t len)
{
    switch (code) {
    case YETTY_YWASM_OSC_CS_HELLO:
        return handle_hello(l, payload, len);
    case YETTY_YWASM_OSC_CS_CMD:
        return handle_cmd(l, payload, len);
    case YETTY_YWASM_OSC_CS_BULK:
        return handle_bulk(l, payload, len);
    case YETTY_YWASM_OSC_CS_BYE:
        return handle_bye(l, payload, len);
    default:
        ywarn("ywasm-layer: unknown OSC code %d", code);
        return YETTY_OK_VOID();
    }
}

/*===========================================================================
 * process_input — pull-mode dispatch from the OSC state machine.
 * Same shape as ymgui-layer's: accumulate until at_end, dispatch, repeat.
 *=========================================================================*/

static struct yetty_ycore_void_result ywasm_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)self;

    for (;;) {
        if (!l->parse_active) {
            l->parse_code = yetty_ywire_wire_statemachine_code(sm);
            yetty_ycore_buffer_clear(&l->accum);
            l->parse_active = 1;
        }

        uint8_t buf[4096];
        for (;;) {
            struct yetty_ycore_size_result rr =
                yetty_ywire_wire_statemachine_read(sm, buf, sizeof(buf));
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ywasm-layer: osc read");
            if (rr.value == 0)
                break;
            struct yetty_ycore_void_result wr =
                yetty_ycore_buffer_write(&l->accum, buf, rr.value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "ywasm-layer: accum write");
        }

        if (!yetty_ywire_wire_statemachine_at_end(sm)) {
            yetty_yplatform_coro_yield();
            continue;
        }

        int code = l->parse_code;
        const uint8_t *payload = l->accum.data;
        size_t payload_len = l->accum.size;

        struct yetty_ycore_void_result r = dispatch(l, code, payload, payload_len);

        yetty_ycore_buffer_clear(&l->accum);
        l->parse_active = 0;
        l->parse_code = 0;

        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ywasm-layer: dispatch");

        yetty_yplatform_coro_yield();
    }
}

/*===========================================================================
 * Other ops (skeleton-level)
 *=========================================================================*/

static struct yetty_ycore_void_result ywasm_resize_grid(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ycore_grid_size gs, struct yetty_ycore_pixel_size cs)
{
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)self;
    self->grid_size = gs;
    self->cell_size = cs;
    self->dirty = 1;

    if (l->connected) {
        struct yetty_ywasm_wire_input_resize msg = {
            .magic = YETTY_YWASM_MAGIC_INPUT_RESIZE,
            .version = YETTY_YWASM_WIRE_VERSION,
            .total_size = (uint32_t)sizeof(msg),
            .width = (float)gs.cols * cs.width,
            .height = (float)gs.rows * cs.height,
        };
        struct yetty_ycore_void_result r =
            emit(l, YETTY_YWASM_OSC_SC_RESIZE, &msg, sizeof(msg));
        if (YETTY_IS_ERR(r))
            yetty_ycore_error_destroy(r.error);
    }
    return YETTY_OK_VOID();
}

static struct yetty_yrender_gpu_resource_set_result ywasm_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    static const struct yetty_ydraw_gpu_resource_set empty = {0};
    return YETTY_OK(yetty_yrender_gpu_resource_set, &empty);
}

static struct yetty_ycore_int_result ywasm_render(struct yetty_yrender_terminal_layer *self,
                                                  struct yetty_ydraw_target *target, int force)
{
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)self;
    if (!target || !target->ops || !target->ops->get_view)
        return YETTY_OK(yetty_ycore_int, 0);
    if (!l->pipeline_ready || !l->frame_bind_group)
        return YETTY_OK(yetty_ycore_int, 0);
    if (!self->dirty && !force)
        return YETTY_OK(yetty_ycore_int, 0);

    WGPUTextureView view = target->ops->get_view(target);
    if (!view)
        return YETTY_OK(yetty_ycore_int, 0);
    ydebug("ywasm_render: drawing vp=(%.0f,%.0f,%.0fx%.0f)",
           target->viewport.x, target->viewport.y, target->viewport.w, target->viewport.h);

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(l->device, &enc_desc);
    if (!enc)
        return YETTY_ERR(yetty_ycore_int, "ywasm_render: create encoder");

    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, l->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, l->frame_bind_group, 0, NULL);

    /* Confine to the pane's pixel rect — pane_render() in yui/tile.c
     * writes the pane bounds into target->viewport before calling us.
     * Without this we draw across the whole big_target and stomp
     * neighbouring panes. */
    struct yetty_yrender_viewport vp = target->viewport;
    if (vp.w > 0.0f && vp.h > 0.0f) {
        wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.w, vp.h, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)vp.x, (uint32_t)vp.y,
                                            (uint32_t)vp.w, (uint32_t)vp.h);
    }

    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cb_desc);
    wgpuQueueSubmit(l->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    self->dirty = 0;
    return YETTY_OK(yetty_ycore_int, 1);
}

static int ywasm_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    return self->dirty;
}

static int ywasm_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_ywasm_layer *l = (const struct yetty_yterm_ywasm_layer *)self;
    /* The bridge stays transparent until the client actually pushes a
     * frame. HELLO alone doesn't take over the pane — only set_frame
     * (yetty_ywasm_present_frame) does. */
    return !l->has_frame;
}

static int ywasm_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)self;
    if (!l->connected)
        return 0;
    struct yetty_ywasm_wire_input_key msg = {
        .magic = YETTY_YWASM_MAGIC_INPUT_KEY,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(msg),
        .kind = YETTY_YWASM_INPUT_KEY_DOWN,
        .key = key,
        .mods = mods,
        .codepoint = 0,
    };
    struct yetty_ycore_void_result r =
        emit(l, YETTY_YWASM_OSC_SC_KEY, &msg, sizeof(msg));
    if (YETTY_IS_ERR(r))
        yetty_ycore_error_destroy(r.error);
    return 1;
}

static int ywasm_on_char(struct yetty_yrender_terminal_layer *self, uint32_t cp, int mods)
{
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)self;
    if (!l->connected)
        return 0;
    struct yetty_ywasm_wire_input_key msg = {
        .magic = YETTY_YWASM_MAGIC_INPUT_KEY,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(msg),
        .kind = YETTY_YWASM_INPUT_KEY_CHAR,
        .key = -1,
        .mods = mods,
        .codepoint = cp,
    };
    struct yetty_ycore_void_result r =
        emit(l, YETTY_YWASM_OSC_SC_KEY, &msg, sizeof(msg));
    if (YETTY_IS_ERR(r))
        yetty_ycore_error_destroy(r.error);
    return 1;
}

/*===========================================================================
 * Handle table — backs the ywasm_server_handle_* hooks the codegen
 * dispatcher calls. Linear scan; first cut, fine for now.
 *=========================================================================*/

static struct ywasm_handle_entry *handle_find(struct yetty_yterm_ywasm_layer *l, uint64_t handle)
{
    for (size_t i = 0; i < l->handle_count; ++i) {
        if (l->handles[i].handle == handle)
            return &l->handles[i];
    }
    return NULL;
}

void *ywasm_server_handle_get(void *ctx, uint64_t handle)
{
    if (!ctx || handle == YETTY_YWASM_HANDLE_NULL)
        return NULL;
    struct ywasm_handle_entry *e = handle_find((struct yetty_yterm_ywasm_layer *)ctx, handle);
    return e ? e->ptr : NULL;
}

struct yetty_ycore_void_result ywasm_server_handle_set(void *ctx, uint64_t handle, void *ptr)
{
    if (!ctx || handle == YETTY_YWASM_HANDLE_NULL)
        return YETTY_ERR(yetty_ycore_void, "ywasm_handle_set: bad ctx/handle");
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)ctx;
    struct ywasm_handle_entry *e = handle_find(l, handle);
    if (e) {
        e->ptr = ptr;
        return YETTY_OK_VOID();
    }
    if (l->handle_count == l->handle_cap) {
        size_t cap = l->handle_cap ? l->handle_cap * 2u : 16u;
        struct ywasm_handle_entry *grown =
            (struct ywasm_handle_entry *)realloc(l->handles, cap * sizeof(*grown));
        if (!grown)
            return YETTY_ERR(yetty_ycore_void, "ywasm_handle_set: handle table oom");
        l->handles = grown;
        l->handle_cap = cap;
    }
    l->handles[l->handle_count++] = (struct ywasm_handle_entry){
        .handle = handle, .ptr = ptr, .kind = 0,
    };
    return YETTY_OK_VOID();
}

void ywasm_server_handle_release(void *ctx, uint64_t handle)
{
    if (!ctx)
        return;
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)ctx;
    for (size_t i = 0; i < l->handle_count; ++i) {
        if (l->handles[i].handle == handle) {
            l->handles[i] = l->handles[--l->handle_count];
            return;
        }
    }
}

/*===========================================================================
 * GPU lifecycle — pipeline build + placeholder texture upload
 *=========================================================================*/

#define YWASM_PLACEHOLDER_W 256u
#define YWASM_PLACEHOLDER_H 256u

static int build_pipeline(struct yetty_yterm_ywasm_layer *l)
{
    const char *wgsl_src = ywasm_layer_wgsl();
    size_t wgsl_len = strlen(wgsl_src);

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){wgsl_src, wgsl_len};

    WGPUShaderModuleDescriptor sm_desc = {0};
    sm_desc.nextInChain = &wgsl.chain;
    l->shader_module = wgpuDeviceCreateShaderModule(l->device, &sm_desc);
    if (!l->shader_module)
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
    l->bind_group_layout = wgpuDeviceCreateBindGroupLayout(l->device, &bgl_desc);
    if (!l->bind_group_layout)
        return 0;

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &l->bind_group_layout;
    l->pipeline_layout = wgpuDeviceCreatePipelineLayout(l->device, &pl_desc);
    if (!l->pipeline_layout)
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
    color_target.format = l->target_format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {0};
    fs.module = l->shader_module;
    fs.entryPoint = (WGPUStringView){"fs_main", 7};
    fs.targetCount = 1;
    fs.targets = &color_target;

    WGPURenderPipelineDescriptor rpd = {0};
    rpd.layout = l->pipeline_layout;
    rpd.vertex.module = l->shader_module;
    rpd.vertex.entryPoint = (WGPUStringView){"vs_main", 7};
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode = WGPUCullMode_None;
    rpd.fragment = &fs;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xFFFFFFFFu;

    l->pipeline = wgpuDeviceCreateRenderPipeline(l->device, &rpd);
    if (!l->pipeline)
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
    l->sampler = wgpuDeviceCreateSampler(l->device, &sd);
    if (!l->sampler)
        return 0;

    l->pipeline_ready = 1;
    return 1;
}

static int build_placeholder_texture(struct yetty_yterm_ywasm_layer *l)
{
    WGPUTextureDescriptor td = {0};
    td.size = (WGPUExtent3D){YWASM_PLACEHOLDER_W, YWASM_PLACEHOLDER_H, 1};
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    l->frame_texture = wgpuDeviceCreateTexture(l->device, &td);
    if (!l->frame_texture)
        return 0;

    size_t bytes = (size_t)YWASM_PLACEHOLDER_W * YWASM_PLACEHOLDER_H * 4u;
    uint8_t *pixels = (uint8_t *)malloc(bytes);
    if (!pixels)
        return 0;
    /* Brand mint #6BA892 (107, 168, 146, 255) — until a real client frame
     * arrives, the layer paints this so the bridge is visibly present. */
    for (size_t i = 0; i < bytes; i += 4u) {
        pixels[i + 0] = 107u;
        pixels[i + 1] = 168u;
        pixels[i + 2] = 146u;
        pixels[i + 3] = 255u;
    }

    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = l->frame_texture;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {0};
    layout.bytesPerRow = YWASM_PLACEHOLDER_W * 4u;
    layout.rowsPerImage = YWASM_PLACEHOLDER_H;
    WGPUExtent3D extent = {YWASM_PLACEHOLDER_W, YWASM_PLACEHOLDER_H, 1};
    wgpuQueueWriteTexture(l->queue, &dst, pixels, bytes, &layout, &extent);
    free(pixels);

    WGPUTextureViewDescriptor tvd = {0};
    tvd.format = WGPUTextureFormat_RGBA8Unorm;
    tvd.dimension = WGPUTextureViewDimension_2D;
    tvd.baseMipLevel = 0;
    tvd.mipLevelCount = 1;
    tvd.baseArrayLayer = 0;
    tvd.arrayLayerCount = 1;
    tvd.aspect = WGPUTextureAspect_All;
    l->frame_view = wgpuTextureCreateView(l->frame_texture, &tvd);
    if (!l->frame_view)
        return 0;

    WGPUBindGroupEntry bge[2] = {0};
    bge[0].binding = 0;
    bge[0].sampler = l->sampler;
    bge[1].binding = 1;
    bge[1].textureView = l->frame_view;

    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = l->bind_group_layout;
    bgd.entryCount = 2;
    bgd.entries = bge;
    l->frame_bind_group = wgpuDeviceCreateBindGroup(l->device, &bgd);
    return l->frame_bind_group ? 1 : 0;
}

static int build_frame_resources(struct yetty_yterm_ywasm_layer *l,
                                 uint32_t width, uint32_t height)
{
    WGPUTextureDescriptor td = {0};
    td.size = (WGPUExtent3D){width, height, 1};
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture tex = wgpuDeviceCreateTexture(l->device, &td);
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
    bge[0].sampler = l->sampler;
    bge[1].binding = 1;
    bge[1].textureView = view;

    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = l->bind_group_layout;
    bgd.entryCount = 2;
    bgd.entries = bge;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(l->device, &bgd);
    if (!bg) {
        wgpuTextureViewRelease(view);
        wgpuTextureDestroy(tex);
        wgpuTextureRelease(tex);
        return 0;
    }

    if (l->frame_bind_group) wgpuBindGroupRelease(l->frame_bind_group);
    if (l->frame_view) wgpuTextureViewRelease(l->frame_view);
    if (l->frame_texture) {
        wgpuTextureDestroy(l->frame_texture);
        wgpuTextureRelease(l->frame_texture);
    }
    l->frame_texture = tex;
    l->frame_view = view;
    l->frame_bind_group = bg;
    return 1;
}

struct yetty_ycore_void_result ywasm_server_set_frame(
    void *ctx, uint32_t width, uint32_t height,
    const uint8_t *pixels, size_t bytes)
{
    if (!ctx || !pixels)
        return YETTY_ERR(yetty_ycore_void, "ywasm_set_frame: NULL ctx or pixels");
    if (width == 0 || height == 0)
        return YETTY_ERR(yetty_ycore_void, "ywasm_set_frame: zero dim");
    size_t want = (size_t)width * (size_t)height * 4u;
    if (bytes != want)
        return YETTY_ERR(yetty_ycore_void, "ywasm_set_frame: byte count != w*h*4");

    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)ctx;
    if (!l->frame_texture ||
        wgpuTextureGetWidth(l->frame_texture) != width ||
        wgpuTextureGetHeight(l->frame_texture) != height) {
        if (!build_frame_resources(l, width, height))
            return YETTY_ERR(yetty_ycore_void, "ywasm_set_frame: rebuild texture failed");
    }

    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = l->frame_texture;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {0};
    layout.bytesPerRow = width * 4u;
    layout.rowsPerImage = height;
    WGPUExtent3D extent = {width, height, 1};
    wgpuQueueWriteTexture(l->queue, &dst, pixels, bytes, &layout, &extent);

    ydebug("ywasm-layer: set_frame %ux%u %zu bytes -> texture updated", width, height, bytes);

    l->has_frame = 1;
    l->base.dirty = 1;
    if (l->base.request_render_fn)
        (void)l->base.request_render_fn(l->base.request_render_userdata);
    return YETTY_OK_VOID();
}

static void release_gpu(struct yetty_yterm_ywasm_layer *l)
{
    if (l->frame_bind_group) wgpuBindGroupRelease(l->frame_bind_group);
    if (l->frame_view) wgpuTextureViewRelease(l->frame_view);
    if (l->frame_texture) {
        wgpuTextureDestroy(l->frame_texture);
        wgpuTextureRelease(l->frame_texture);
    }
    if (l->sampler) wgpuSamplerRelease(l->sampler);
    if (l->pipeline) wgpuRenderPipelineRelease(l->pipeline);
    if (l->pipeline_layout) wgpuPipelineLayoutRelease(l->pipeline_layout);
    if (l->bind_group_layout) wgpuBindGroupLayoutRelease(l->bind_group_layout);
    if (l->shader_module) wgpuShaderModuleRelease(l->shader_module);
    l->frame_bind_group = NULL;
    l->frame_view = NULL;
    l->frame_texture = NULL;
    l->sampler = NULL;
    l->pipeline = NULL;
    l->pipeline_layout = NULL;
    l->bind_group_layout = NULL;
    l->shader_module = NULL;
    l->pipeline_ready = 0;
}

/*===========================================================================
 * Lifecycle
 *=========================================================================*/

static struct yetty_ycore_void_result ywasm_destroy(struct yetty_yrender_terminal_layer *self)
{
    if (!self)
        return YETTY_OK_VOID();
    struct yetty_yterm_ywasm_layer *l = (struct yetty_yterm_ywasm_layer *)self;
    release_gpu(l);
    yetty_ycore_buffer_destroy(&l->accum);
    for (size_t i = 0; i < l->bulk_count; ++i)
        yetty_ycore_buffer_destroy(&l->bulks[i].buf);
    free(l->bulks);
    free(l->handles);
    free(l);
    return YETTY_OK_VOID();
}

struct yetty_yterm_terminal_layer_result yetty_yterm_ywasm_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height,
    const struct yetty_context *context)
{
    if (!context)
        return YETTY_ERR(yetty_yterm_terminal_layer, "ywasm_layer_create: NULL context");

    struct yetty_yterm_ywasm_layer *l =
        (struct yetty_yterm_ywasm_layer *)calloc(1, sizeof(*l));
    if (!l)
        return YETTY_ERR(yetty_yterm_terminal_layer, "ywasm_layer_create: oom");

    l->base.ops = ywasm_ops();
    l->base.grid_size.cols = cols;
    l->base.grid_size.rows = rows;
    l->base.cell_size.width = cell_width;
    l->base.cell_size.height = cell_height;

    l->device = context->gpu_context.device;
    l->queue = context->gpu_context.queue;
    l->target_format = context->gpu_context.surface_format;
    l->base.dirty = 1;

    if (!build_pipeline(l)) {
        release_gpu(l);
        free(l);
        return YETTY_ERR(yetty_yterm_terminal_layer, "ywasm_layer_create: pipeline build failed");
    }
    /* No placeholder texture: the layer reports is_empty=1 until a real
     * frame lands via set_frame, so it would never sample a placeholder
     * anyway. Frame resources get built on the first set_frame call. */

    return YETTY_OK(yetty_yterm_terminal_layer, &l->base);
}
