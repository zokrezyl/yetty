/*
 * ymgui-layer.c — multi-card Dear ImGui layer.
 *
 * Cards
 *   The layer hosts a registry of cards (see include/yetty/ymgui/wire.h).
 *   Each card is a placed sub-region of the terminal grid that one
 *   ImGui app draws into. Cards are addressed by client-allocated u32
 *   IDs. Multiple cards may coexist; mouse hit-test routes input to
 *   the topmost card under the cursor.
 *
 * GPU model
 *   The layer owns ONE pipeline + sampler (compiled once, cached). Per
 *   card it owns: vertex/index buffers, atlas texture+view, uniform
 *   buffer, and a bind group binding all three. Card geometry is in
 *   card-local pixels; the vertex shader translates by card_origin and
 *   projects to NDC by pane_size. Both come from the per-card UBO.
 *
 * Scrolling
 *   Each card is anchored at a rolling_row at placement time (same
 *   model the ydraw canvas uses). card_origin_y on render is computed
 *   as (rolling_row - row0_absolute) * cell_height. Scroll is O(1):
 *   geometry never re-uploads, the per-card uniform is rewritten.
 *
 * Atlas
 *   Each card uploads its own font atlas via --tex with that card's
 *   id. R8 only today (matches ImGui's Alpha8 atlas).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yface/yface.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/osc-args.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yterm/ymgui-layer.h>
#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Card
 *=========================================================================*/

struct yetty_yterm_ymgui_card {
    uint32_t id;

    /* Placement (grid). w_cells=0 means "until right edge" — the card's
     * effective width tracks grid width on resize. */
    int32_t col;
    uint32_t w_cells;
    uint32_t h_cells;

    /* Anchor: absolute rolling row of the card's top edge. */
    uint32_t rolling_row;

    /* Latest decoded frame (owning copy of the OSC payload, post-LZ4F). */
    uint8_t *frame_bytes;
    size_t frame_size;
    int has_frame;
    float frame_display_w; /* ImGui DisplaySize from the last frame */
    float frame_display_h;

    /* Atlas. */
    int atlas_ready;
    uint32_t atlas_w;
    uint32_t atlas_h;
    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;

    /* GPU state owned by the card. */
    WGPUBindGroup bind_group;  /* rebuilt when atlas changes */
    WGPUBuffer uniform_buffer; /* 32 B */
    WGPUBuffer vtx_buf;
    size_t vtx_buf_capacity;
    WGPUBuffer idx_buf;
    size_t idx_buf_capacity;
};

/*===========================================================================
 * Layer
 *=========================================================================*/

struct yetty_yterm_ymgui_layer {
    struct yetty_yrender_terminal_layer base;

    /* GPU context. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;

    /* WGSL source — read once at create. */
    struct yetty_ycore_buffer shader_code;

    /* Shared pipeline. */
    int pipeline_ready;
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;

    /* Card registry — newer cards are appended; topmost-under-cursor =
     * iterate back-to-front. */
    struct yetty_yterm_ymgui_card **cards;
    size_t card_count;
    size_t card_cap;

    /* Streaming OSC decoder (b64 + LZ4F). Reused across all uploads. */
    /* yface — kept for OUTGOING emit only. Incoming decode lives in
     * the OSC SM. */
    struct yetty_yface *yface;

    /* Per-envelope decoded-payload accumulator (see ydraw-layer for
     * the same pattern). */
    struct yetty_ycore_buffer accum;
    int parse_code;
    int parse_active;

    /* Scrolling / cursor tracking. */
    uint32_t row0_absolute;
    uint32_t cursor_col;
    uint32_t cursor_row;

    /* Click-focus. 0 = no card focused. */
    uint32_t focused_card_id;

    /* Alt-screen state. The currently-active card set is in the fields
     * above (cards/card_count/...). The "other" set (primary while we're
     * in alt, alt while we're in primary) is parked here. Toggle via
     * ymgui_set_alt_screen swaps the two halves wholesale — GPU
     * resources owned by saved cards stay alive across the swap. */
    int alt_active;
    struct yetty_yterm_ymgui_card **saved_cards;
    size_t saved_card_count;
    size_t saved_card_cap;
    uint32_t saved_focused_card_id;
};

/*===========================================================================
 * Forward declarations
 *=========================================================================*/

static struct yetty_ycore_void_result ymgui_destroy(struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ymgui_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *osc_statemachine);
static struct yetty_ycore_void_result ymgui_resize_grid(struct yetty_yrender_terminal_layer *self,
                                                        struct yetty_ycore_grid_size gs);
static struct yetty_ycore_void_result ymgui_set_cell_size(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ycore_pixel_size cs);
static struct yetty_ycore_void_result ymgui_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y);
static struct yetty_yrender_gpu_resource_set_result ymgui_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ymgui_render(struct yetty_yrender_terminal_layer *self,
                                                   struct yetty_ydraw_target *target);
static int ymgui_is_empty(const struct yetty_yrender_terminal_layer *self);
static int ymgui_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int ymgui_on_char(struct yetty_yrender_terminal_layer *self, uint32_t cp, int mods);
static struct yetty_ycore_void_result ymgui_scroll(struct yetty_yrender_terminal_layer *self,
                                                   int lines);
static struct yetty_ycore_void_result ymgui_set_cursor(struct yetty_yrender_terminal_layer *self,
                                                       int col, int row);
static struct yetty_ycore_void_result ymgui_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active);

static const struct yetty_yterm_terminal_layer_ops ymgui_ops = {
    .destroy = ymgui_destroy,
    .process_input = ymgui_process_input,
    .resize_grid = ymgui_resize_grid,
    .set_cell_size = ymgui_set_cell_size,
    .set_visual_zoom = ymgui_set_visual_zoom,
    .get_gpu_resource_set = ymgui_get_gpu_resource_set,
    .render = ymgui_render,
    .is_empty = ymgui_is_empty,
    .on_key = ymgui_on_key,
    .on_char = ymgui_on_char,
    .scroll = ymgui_scroll,
    .set_cursor = ymgui_set_cursor,
    .set_alt_screen = ymgui_set_alt_screen,
};

/*===========================================================================
 * Card lookup / lifecycle
 *=========================================================================*/

static struct yetty_yterm_ymgui_card *card_find(const struct yetty_yterm_ymgui_layer *l,
                                                uint32_t id)
{
    for (size_t i = 0; i < l->card_count; i++) {
        if (l->cards[i]->id == id) {
            return l->cards[i];
        }
    }
    return NULL;
}

static struct yetty_yterm_ymgui_card *card_alloc(struct yetty_yterm_ymgui_layer *l, uint32_t id)
{
    if (l->card_count == l->card_cap) {
        size_t cap = l->card_cap ? l->card_cap * 2u : 4u;
        struct yetty_yterm_ymgui_card **n =
            (struct yetty_yterm_ymgui_card **)realloc(l->cards, cap * sizeof(*n));
        if (!n) {
            return NULL;
        }
        l->cards = n;
        l->card_cap = cap;
    }
    struct yetty_yterm_ymgui_card *c = (struct yetty_yterm_ymgui_card *)calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    c->id = id;
    l->cards[l->card_count++] = c;
    return c;
}

static void card_release_gpu(struct yetty_yterm_ymgui_card *c)
{
    if (c->bind_group) {
        wgpuBindGroupRelease(c->bind_group);
        c->bind_group = NULL;
    }
    if (c->atlas_view) {
        wgpuTextureViewRelease(c->atlas_view);
        c->atlas_view = NULL;
    }
    if (c->atlas_texture) {
        wgpuTextureDestroy(c->atlas_texture);
        wgpuTextureRelease(c->atlas_texture);
        c->atlas_texture = NULL;
    }
    if (c->uniform_buffer) {
        wgpuBufferRelease(c->uniform_buffer);
        c->uniform_buffer = NULL;
    }
    if (c->vtx_buf) {
        wgpuBufferRelease(c->vtx_buf);
        c->vtx_buf = NULL;
    }
    if (c->idx_buf) {
        wgpuBufferRelease(c->idx_buf);
        c->idx_buf = NULL;
    }
    c->vtx_buf_capacity = 0;
    c->idx_buf_capacity = 0;
    c->atlas_ready = 0;
}

static void card_destroy(struct yetty_yterm_ymgui_card *c)
{
    if (!c) {
        return;
    }
    card_release_gpu(c);
    free(c->frame_bytes);
    free(c);
}

static void card_remove(struct yetty_yterm_ymgui_layer *l, uint32_t id)
{
    for (size_t i = 0; i < l->card_count; i++) {
        if (l->cards[i]->id == id) {
            card_destroy(l->cards[i]);
            for (size_t j = i + 1; j < l->card_count; j++) {
                l->cards[j - 1] = l->cards[j];
            }
            l->card_count--;
            return;
        }
    }
}

static uint32_t card_effective_w_cells(const struct yetty_yterm_ymgui_layer *l,
                                       const struct yetty_yterm_ymgui_card *c)
{
    if (c->w_cells != 0) {
        return c->w_cells;
    }
    /* w_cells == 0 means "until right edge". */
    int32_t col = c->col < 0 ? 0 : c->col;
    if ((uint32_t)col >= l->base.grid_size.cols) {
        return 1;
    }
    return l->base.grid_size.cols - (uint32_t)col;
}

static float card_pixel_w(const struct yetty_yterm_ymgui_layer *l,
                          const struct yetty_yterm_ymgui_card *c)
{
    return (float)card_effective_w_cells(l, c) * l->base.cell_size.width;
}

static float card_pixel_h(const struct yetty_yterm_ymgui_layer *l,
                          const struct yetty_yterm_ymgui_card *c)
{
    return (float)c->h_cells * l->base.cell_size.height;
}

static float card_origin_x(const struct yetty_yterm_ymgui_layer *l,
                           const struct yetty_yterm_ymgui_card *c)
{
    int32_t col = c->col < 0 ? 0 : c->col;
    return (float)col * l->base.cell_size.width;
}

static float card_origin_y(const struct yetty_yterm_ymgui_layer *l,
                           const struct yetty_yterm_ymgui_card *c)
{
    /* int32 to allow temporarily negative when scrolled off the top. */
    return (float)((int32_t)c->rolling_row - (int32_t)l->row0_absolute) * l->base.cell_size.height;
}

static int card_visible(const struct yetty_yterm_ymgui_layer *l,
                        const struct yetty_yterm_ymgui_card *c)
{
    uint32_t row0 = l->row0_absolute;
    uint32_t rows = l->base.grid_size.rows;
    return (c->rolling_row + c->h_cells > row0) && (c->rolling_row < row0 + rows);
}

/*===========================================================================
 * Pipeline (built once, shared across all cards)
 *=========================================================================*/

static void release_pipeline(struct yetty_yterm_ymgui_layer *l)
{
    if (l->pipeline) {
        wgpuRenderPipelineRelease(l->pipeline);
        l->pipeline = NULL;
    }
    if (l->pipeline_layout) {
        wgpuPipelineLayoutRelease(l->pipeline_layout);
        l->pipeline_layout = NULL;
    }
    if (l->bind_group_layout) {
        wgpuBindGroupLayoutRelease(l->bind_group_layout);
        l->bind_group_layout = NULL;
    }
    if (l->shader_module) {
        wgpuShaderModuleRelease(l->shader_module);
        l->shader_module = NULL;
    }
    if (l->sampler) {
        wgpuSamplerRelease(l->sampler);
        l->sampler = NULL;
    }
    l->pipeline_ready = 0;
}

static int build_pipeline(struct yetty_yterm_ymgui_layer *l)
{
    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){(const char *)l->shader_code.data, l->shader_code.size};

    WGPUShaderModuleDescriptor sm_desc = {0};
    sm_desc.nextInChain = &wgsl.chain;
    l->shader_module = wgpuDeviceCreateShaderModule(l->device, &sm_desc);
    if (!l->shader_module) {
        yerror("ymgui: shader module creation failed");
        return 0;
    }

    WGPUBindGroupLayoutEntry bgl_entries[3] = {0};
    bgl_entries[0].binding = 0;
    bgl_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entries[0].buffer.minBindingSize = 32;

    bgl_entries[1].binding = 1;
    bgl_entries[1].visibility = WGPUShaderStage_Fragment;
    bgl_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bgl_entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    bgl_entries[2].binding = 2;
    bgl_entries[2].visibility = WGPUShaderStage_Fragment;
    bgl_entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 3;
    bgl_desc.entries = bgl_entries;
    l->bind_group_layout = wgpuDeviceCreateBindGroupLayout(l->device, &bgl_desc);
    if (!l->bind_group_layout) {
        return 0;
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &l->bind_group_layout;
    l->pipeline_layout = wgpuDeviceCreatePipelineLayout(l->device, &pl_desc);
    if (!l->pipeline_layout) {
        return 0;
    }

    WGPUVertexAttribute vattrs[3] = {0};
    vattrs[0].format = WGPUVertexFormat_Float32x2;
    vattrs[0].offset = 0;
    vattrs[0].shaderLocation = 0;
    vattrs[1].format = WGPUVertexFormat_Float32x2;
    vattrs[1].offset = 8;
    vattrs[1].shaderLocation = 1;
    vattrs[2].format = WGPUVertexFormat_Unorm8x4;
    vattrs[2].offset = 16;
    vattrs[2].shaderLocation = 2;

    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = 20;
    vbl.attributeCount = 3;
    vbl.attributes = vattrs;

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
    rpd.vertex.bufferCount = 1;
    rpd.vertex.buffers = &vbl;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode = WGPUCullMode_None;
    rpd.fragment = &fs;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xFFFFFFFFu;

    l->pipeline = wgpuDeviceCreateRenderPipeline(l->device, &rpd);
    if (!l->pipeline) {
        return 0;
    }

    WGPUSamplerDescriptor sd = {0};
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.maxAnisotropy = 1;
    l->sampler = wgpuDeviceCreateSampler(l->device, &sd);

    l->pipeline_ready = 1;
    ydebug("ymgui: pipeline compiled and cached");
    return 1;
}

/*===========================================================================
 * Per-card GPU helpers
 *=========================================================================*/

static int ensure_card_uniform(struct yetty_yterm_ymgui_layer *l, struct yetty_yterm_ymgui_card *c)
{
    if (c->uniform_buffer) {
        return 1;
    }
    WGPUBufferDescriptor ub = {0};
    ub.size = 32;
    ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    c->uniform_buffer = wgpuDeviceCreateBuffer(l->device, &ub);
    return c->uniform_buffer != NULL;
}

static void rebuild_card_bind_group(struct yetty_yterm_ymgui_layer *l,
                                    struct yetty_yterm_ymgui_card *c)
{
    if (c->bind_group) {
        wgpuBindGroupRelease(c->bind_group);
        c->bind_group = NULL;
    }
    if (!l->pipeline_ready || !c->atlas_ready || !c->uniform_buffer) {
        return;
    }

    WGPUBindGroupEntry e[3] = {0};
    e[0].binding = 0;
    e[0].buffer = c->uniform_buffer;
    e[0].size = 32;
    e[1].binding = 1;
    e[1].textureView = c->atlas_view;
    e[2].binding = 2;
    e[2].sampler = l->sampler;

    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = l->bind_group_layout;
    bgd.entryCount = 3;
    bgd.entries = e;
    c->bind_group = wgpuDeviceCreateBindGroup(l->device, &bgd);
}

static int ensure_buffer(WGPUDevice dev, WGPUBuffer *buf, size_t *cap, size_t need,
                         WGPUBufferUsage usage)
{
    if (need <= *cap && *buf) {
        return 1;
    }
    if (*buf) {
        wgpuBufferRelease(*buf);
        *buf = NULL;
    }
    size_t new_cap = need + need / 4u;
    if (new_cap < 4096) {
        new_cap = 4096;
    }
    new_cap = (new_cap + 3u) & ~(size_t)3u;
    WGPUBufferDescriptor bd = {0};
    bd.size = new_cap;
    bd.usage = usage;
    *buf = wgpuDeviceCreateBuffer(dev, &bd);
    if (!*buf) {
        return 0;
    }
    *cap = new_cap;
    return 1;
}

/*===========================================================================
 * Atlas upload (--tex)
 *=========================================================================*/

static int upload_card_atlas(struct yetty_yterm_ymgui_layer *l, struct yetty_yterm_ymgui_card *c,
                             const struct yetty_ymgui_wire_tex *th)
{
    if (th->format != YMGUI_TEX_FMT_R8) {
        yerror("ymgui: --tex format %u not supported (R8 only)", th->format);
        return 0;
    }

    /* Reset GPU bits owned by the atlas (texture+view+bind_group). */
    if (c->bind_group) {
        wgpuBindGroupRelease(c->bind_group);
        c->bind_group = NULL;
    }
    if (c->atlas_view) {
        wgpuTextureViewRelease(c->atlas_view);
        c->atlas_view = NULL;
    }
    if (c->atlas_texture) {
        wgpuTextureDestroy(c->atlas_texture);
        wgpuTextureRelease(c->atlas_texture);
        c->atlas_texture = NULL;
    }
    c->atlas_ready = 0;

    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = th->width;
    td.size.height = th->height;
    td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_R8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    c->atlas_texture = wgpuDeviceCreateTexture(l->device, &td);
    if (!c->atlas_texture) {
        return 0;
    }

    WGPUTextureViewDescriptor vd = {0};
    vd.format = WGPUTextureFormat_R8Unorm;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    c->atlas_view = wgpuTextureCreateView(c->atlas_texture, &vd);

    const uint8_t *pixels = (const uint8_t *)(th + 1);
    size_t pixel_bytes = (size_t)th->width * (size_t)th->height;

    WGPUTexelCopyTextureInfo dest = {0};
    dest.texture = c->atlas_texture;
    WGPUTexelCopyBufferLayout src_layout = {0};
    src_layout.bytesPerRow = th->width;
    src_layout.rowsPerImage = th->height;
    WGPUExtent3D extent = {th->width, th->height, 1};
    wgpuQueueWriteTexture(l->queue, &dest, pixels, pixel_bytes, &src_layout, &extent);

    c->atlas_w = th->width;
    c->atlas_h = th->height;
    c->atlas_ready = 1;
    if (!ensure_card_uniform(l, c)) {
        return 0;
    }
    rebuild_card_bind_group(l, c);

    ydebug("ymgui: card=%u atlas %ux%u R8", c->id, th->width, th->height);
    return 1;
}

/*===========================================================================
 * Wire validators
 *=========================================================================*/

static int validate_frame(const uint8_t *data, size_t size,
                          const struct yetty_ymgui_wire_frame **out_hdr)
{
    if (size < sizeof(struct yetty_ymgui_wire_frame)) {
        return -1;
    }
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)data;
    if (fh->magic != YMGUI_WIRE_MAGIC_FRAME) {
        return -1;
    }
    if (fh->version != YMGUI_WIRE_VERSION) {
        return -1;
    }
    if (fh->total_size != size) {
        return -1;
    }
    *out_hdr = fh;
    return 0;
}

static int validate_tex(const uint8_t *data, size_t size,
                        const struct yetty_ymgui_wire_tex **out_hdr)
{
    if (size < sizeof(struct yetty_ymgui_wire_tex)) {
        return -1;
    }
    const struct yetty_ymgui_wire_tex *th = (const struct yetty_ymgui_wire_tex *)data;
    if (th->magic != YMGUI_WIRE_MAGIC_TEX) {
        return -1;
    }
    if (th->version != YMGUI_WIRE_VERSION) {
        return -1;
    }
    if (th->total_size != size) {
        return -1;
    }
    uint32_t bpp = (th->format == YMGUI_TEX_FMT_R8)      ? 1u
                   : (th->format == YMGUI_TEX_FMT_RGBA8) ? 4u
                                                         : 0u;
    if (bpp == 0) {
        return -1;
    }
    if ((size_t)th->total_size != sizeof(*th) + (size_t)th->width * (size_t)th->height * bpp) {
        return -1;
    }
    *out_hdr = th;
    return 0;
}

/*===========================================================================
 * Card placement / removal
 *=========================================================================*/

static struct yetty_ycore_void_result anchor_card_and_fit(
    struct yetty_yterm_ymgui_layer *l, struct yetty_yterm_ymgui_card *c, int row_visible_top)
{
    /* Resolve visible-relative `row` to a rolling_row anchor. */
    if (row_visible_top < 0) {
        row_visible_top = 0;
    }
    c->rolling_row = l->row0_absolute + (uint32_t)row_visible_top;

    uint32_t rows = l->base.grid_size.rows;
    uint32_t card_top_visible = (uint32_t)row_visible_top;
    uint32_t span = c->h_cells ? c->h_cells : 1u;
    uint32_t bottom_excl = card_top_visible + span;

    if (bottom_excl > rows && l->base.scroll_fn && !l->base.in_external_scroll) {
        int need = (int)(bottom_excl - rows);
        struct yetty_ycore_void_result r =
            l->base.scroll_fn(&l->base, need, l->base.scroll_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "anchor_card_and_fit: scroll_fn failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_card_place(struct yetty_yterm_ymgui_layer *l,
                                                        const uint8_t *raw, size_t size)
{
    if (size < sizeof(struct yetty_ymgui_wire_card_place)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: malformed CARD_PLACE");
    }
    const struct yetty_ymgui_wire_card_place *cp = (const struct yetty_ymgui_wire_card_place *)raw;
    if (cp->magic != YMGUI_WIRE_MAGIC_CARD_PLACE) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: bad CARD_PLACE magic");
    }
    if (cp->version != YMGUI_WIRE_VERSION) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: CARD_PLACE version mismatch");
    }
    if (cp->card_id == YMGUI_CARD_ID_NONE) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: CARD_PLACE id=0");
    }

    struct yetty_yterm_ymgui_card *c = card_find(l, cp->card_id);
    int created = 0;
    if (!c) {
        c = card_alloc(l, cp->card_id);
        if (!c) {
            return YETTY_ERR(yetty_ycore_void, "ymgui: card alloc failed");
        }
        created = 1;
    }

    c->col = cp->col;
    c->w_cells = cp->w_cells;
    /* h_cells == 0 = "until bottom edge of the pane at placement time".
     * Resolved once here; the card's height is fixed from then on (the
     * width still tracks the pane if w_cells == 0 — that's dynamic). */
    if (cp->h_cells == 0) {
        int32_t row = cp->row < 0 ? 0 : cp->row;
        uint32_t rows = l->base.grid_size.rows;
        c->h_cells = ((uint32_t)row >= rows) ? 1u : (rows - (uint32_t)row);
    } else {
        c->h_cells = cp->h_cells;
    }

    /* Map visible-row to rolling_row anchor; scroll up if not enough room. */
    {
        struct yetty_ycore_void_result ar = anchor_card_and_fit(l, c, cp->row);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar,
                            "handle_card_place: anchor_card_and_fit failed");
    }

    /* On first placement, advance the cursor under the card so subsequent
     * stdout flows beneath. Move/resize emits do NOT touch the cursor. */
    if (created && l->base.cursor_fn) {
        int new_row = cp->row + (int)c->h_cells;
        uint32_t rows = l->base.grid_size.rows;
        if (new_row < 0) {
            new_row = 0;
        }
        if ((uint32_t)new_row >= rows) {
            new_row = (int)rows - 1;
        }
        struct yetty_ycore_grid_cursor_pos pos = {
            .cols = 0,
            .rows = (uint16_t)new_row,
        };
        struct yetty_ycore_void_result cr =
            l->base.cursor_fn(&l->base, pos, l->base.cursor_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "handle_card_place: cursor_fn failed");
    }

    /* Confirm pixel size to the client (DisplaySize). */
    if (l->base.emit_osc_fn) {
        struct yetty_ymgui_wire_input_resize msg = {
            .magic = YMGUI_WIRE_MAGIC_INPUT_RESIZE,
            .version = YMGUI_WIRE_VERSION,
            .card_id = c->id,
            .width = card_pixel_w(l, c),
            .height = card_pixel_h(l, c),
        };
        struct yetty_ycore_void_result er = l->base.emit_osc_fn(
            YMGUI_OSC_SC_RESIZE, &msg, sizeof(msg), l->base.emit_osc_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er,
                            "handle_card_place: emit_osc_fn(YMGUI_OSC_SC_RESIZE) failed");
    }

    l->base.dirty = 1;
    if (l->base.request_render_fn) {
        struct yetty_ycore_void_result rr =
            l->base.request_render_fn(l->base.request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "handle_card_place: request_render_fn failed");
    }

    ydebug("ymgui: card %u %s at (col=%d row=%d, w=%u h=%u, rolling=%u)", c->id,
           created ? "placed" : "moved", c->col, cp->row, c->w_cells, c->h_cells, c->rolling_row);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_focus(struct yetty_yterm_ymgui_layer *l,
                                                  uint32_t card_id, int gained)
{
    if (!l->base.emit_osc_fn) {
        return YETTY_OK_VOID();
    }
    struct yetty_ymgui_wire_input_focus msg = {
        .magic = YMGUI_WIRE_MAGIC_INPUT_FOCUS,
        .version = YMGUI_WIRE_VERSION,
        .card_id = card_id,
        .gained = gained,
    };
    struct yetty_ycore_void_result r = l->base.emit_osc_fn(
        YMGUI_OSC_SC_FOCUS, &msg, sizeof(msg), l->base.emit_osc_userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_focus: emit_osc_fn(YMGUI_OSC_SC_FOCUS) failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_card_remove(struct yetty_yterm_ymgui_layer *l,
                                                         const uint8_t *raw, size_t size)
{
    if (size < sizeof(struct yetty_ymgui_wire_card_remove)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: malformed CARD_REMOVE");
    }
    const struct yetty_ymgui_wire_card_remove *cr =
        (const struct yetty_ymgui_wire_card_remove *)raw;
    if (cr->magic != YMGUI_WIRE_MAGIC_CARD_REMOVE) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: bad CARD_REMOVE magic");
    }
    if (cr->version != YMGUI_WIRE_VERSION) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: CARD_REMOVE version mismatch");
    }
    if (cr->card_id == YMGUI_CARD_ID_NONE) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: CARD_REMOVE id=0");
    }

    /* TODO: archive to ymgui-static-layer when KEEP_VISIBLE flag set. */
    if (l->focused_card_id == cr->card_id) {
        struct yetty_ycore_void_result ef = emit_focus(l, cr->card_id, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ef,
                            "handle_card_remove: emit_focus(lost) failed");
        l->focused_card_id = 0;
    }
    card_remove(l, cr->card_id);
    l->base.dirty = 1;
    if (l->base.request_render_fn) {
        struct yetty_ycore_void_result rr =
            l->base.request_render_fn(l->base.request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "request_render_fn failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_clear(struct yetty_yterm_ymgui_layer *l,
                                                   const uint8_t *raw, size_t size)
{
    if (size < sizeof(struct yetty_ymgui_wire_clear)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: malformed CLEAR");
    }
    const struct yetty_ymgui_wire_clear *cl = (const struct yetty_ymgui_wire_clear *)raw;
    if (cl->magic != YMGUI_WIRE_MAGIC_CLEAR) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: bad CLEAR magic");
    }
    if (cl->version != YMGUI_WIRE_VERSION) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: CLEAR version mismatch");
    }

    /* TODO: archive to ymgui-static-layer when KEEP_VISIBLE flag set. */
    if (cl->card_id == YMGUI_CARD_ID_NONE) {
        if (l->focused_card_id) {
            struct yetty_ycore_void_result ef = emit_focus(l, l->focused_card_id, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ef,
                                "handle_clear: emit_focus(all) failed");
            l->focused_card_id = 0;
        }
        for (size_t i = 0; i < l->card_count; i++) {
            card_destroy(l->cards[i]);
        }
        l->card_count = 0;
    } else {
        if (l->focused_card_id == cl->card_id) {
            struct yetty_ycore_void_result ef = emit_focus(l, cl->card_id, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ef,
                                "handle_clear: emit_focus(one) failed");
            l->focused_card_id = 0;
        }
        card_remove(l, cl->card_id);
    }
    l->base.dirty = 1;
    if (l->base.request_render_fn) {
        struct yetty_ycore_void_result rr =
            l->base.request_render_fn(l->base.request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "request_render_fn failed");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Frame / atlas handlers
 *=========================================================================*/

static struct yetty_ycore_void_result handle_frame(struct yetty_yterm_ymgui_layer *l,
                                                   const uint8_t *raw, size_t size)
{
    const struct yetty_ymgui_wire_frame *fh = NULL;
    if (validate_frame(raw, size, &fh) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: malformed --frame payload");
    }
    if (fh->card_id == YMGUI_CARD_ID_NONE) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: --frame card_id=0");
    }

    struct yetty_yterm_ymgui_card *c = card_find(l, fh->card_id);
    if (!c) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: --frame for unknown card");
    }

    uint8_t *copy = (uint8_t *)malloc(size);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: oom");
    }
    memcpy(copy, raw, size);

    free(c->frame_bytes);
    c->frame_bytes = copy;
    c->frame_size = size;
    c->has_frame = 1;
    c->frame_display_w = fh->display_size_x;
    c->frame_display_h = fh->display_size_y;

    l->base.dirty = 1;
    if (l->base.request_render_fn) {
        struct yetty_ycore_void_result rr =
            l->base.request_render_fn(l->base.request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "request_render_fn failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_tex(struct yetty_yterm_ymgui_layer *l,
                                                 const uint8_t *raw, size_t size)
{
    const struct yetty_ymgui_wire_tex *th = NULL;
    if (validate_tex(raw, size, &th) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: malformed --tex payload");
    }
    if (th->card_id == YMGUI_CARD_ID_NONE) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: --tex card_id=0");
    }
    if (th->tex_id != YMGUI_TEX_ID_FONT_ATLAS) {
        return YETTY_OK_VOID(); /* user textures: future work */
    }

    struct yetty_yterm_ymgui_card *c = card_find(l, th->card_id);
    if (!c) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: --tex for unknown card");
    }
    if (!upload_card_atlas(l, c, th)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: atlas upload failed");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * write — OSC dispatch
 *=========================================================================*/

/* Process — pulls already-decoded bytes from the OSC SM into the
 * per-envelope accumulator. On at_end the layer dispatches by code. */
static struct yetty_ycore_void_result ymgui_dispatch_atomic(
    struct yetty_yterm_ymgui_layer *l, int code, const uint8_t *payload, size_t payload_len)
{
    switch (code) {
    case YMGUI_OSC_CS_FRAME:
        return handle_frame(l, payload, payload_len);
    case YMGUI_OSC_CS_TEX:
        return handle_tex(l, payload, payload_len);
    case YMGUI_OSC_CS_CARD_PLACE:
        return handle_card_place(l, payload, payload_len);
    case YMGUI_OSC_CS_CARD_REMOVE:
        return handle_card_remove(l, payload, payload_len);
    case YMGUI_OSC_CS_CLEAR:
        return handle_clear(l, payload, payload_len);
    case YMGUI_OSC_CS_TERM_INPUT_SUB: {
        if (payload_len < sizeof(struct yetty_ymgui_wire_term_input_sub)) {
            return YETTY_ERR(yetty_ycore_void, "ymgui: malformed TERM_INPUT_SUB");
        }
        const struct yetty_ymgui_wire_term_input_sub *s =
            (const struct yetty_ymgui_wire_term_input_sub *)payload;
        if (s->magic != YMGUI_WIRE_MAGIC_TERM_INPUT_SUB) {
            return YETTY_ERR(yetty_ycore_void, "ymgui: bad TERM_INPUT_SUB magic");
        }
        if (s->version != YMGUI_WIRE_VERSION) {
            return YETTY_ERR(yetty_ycore_void, "ymgui: TERM_INPUT_SUB version mismatch");
        }
        if (l->base.term_input_sub_fn) {
            l->base.term_input_sub_fn(s->flags, l->base.term_input_sub_userdata);
        }
        return YETTY_OK_VOID();
    }
    default:
        return YETTY_ERR(yetty_ycore_void, "ymgui: unexpected OSC code");
    }
}

static struct yetty_ycore_void_result ymgui_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *osc_statemachine)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;

    if (!l->parse_active) {
        l->parse_code = yetty_ywire_wire_statemachine_code(osc_statemachine);
        yetty_ycore_buffer_clear(&l->accum);
        l->parse_active = 1;
    }

    uint8_t buf[4096];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(osc_statemachine, buf, sizeof(buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ymgui: osc read");
        if (rr.value == 0) {
            break;
        }
        struct yetty_ycore_void_result wr =
            yetty_ycore_buffer_write(&l->accum, buf, rr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "ymgui: accum write");
    }

    if (!yetty_ywire_wire_statemachine_at_end(osc_statemachine)) {
        return YETTY_OK_VOID();
    }

    int code = l->parse_code;
    const uint8_t *payload = l->accum.data;
    size_t payload_len = l->accum.size;

    struct yetty_ycore_void_result r = ymgui_dispatch_atomic(l, code, payload, payload_len);

    yetty_ycore_buffer_clear(&l->accum);
    l->parse_active = 0;
    l->parse_code = 0;

    return r;
}

/*===========================================================================
 * resize / set_cell_size / set_visual_zoom
 *=========================================================================*/

static struct yetty_ycore_void_result ymgui_resize_grid(struct yetty_yrender_terminal_layer *self,
                                                        struct yetty_ycore_grid_size gs)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    self->grid_size = gs;
    self->dirty = 1;

    /* Resize-aware cards (w_cells=0 = "until right edge") need to notify
     * the client of their new pixel size so DisplaySize tracks the pane. */
    if (l->base.emit_osc_fn) {
        for (size_t i = 0; i < l->card_count; i++) {
            struct yetty_yterm_ymgui_card *c = l->cards[i];
            if (c->w_cells != 0) {
                continue;
            }
            struct yetty_ymgui_wire_input_resize msg = {
                .magic = YMGUI_WIRE_MAGIC_INPUT_RESIZE,
                .version = YMGUI_WIRE_VERSION,
                .card_id = c->id,
                .width = card_pixel_w(l, c),
                .height = card_pixel_h(l, c),
            };
            l->base.emit_osc_fn(YMGUI_OSC_SC_RESIZE, &msg, sizeof(msg), l->base.emit_osc_userdata);
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymgui_set_cell_size(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ycore_pixel_size cs)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    if (cs.width <= 0.0f || cs.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: invalid cell size");
    }
    self->cell_size = cs;
    self->dirty = 1;

    /* Cell size change → every card's pixel size changes. */
    if (l->base.emit_osc_fn) {
        for (size_t i = 0; i < l->card_count; i++) {
            struct yetty_yterm_ymgui_card *c = l->cards[i];
            struct yetty_ymgui_wire_input_resize msg = {
                .magic = YMGUI_WIRE_MAGIC_INPUT_RESIZE,
                .version = YMGUI_WIRE_VERSION,
                .card_id = c->id,
                .width = card_pixel_w(l, c),
                .height = card_pixel_h(l, c),
            };
            l->base.emit_osc_fn(YMGUI_OSC_SC_RESIZE, &msg, sizeof(msg), l->base.emit_osc_userdata);
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymgui_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    /* Visual zoom is not yet wired through this layer. Accept silently. */
    (void)self;
    (void)scale;
    (void)off_x;
    (void)off_y;
    return YETTY_OK_VOID();
}

static struct yetty_yrender_gpu_resource_set_result ymgui_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    static const struct yetty_ydraw_gpu_resource_set empty = {0};
    return YETTY_OK(yetty_yrender_gpu_resource_set, &empty);
}

/*===========================================================================
 * render
 *=========================================================================*/

struct yetty_yterm_cl_offsets {
    size_t vtx_byte_offset;
    size_t idx_u32_offset;
    uint32_t cmd_count;
    const struct yetty_ymgui_wire_cmd *cmds;
    uint32_t vtx_count;
};

static int frame_measure(const struct yetty_yterm_ymgui_card *c, size_t *out_vtx_bytes,
                         size_t *out_idx_bytes, int *out_idx32)
{
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)c->frame_bytes;
    const uint8_t *cur = c->frame_bytes + sizeof(*fh);
    const uint8_t *end = c->frame_bytes + c->frame_size;
    int idx32 = (fh->flags & YMGUI_FRAME_FLAG_IDX32) ? 1 : 0;
    size_t idx_bpe = idx32 ? 4u : 2u;

    size_t total_vtx = 0, total_idx_bytes = 0;
    for (uint32_t li = 0; li < fh->cmd_list_count; li++) {
        if (cur + sizeof(struct yetty_ymgui_wire_cmd_list) > end) {
            return 0;
        }
        const struct yetty_ymgui_wire_cmd_list *clh = (const struct yetty_ymgui_wire_cmd_list *)cur;
        cur += sizeof(*clh);

        size_t vbytes = (size_t)clh->vtx_count * 20u;
        cur += vbytes;
        if (cur > end) {
            return 0;
        }

        size_t ibytes_padded = (size_t)clh->idx_count * idx_bpe;
        if (ibytes_padded & 3u) {
            ibytes_padded += 4u - (ibytes_padded & 3u);
        }
        cur += ibytes_padded;
        if (cur > end) {
            return 0;
        }

        cur += (size_t)clh->cmd_count * sizeof(struct yetty_ymgui_wire_cmd);
        if (cur > end) {
            return 0;
        }

        total_vtx += vbytes;
        total_idx_bytes += (size_t)clh->idx_count * 4u;
    }
    *out_vtx_bytes = total_vtx;
    *out_idx_bytes = total_idx_bytes;
    *out_idx32 = idx32;
    return 1;
}

static int frame_upload(struct yetty_yterm_ymgui_layer *l, struct yetty_yterm_ymgui_card *c,
                        struct yetty_yterm_cl_offsets *cls, size_t cls_max, size_t *cls_count,
                        int idx32)
{
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)c->frame_bytes;
    const uint8_t *cur = c->frame_bytes + sizeof(*fh);
    const uint8_t *end = c->frame_bytes + c->frame_size;
    size_t idx_bpe = idx32 ? 4u : 2u;

    static uint32_t *idx_stage = NULL;
    static size_t idx_stage_cap = 0;

    size_t vtx_off = 0;
    size_t idx_off_u32 = 0;
    size_t n = 0;

    for (uint32_t li = 0; li < fh->cmd_list_count; li++) {
        if (n >= cls_max) {
            break;
        }

        const struct yetty_ymgui_wire_cmd_list *clh = (const struct yetty_ymgui_wire_cmd_list *)cur;
        cur += sizeof(*clh);

        const uint8_t *vtx = cur;
        size_t vbytes = (size_t)clh->vtx_count * 20u;
        cur += vbytes;

        const uint8_t *idx = cur;
        size_t ibytes_padded = (size_t)clh->idx_count * idx_bpe;
        if (ibytes_padded & 3u) {
            ibytes_padded += 4u - (ibytes_padded & 3u);
        }
        cur += ibytes_padded;

        const struct yetty_ymgui_wire_cmd *cmds = (const struct yetty_ymgui_wire_cmd *)cur;
        cur += (size_t)clh->cmd_count * sizeof(struct yetty_ymgui_wire_cmd);
        if (cur > end) {
            return 0;
        }

        if (vbytes) {
            wgpuQueueWriteBuffer(l->queue, c->vtx_buf, vtx_off, vtx, vbytes);
        }

        size_t i32_bytes = (size_t)clh->idx_count * 4u;
        if (i32_bytes) {
            if (idx32) {
                wgpuQueueWriteBuffer(l->queue, c->idx_buf, idx_off_u32 * 4u, idx, i32_bytes);
            } else {
                if (idx_stage_cap < clh->idx_count) {
                    free(idx_stage);
                    idx_stage = (uint32_t *)malloc((size_t)clh->idx_count * sizeof(uint32_t));
                    idx_stage_cap = clh->idx_count;
                    if (!idx_stage) {
                        return 0;
                    }
                }
                const uint16_t *src = (const uint16_t *)idx;
                for (uint32_t i = 0; i < clh->idx_count; i++) {
                    idx_stage[i] = src[i];
                }
                wgpuQueueWriteBuffer(l->queue, c->idx_buf, idx_off_u32 * 4u, idx_stage, i32_bytes);
            }
        }

        cls[n].vtx_byte_offset = vtx_off;
        cls[n].idx_u32_offset = idx_off_u32;
        cls[n].cmd_count = clh->cmd_count;
        cls[n].cmds = cmds;
        cls[n].vtx_count = clh->vtx_count;
        n++;

        vtx_off += vbytes;
        idx_off_u32 += clh->idx_count;
    }
    *cls_count = n;
    return 1;
}

static struct yetty_ycore_void_result draw_card(struct yetty_yterm_ymgui_layer *l,
                                                struct yetty_yterm_ymgui_card *c,
                                                WGPURenderPassEncoder pass, float pane_w,
                                                float pane_h, float scissor_off_x,
                                                float scissor_off_y)
{
    if (!c->has_frame || !c->atlas_ready || !c->bind_group) {
        return YETTY_OK_VOID();
    }
    if (!card_visible(l, c)) {
        return YETTY_OK_VOID();
    }

    size_t total_vtx_bytes = 0;
    size_t total_idx_bytes = 0;
    int idx32 = 0;
    if (!frame_measure(c, &total_vtx_bytes, &total_idx_bytes, &idx32)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: frame layout invalid");
    }
    if (total_vtx_bytes == 0 || total_idx_bytes == 0) {
        return YETTY_OK_VOID();
    }

    if (!ensure_buffer(l->device, &c->vtx_buf, &c->vtx_buf_capacity, total_vtx_bytes,
                       WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: vtx alloc failed");
    }
    if (!ensure_buffer(l->device, &c->idx_buf, &c->idx_buf_capacity, total_idx_bytes,
                       WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: idx alloc failed");
    }

    enum { YETTY_YTERM_MAX_CL = 32 };
    struct yetty_yterm_cl_offsets cls[YETTY_YTERM_MAX_CL];
    size_t cls_count = 0;
    if (!frame_upload(l, c, cls, YETTY_YTERM_MAX_CL, &cls_count, idx32)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: upload failed");
    }

    /* Per-card uniform: pane_size for NDC denom, card_origin for translation.
     * Vertex pos is in card-local pixels (matches DisplaySize / DisplayPos=0
     * on the client). Shader: (vert + card_origin) → NDC by pane_size. */
    float ox = card_origin_x(l, c);
    float oy = card_origin_y(l, c);
    float uniforms[8] = {
        pane_w, pane_h, /* display_size := pane_size */
        ox,     oy,     /* frame_top    := card_origin */
        0,      0,      0, 0,
    };
    wgpuQueueWriteBuffer(l->queue, c->uniform_buffer, 0, uniforms, sizeof(uniforms));

    wgpuRenderPassEncoderSetBindGroup(pass, 0, c->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, c->vtx_buf, 0, total_vtx_bytes);
    wgpuRenderPassEncoderSetIndexBuffer(pass, c->idx_buf, WGPUIndexFormat_Uint32, 0,
                                        total_idx_bytes);

    /* Card pixel rect in pane space (for scissor clamping). */
    float cw = card_pixel_w(l, c);
    float ch = card_pixel_h(l, c);
    float card_x0 = ox;
    float card_y0 = oy;
    float card_x1 = ox + cw;
    float card_y1 = oy + ch;
    if (card_x0 < 0) {
        card_x0 = 0;
    }
    if (card_y0 < 0) {
        card_y0 = 0;
    }
    if (card_x1 > pane_w) {
        card_x1 = pane_w;
    }
    if (card_y1 > pane_h) {
        card_y1 = pane_h;
    }

    /* Iterate cmd-lists × cmds. ImGui cmd's clip rect is in card-local
     * pixels (matches DisplayPos=0). Translate to pane and clamp to the
     * card's visible rect — geometry outside is harmless because the
     * card is fully contained in [card_x0,card_x1] × [card_y0,card_y1],
     * but scissor must lie within the render target. */
    for (size_t i = 0; i < cls_count; i++) {
        const struct yetty_yterm_cl_offsets *cl = &cls[i];
        uint32_t base_vtx_idx = (uint32_t)(cl->vtx_byte_offset / 20u);
        for (uint32_t k = 0; k < cl->cmd_count; k++) {
            const struct yetty_ymgui_wire_cmd *dc = &cl->cmds[k];
            if (dc->elem_count == 0) {
                continue;
            }

            float sx0 = ox + dc->clip_min_x;
            float sy0 = oy + dc->clip_min_y;
            float sx1 = ox + dc->clip_max_x;
            float sy1 = oy + dc->clip_max_y;
            if (sx0 < card_x0) {
                sx0 = card_x0;
            }
            if (sy0 < card_y0) {
                sy0 = card_y0;
            }
            if (sx1 > card_x1) {
                sx1 = card_x1;
            }
            if (sy1 > card_y1) {
                sy1 = card_y1;
            }
            if (sx1 <= sx0 || sy1 <= sy0) {
                continue;
            }

            /* Per-cmd scissor lives in screen-space (SetScissorRect takes
             * absolute framebuffer pixels). sx0/sy0 above are pane-local
             * (card_origin + clip rect). Shift by (scissor_off_x, _off_y)
             * so the scissor and the GPU viewport agree on what "y=0"
             * means — without this, after viewport-confine to the pane
             * rect the scissor stays at framebuffer origin and clips the
             * bottom of the card by exactly the strip's pixel height. */
            float ssx0 = sx0 + scissor_off_x;
            float ssy0 = sy0 + scissor_off_y;
            float ssx1 = sx1 + scissor_off_x;
            float ssy1 = sy1 + scissor_off_y;
            wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)ssx0, (uint32_t)ssy0,
                                                (uint32_t)(ssx1 - ssx0),
                                                (uint32_t)(ssy1 - ssy0));

            wgpuRenderPassEncoderDrawIndexed(pass, dc->elem_count, 1,
                                             (uint32_t)cl->idx_u32_offset + dc->idx_offset,
                                             (int32_t)(base_vtx_idx + dc->vtx_offset), 0);
        }
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymgui_render(struct yetty_yrender_terminal_layer *self,
                                                   struct yetty_ydraw_target *target)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    if (!target || !target->ops || !target->ops->get_view) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: target has no get_view");
    }

    if (!l->pipeline_ready) {
        if (!build_pipeline(l)) {
            return YETTY_ERR(yetty_ycore_void, "ymgui: pipeline build failed");
        }
        for (size_t i = 0; i < l->card_count; i++) {
            rebuild_card_bind_group(l, l->cards[i]);
        }
    }

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "ymgui: target view is NULL");
    }

    /* LoadOp_Load: every pass into the shared big target preserves prior
     * pixels. The single per-frame wipe is the global clear() in
     * yetty_event_handler. ymgui draws on top of whatever earlier layers
     * (text, ydraw, shader-glyph) put down. */
    WGPUCommandEncoderDescriptor ed = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(l->device, &ed);

    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0, 0, 0, 0};
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pd = {0};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pd);

    wgpuRenderPassEncoderSetPipeline(pass, l->pipeline);

    /* Confine the layer's draws to its pane's rect — same as text/ydraw
     * get via render_target_texture_render_layer's SetViewport call.
     * Without this, the ymgui pipeline draws into the whole framebuffer
     * (default viewport = full texture), so card vertices at pane-local
     * (0, 0) render at framebuffer (0, 0) — under the yui tabbar strip,
     * which overlays them and produces the "top few pixels missing"
     * symptom. pane_render in yui/tile.c writes the pane bounds into
     * target->viewport before delegating to us. */
    struct yetty_yrender_viewport vp = target->viewport;
    if (vp.w > 0.0f && vp.h > 0.0f) {
        wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.w, vp.h, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)vp.x, (uint32_t)vp.y,
                                            (uint32_t)vp.w, (uint32_t)vp.h);
    }

    float pane_w = (float)l->base.grid_size.cols * l->base.cell_size.width;
    float pane_h = (float)l->base.grid_size.rows * l->base.cell_size.height;

    /* Older cards first, newer ones on top — matches z-order convention. */
    for (size_t i = 0; i < l->card_count; i++) {
        struct yetty_ycore_void_result r =
            draw_card(l, l->cards[i], pass, pane_w, pane_h, vp.x, vp.y);
        if (YETTY_IS_ERR(r)) {
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return YETTY_ERR(yetty_ycore_void, "ymgui_render: draw_card failed", r);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cd = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cd);
    wgpuQueueSubmit(l->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    self->dirty = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Misc ops
 *=========================================================================*/

static int ymgui_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_ymgui_layer *l = (const struct yetty_yterm_ymgui_layer *)self;
    if (l->card_count == 0) {
        return 1;
    }
    for (size_t i = 0; i < l->card_count; i++) {
        const struct yetty_yterm_ymgui_card *c = l->cards[i];
        if (c->has_frame && card_visible(l, c)) {
            return 0;
        }
    }
    return 1;
}

/* Keyboard routing happens in terminal.c (terminal owns emit_yface and
 * the focused-card lookup). The layer ops just say "not consumed" so
 * the text-layer can take the events when no card has focus. */
static int ymgui_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    (void)self;
    (void)key;
    (void)mods;
    return 0;
}

static int ymgui_on_char(struct yetty_yrender_terminal_layer *self, uint32_t cp, int mods)
{
    (void)self;
    (void)cp;
    (void)mods;
    return 0;
}

static struct yetty_ycore_void_result ymgui_scroll(struct yetty_yrender_terminal_layer *self,
                                                   int lines)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    if (lines <= 0) {
        return YETTY_OK_VOID();
    }
    l->row0_absolute += (uint32_t)lines;
    self->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymgui_set_cursor(struct yetty_yrender_terminal_layer *self,
                                                       int col, int row)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    if (col < 0) {
        col = 0;
    }
    if (row < 0) {
        row = 0;
    }
    l->cursor_col = (uint32_t)col;
    l->cursor_row = (uint32_t)row;
    return YETTY_OK_VOID();
}

/* Alt-screen entry/exit: swap the live cards[] with the saved set so
 * the entering session gets a fresh empty screen and the exiting one
 * restores the previously-saved state. The GPU resources tied to each
 * card (atlas, buffers, bind group) ride along with the card pointers
 * — no GPU work needed at toggle time. */
static struct yetty_ycore_void_result ymgui_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    int wanted = active ? 1 : 0;
    if (l->alt_active == wanted) {
        return YETTY_OK_VOID();
    }

    /* Drop focus emission for the about-to-be-saved card so the client
     * gets a clean focus-lost. We don't restore focus on the way back —
     * focus is a transient runtime fact, not a state to preserve. */
    if (l->focused_card_id) {
        struct yetty_ycore_void_result ef = emit_focus(l, l->focused_card_id, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ef,
                            "ymgui_set_alt_screen: emit_focus(saved) failed");
        l->focused_card_id = 0;
    }

    struct yetty_yterm_ymgui_card **tmp_cards = l->cards;
    size_t tmp_card_cnt = l->card_count;
    size_t tmp_card_cap = l->card_cap;
    uint32_t tmp_focused = l->focused_card_id;

    l->cards = l->saved_cards;
    l->card_count = l->saved_card_count;
    l->card_cap = l->saved_card_cap;
    l->focused_card_id = l->saved_focused_card_id;

    l->saved_cards = tmp_cards;
    l->saved_card_count = tmp_card_cnt;
    l->saved_card_cap = tmp_card_cap;
    l->saved_focused_card_id = tmp_focused;

    l->alt_active = wanted;
    self->dirty = 1;
    if (self->request_render_fn) {
        struct yetty_ycore_void_result rr =
            self->request_render_fn(self->request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "ymgui_set_alt_screen: request_render_fn failed");
    }

    ydebug("ymgui: alt_screen=%d (live=%zu cards, saved=%zu cards)", wanted, l->card_count,
           l->saved_card_count);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Create / destroy
 *=========================================================================*/

struct yetty_yterm_terminal_layer_result yetty_yterm_ymgui_layer_create(
    uint32_t cols, uint32_t rows, float cell_w, float cell_h, const struct yetty_context *context,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata)
{
    if (!context) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "context is NULL");
    }
    if (!context->gpu_context.device || !context->gpu_context.queue) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "gpu context is incomplete");
    }

    struct yetty_yconfig_config *cfg = context->app_context.config;
    const char *shaders_dir = cfg->ops->get_string(cfg, "paths/shaders", "");
    char shader_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/ymgui-layer.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_yterm_terminal_layer,
                         "ymgui_layer_create: read_file(ymgui-layer.wgsl) failed", shader_res);
    }

    struct yetty_yterm_ymgui_layer *l = calloc(1, sizeof(*l));
    if (!l) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, "alloc failed");
    }
    l->shader_code = shader_res.value;

    l->base.ops = &ymgui_ops;
    l->base.grid_size.cols = cols;
    l->base.grid_size.rows = rows;
    l->base.cell_size.width = cell_w;
    l->base.cell_size.height = cell_h;
    l->base.dirty = 0;
    l->base.request_render_fn = request_render_fn;
    l->base.request_render_userdata = request_render_userdata;
    l->base.scroll_fn = scroll_fn;
    l->base.scroll_userdata = scroll_userdata;
    l->base.cursor_fn = cursor_fn;
    l->base.cursor_userdata = cursor_userdata;

    l->device = context->gpu_context.device;
    l->queue = context->gpu_context.queue;
    l->target_format = context->gpu_context.surface_format;

    {
        struct yetty_yface_ptr_result yr = yetty_yface_create();
        if (YETTY_IS_ERR(yr)) {
            free(l->shader_code.data);
            free(l);
            return YETTY_ERR(yetty_yterm_terminal_layer,
                             "ymgui_layer_create: yetty_yface_create failed", yr);
        }
        l->yface = yr.value;
    }

    ydebug("ymgui_layer_create: %ux%u grid, %.1fx%.1f cell, format=%u", cols, rows, cell_w, cell_h,
           (unsigned)l->target_format);

    return YETTY_OK(yetty_yterm_terminal_layer, &l->base);
}

static struct yetty_ycore_void_result ymgui_destroy(struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)self;
    if (!l) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_destroy: NULL layer");
    }
    for (size_t i = 0; i < l->card_count; i++) {
        card_destroy(l->cards[i]);
    }
    free(l->cards);
    for (size_t i = 0; i < l->saved_card_count; i++) {
        card_destroy(l->saved_cards[i]);
    }
    free(l->saved_cards);
    release_pipeline(l);
    if (l->yface) {
        yetty_yface_destroy(l->yface);
    }
    free(l->accum.data);
    free(l->shader_code.data);
    free(l);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API for terminal.c — hit-test / focus
 *=========================================================================*/

struct yetty_yterm_ymgui_hit yetty_yterm_terminal_layer_ymgui_layer_hit_test(
    const struct yetty_yrender_terminal_layer *layer, float px, float py)
{
    struct yetty_yterm_ymgui_hit h = {0, 0, 0};
    if (!layer || layer->ops != &ymgui_ops) {
        return h;
    }
    const struct yetty_yterm_ymgui_layer *l = (const struct yetty_yterm_ymgui_layer *)layer;

    /* Newest card first — last-rendered = topmost. */
    for (size_t i = l->card_count; i > 0; i--) {
        const struct yetty_yterm_ymgui_card *c = l->cards[i - 1];
        if (!card_visible(l, c)) {
            continue;
        }
        float ox = card_origin_x(l, c);
        float oy = card_origin_y(l, c);
        float w = card_pixel_w(l, c);
        float ch = card_pixel_h(l, c);
        if (px >= ox && px < ox + w && py >= oy && py < oy + ch) {
            h.card_id = c->id;
            h.local_x = px - ox;
            h.local_y = py - oy;
            return h;
        }
    }
    return h;
}

uint32_t yetty_yterm_terminal_layer_ymgui_layer_focused_card(
    const struct yetty_yrender_terminal_layer *layer)
{
    if (!layer || layer->ops != &ymgui_ops) {
        return 0;
    }
    const struct yetty_yterm_ymgui_layer *l = (const struct yetty_yterm_ymgui_layer *)layer;
    return l->focused_card_id;
}

struct yetty_ycore_void_result yetty_yterm_terminal_layer_ymgui_layer_set_focus(
    struct yetty_yrender_terminal_layer *layer, uint32_t card_id)
{
    if (!layer) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_ymgui_layer_set_focus: layer is NULL");
    }
    if (layer->ops != &ymgui_ops) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yterm_terminal_layer_ymgui_layer_set_focus: layer is not a ymgui layer");
    }
    struct yetty_yterm_ymgui_layer *l = (struct yetty_yterm_ymgui_layer *)layer;
    if (l->focused_card_id == card_id) {
        return YETTY_OK_VOID();
    }

    /* Validate that card_id refers to a live card (or 0). Unknown id is
     * silently ignored — the focus model is purely advisory. */
    if (card_id != 0 && !card_find(l, card_id)) {
        return YETTY_OK_VOID();
    }

    if (l->focused_card_id != 0) {
        struct yetty_ycore_void_result lost = emit_focus(l, l->focused_card_id, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lost,
                            "ymgui_layer_set_focus: emit_focus(lost) failed");
    }
    l->focused_card_id = card_id;
    if (card_id != 0) {
        struct yetty_ycore_void_result gained = emit_focus(l, card_id, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gained,
                            "ymgui_layer_set_focus: emit_focus(gained) failed");
    }
    return YETTY_OK_VOID();
}
