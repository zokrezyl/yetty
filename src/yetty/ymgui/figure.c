/*
 * figure.c — one Dear-ImGui frame as a compositor figure.
 *
 * Owns: frame bytes (denormalized per <yetty/ymgui/wire.h>), R8 atlas
 * texture, per-instance vtx/idx/uniform buffers, bind group.
 *
 * Borrows: yetty_ymgui_pipeline * (shared shader + sampler + render
 * pipeline + bind group layout). Lifetime is the host's problem.
 *
 * Position: figure->rect is absolute target pixel space, set by the
 * compositor via yfigure container set_rect on move/resize.
 * The render path reads it for the viewport + scissor + frame-origin
 * uniform. Vertex coords inside the frame stay in their authored
 * frame-local pixel space; the shader translates by frame_top and
 * normalizes by display_size (read from the frame header).
 */
#include <yetty/ymgui/figure.h>

#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

#include "pipeline.h"

/*===========================================================================
 * Figure struct
 *=========================================================================*/

struct yetty_ymgui_figure {
    struct yetty_yfigure_figure base;

    /* Borrowed — shared shader/pipeline/sampler. */
    struct yetty_ymgui_pipeline *pipeline;

    /* Decoded ImGui frame. Layout: yetty_ymgui_wire_frame header followed
     * by cmd_list_count × (cmd_list_hdr + vtx + idx + cmds). Owned. */
    uint8_t *frame_bytes;
    size_t   frame_size;
    int      has_frame;

    /* Font atlas (R8). Owned. */
    int                  atlas_ready;
    uint32_t             atlas_w;
    uint32_t             atlas_h;
    WGPUTexture          atlas_texture;
    WGPUTextureView      atlas_view;

    /* Per-instance GPU buffers. Owned. */
    WGPUBuffer    uniform_buffer; /* 32 B */
    WGPUBuffer    vtx_buf;
    size_t        vtx_buf_capacity;
    WGPUBuffer    idx_buf;
    size_t        idx_buf_capacity;
    WGPUBindGroup bind_group; /* rebuilt when atlas changes */
};

/*===========================================================================
 * GPU helpers — lifted from the old ymgui-layer, stripped of all card
 * placement / rolling-row plumbing.
 *=========================================================================*/

static int ensure_buffer(WGPUDevice dev, WGPUBuffer *buf, size_t *cap,
                         size_t need, WGPUBufferUsage usage)
{
    if (need <= *cap && *buf) return 1;
    if (*buf) {
        wgpuBufferRelease(*buf);
        *buf = NULL;
    }
    size_t new_cap = need + need / 4u;
    if (new_cap < 4096) new_cap = 4096;
    new_cap = (new_cap + 3u) & ~(size_t)3u;
    WGPUBufferDescriptor bd = {0};
    bd.size = new_cap;
    bd.usage = usage;
    *buf = wgpuDeviceCreateBuffer(dev, &bd);
    if (!*buf) return 0;
    *cap = new_cap;
    return 1;
}

static int ensure_uniform_buffer(struct yetty_ymgui_figure *f)
{
    if (f->uniform_buffer) return 1;
    WGPUBufferDescriptor ub = {0};
    ub.size = 32;
    ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    f->uniform_buffer = wgpuDeviceCreateBuffer(f->pipeline->device, &ub);
    return f->uniform_buffer != NULL;
}

static void rebuild_bind_group(struct yetty_ymgui_figure *f)
{
    if (f->bind_group) {
        wgpuBindGroupRelease(f->bind_group);
        f->bind_group = NULL;
    }
    if (!f->atlas_ready || !f->uniform_buffer) return;
    WGPUBindGroupEntry e[3] = {0};
    e[0].binding = 0;
    e[0].buffer = f->uniform_buffer;
    e[0].size = 32;
    e[1].binding = 1;
    e[1].textureView = f->atlas_view;
    e[2].binding = 2;
    e[2].sampler = f->pipeline->sampler;
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = f->pipeline->bind_group_layout;
    bgd.entryCount = 3;
    bgd.entries = e;
    f->bind_group = wgpuDeviceCreateBindGroup(f->pipeline->device, &bgd);
}

/* Per-cmd-list offsets inside the figure's packed vtx/idx buffers.
 * Populated by frame_upload; consumed by figure_draw to issue
 * DrawIndexed per cmd. */
struct cl_offsets {
    size_t vtx_byte_offset;
    size_t idx_u32_offset;
    uint32_t cmd_count;
    const struct yetty_ymgui_wire_cmd *cmds;
    uint32_t vtx_count;
};

static int frame_measure(const struct yetty_ymgui_figure *f,
                         size_t *out_vtx_bytes, size_t *out_idx_bytes,
                         int *out_idx32)
{
    const struct yetty_ymgui_wire_frame *fh =
        (const struct yetty_ymgui_wire_frame *)f->frame_bytes;
    const uint8_t *cur = f->frame_bytes + sizeof(*fh);
    const uint8_t *end = f->frame_bytes + f->frame_size;
    int idx32 = (fh->flags & YMGUI_FRAME_FLAG_IDX32) ? 1 : 0;
    size_t idx_bpe = idx32 ? 4u : 2u;

    size_t total_vtx = 0;
    size_t total_idx_bytes = 0;
    for (uint32_t li = 0; li < fh->cmd_list_count; li++) {
        if (cur + sizeof(struct yetty_ymgui_wire_cmd_list) > end) return 0;
        const struct yetty_ymgui_wire_cmd_list *clh =
            (const struct yetty_ymgui_wire_cmd_list *)cur;
        cur += sizeof(*clh);
        size_t vbytes = (size_t)clh->vtx_count * 20u;
        cur += vbytes;
        if (cur > end) return 0;
        size_t ibytes_padded = (size_t)clh->idx_count * idx_bpe;
        if (ibytes_padded & 3u) ibytes_padded += 4u - (ibytes_padded & 3u);
        cur += ibytes_padded;
        if (cur > end) return 0;
        cur += (size_t)clh->cmd_count * sizeof(struct yetty_ymgui_wire_cmd);
        if (cur > end) return 0;
        total_vtx += vbytes;
        total_idx_bytes += (size_t)clh->idx_count * 4u;
    }
    *out_vtx_bytes = total_vtx;
    *out_idx_bytes = total_idx_bytes;
    *out_idx32 = idx32;
    return 1;
}

/* idx_stage is a per-call scratch — sized up the moment a 16-bit
 * cmd-list arrives and we have to widen. Caller owns lifetime. */
static int frame_upload(struct yetty_ymgui_figure *f, struct cl_offsets *cls,
                        size_t cls_max, size_t *cls_count, int idx32,
                        uint32_t **idx_stage, size_t *idx_stage_cap)
{
    const struct yetty_ymgui_wire_frame *fh =
        (const struct yetty_ymgui_wire_frame *)f->frame_bytes;
    const uint8_t *cur = f->frame_bytes + sizeof(*fh);
    const uint8_t *end = f->frame_bytes + f->frame_size;
    size_t idx_bpe = idx32 ? 4u : 2u;
    size_t vtx_off = 0;
    size_t idx_off_u32 = 0;
    size_t n = 0;

    for (uint32_t li = 0; li < fh->cmd_list_count; li++) {
        if (n >= cls_max) break;
        const struct yetty_ymgui_wire_cmd_list *clh =
            (const struct yetty_ymgui_wire_cmd_list *)cur;
        cur += sizeof(*clh);
        const uint8_t *vtx = cur;
        size_t vbytes = (size_t)clh->vtx_count * 20u;
        cur += vbytes;
        const uint8_t *idx = cur;
        size_t ibytes_padded = (size_t)clh->idx_count * idx_bpe;
        if (ibytes_padded & 3u) ibytes_padded += 4u - (ibytes_padded & 3u);
        cur += ibytes_padded;
        const struct yetty_ymgui_wire_cmd *cmds =
            (const struct yetty_ymgui_wire_cmd *)cur;
        cur += (size_t)clh->cmd_count * sizeof(struct yetty_ymgui_wire_cmd);
        if (cur > end) return 0;

        if (vbytes)
            wgpuQueueWriteBuffer(f->pipeline->queue, f->vtx_buf,
                                 vtx_off, vtx, vbytes);

        size_t i32_bytes = (size_t)clh->idx_count * 4u;
        if (i32_bytes) {
            if (idx32) {
                wgpuQueueWriteBuffer(f->pipeline->queue, f->idx_buf,
                                     idx_off_u32 * 4u, idx, i32_bytes);
            } else {
                if (*idx_stage_cap < clh->idx_count) {
                    free(*idx_stage);
                    *idx_stage = (uint32_t *)malloc(
                        (size_t)clh->idx_count * sizeof(uint32_t));
                    *idx_stage_cap = clh->idx_count;
                    if (!*idx_stage) return 0;
                }
                const uint16_t *src = (const uint16_t *)idx;
                for (uint32_t i = 0; i < clh->idx_count; i++)
                    (*idx_stage)[i] = src[i];
                wgpuQueueWriteBuffer(f->pipeline->queue, f->idx_buf,
                                     idx_off_u32 * 4u, *idx_stage, i32_bytes);
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

/*===========================================================================
 * Figure ops
 *=========================================================================*/

static struct yetty_ycore_void_result ymgui_figure_destroy(
    struct yetty_yfigure_figure *self)
{
    struct yetty_ymgui_figure *f = (struct yetty_ymgui_figure *)self;
    if (!f) return YETTY_OK_VOID();
    if (f->bind_group) wgpuBindGroupRelease(f->bind_group);
    if (f->atlas_view) wgpuTextureViewRelease(f->atlas_view);
    if (f->atlas_texture) {
        wgpuTextureDestroy(f->atlas_texture);
        wgpuTextureRelease(f->atlas_texture);
    }
    if (f->uniform_buffer) wgpuBufferRelease(f->uniform_buffer);
    if (f->vtx_buf) wgpuBufferRelease(f->vtx_buf);
    if (f->idx_buf) wgpuBufferRelease(f->idx_buf);
    free(f->frame_bytes);
    free(f);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymgui_figure_render(
    struct yetty_yfigure_figure *self, struct yetty_ydraw_target *target)
{
    struct yetty_ymgui_figure *f = (struct yetty_ymgui_figure *)self;
    ydebug("ymgui_figure_render: has_frame=%d atlas_ready=%d rect=(%.1f,%.1f)-(%.1f,%.1f)",
           f->has_frame, f->atlas_ready,
           self->rect.min.x, self->rect.min.y,
           self->rect.max.x, self->rect.max.y);
    if (!f->has_frame || !f->atlas_ready) return YETTY_OK_VOID();

    if (!ensure_uniform_buffer(f))
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_render: uniform buffer alloc failed");
    if (!f->bind_group) rebuild_bind_group(f);
    if (!f->bind_group)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_render: bind group not ready");

    size_t total_vtx_bytes = 0;
    size_t total_idx_bytes = 0;
    int idx32 = 0;
    if (!frame_measure(f, &total_vtx_bytes, &total_idx_bytes, &idx32))
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_render: frame layout invalid");
    if (total_vtx_bytes == 0 || total_idx_bytes == 0)
        return YETTY_OK_VOID();

    if (!ensure_buffer(f->pipeline->device, &f->vtx_buf, &f->vtx_buf_capacity,
                       total_vtx_bytes,
                       WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst))
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: vtx alloc");
    if (!ensure_buffer(f->pipeline->device, &f->idx_buf, &f->idx_buf_capacity,
                       total_idx_bytes,
                       WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst))
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: idx alloc");

    enum { MAX_CL = 32 };
    struct cl_offsets cls[MAX_CL];
    size_t cls_count = 0;
    uint32_t *idx_stage = NULL;
    size_t idx_stage_cap = 0;
    int ok = frame_upload(f, cls, MAX_CL, &cls_count, idx32,
                          &idx_stage, &idx_stage_cap);
    free(idx_stage);
    if (!ok)
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: upload");

    /* Frame's own display size (what the vertices are encoded against)
     * comes from the wire header. frame_top is the figure's absolute
     * top-left in target pixels — moving the figure just moves this. */
    const struct yetty_ymgui_wire_frame *fh =
        (const struct yetty_ymgui_wire_frame *)f->frame_bytes;
    float frame_w = fh->display_size_x;
    float frame_h = fh->display_size_y;
    float ox = self->rect.min.x;
    float oy = self->rect.min.y;
    float uniforms[8] = {frame_w, frame_h, ox, oy, 0, 0, 0, 0};
    wgpuQueueWriteBuffer(f->pipeline->queue, f->uniform_buffer, 0,
                         uniforms, sizeof(uniforms));

    /* Begin a render pass for this figure. Load existing pixels so we
     * compose on top of whatever the compositor's earlier figures drew. */
    WGPUTextureView view = target->ops->get_view(target);
    if (!view)
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: NULL view");

    WGPUCommandEncoderDescriptor ed = {0};
    WGPUCommandEncoder enc =
        wgpuDeviceCreateCommandEncoder(f->pipeline->device, &ed);

    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0, 0, 0, 0};
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pd = {0};
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &ca;
    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(enc, &pd);

    wgpuRenderPassEncoderSetPipeline(pass, f->pipeline->pipeline);

    /* Viewport is the figure's rect in absolute target pixel space.
     * target->viewport tells us the pane the compositor draws into;
     * we honour it as an outer clamp. */
    struct yetty_yrender_viewport vp = target->viewport;
    float fig_w = self->rect.max.x - self->rect.min.x;
    float fig_h = self->rect.max.y - self->rect.min.y;
    if (fig_w > 0.0f && fig_h > 0.0f) {
        wgpuRenderPassEncoderSetViewport(pass, ox, oy, fig_w, fig_h,
                                         0.0f, 1.0f);
    }
    /* Outer scissor = figure rect intersected with target viewport. */
    float sx0 = ox > vp.x ? ox : vp.x;
    float sy0 = oy > vp.y ? oy : vp.y;
    float vp_max_x = vp.x + vp.w;
    float vp_max_y = vp.y + vp.h;
    float fx1 = self->rect.max.x;
    float fy1 = self->rect.max.y;
    float sx1 = fx1 < vp_max_x ? fx1 : vp_max_x;
    float sy1 = fy1 < vp_max_y ? fy1 : vp_max_y;
    if (sx1 <= sx0 || sy1 <= sy0) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(enc);
        return YETTY_OK_VOID();
    }

    wgpuRenderPassEncoderSetBindGroup(pass, 0, f->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, f->vtx_buf, 0,
                                         total_vtx_bytes);
    wgpuRenderPassEncoderSetIndexBuffer(pass, f->idx_buf,
                                        WGPUIndexFormat_Uint32, 0,
                                        total_idx_bytes);

    /* Iterate cmd-lists × cmds. Cmd clip rects are in frame-local
     * pixels (ImGui DisplayPos=(0,0)); translate to absolute by adding
     * the figure's origin, then clamp to the figure's visible rect. */
    for (size_t i = 0; i < cls_count; i++) {
        const struct cl_offsets *cl = &cls[i];
        uint32_t base_vtx_idx = (uint32_t)(cl->vtx_byte_offset / 20u);
        for (uint32_t k = 0; k < cl->cmd_count; k++) {
            const struct yetty_ymgui_wire_cmd *dc = &cl->cmds[k];
            if (dc->elem_count == 0) continue;

            float cx0 = ox + dc->clip_min_x;
            float cy0 = oy + dc->clip_min_y;
            float cx1 = ox + dc->clip_max_x;
            float cy1 = oy + dc->clip_max_y;
            if (cx0 < sx0) cx0 = sx0;
            if (cy0 < sy0) cy0 = sy0;
            if (cx1 > sx1) cx1 = sx1;
            if (cy1 > sy1) cy1 = sy1;
            if (cx1 <= cx0 || cy1 <= cy0) continue;

            wgpuRenderPassEncoderSetScissorRect(
                pass, (uint32_t)cx0, (uint32_t)cy0,
                (uint32_t)(cx1 - cx0), (uint32_t)(cy1 - cy0));
            wgpuRenderPassEncoderDrawIndexed(
                pass, dc->elem_count, 1,
                (uint32_t)cl->idx_u32_offset + dc->idx_offset,
                (int32_t)(base_vtx_idx + dc->vtx_offset), 0);
        }
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cd = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cd);
    wgpuQueueSubmit(f->pipeline->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * process_input — consume EXACTLY `payload_bytes` from the SM and apply
 * to the figure. The payload's first u32 is the magic word of the
 * embedded ymgui wire struct (FRAME or TEX); we use it to dispatch.
 *
 * Reading happens in chunks; when wire_statemachine_read returns 0
 * (no bytes ready yet), the coroutine yields and resumes on the next
 * SM dispatch. The caller (parent figure / compositor) has already
 * pre-determined this figure as the routing target.
 *=========================================================================*/

static struct yetty_ycore_void_result ymgui_figure_process_bytes(
    struct yetty_yfigure_figure *self,
    const uint8_t *bytes, size_t bytes_len)
{
    struct yetty_ymgui_figure *f = (struct yetty_ymgui_figure *)self;
    if (bytes_len < 4)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_process_bytes: too small for magic");

    uint32_t magic;
    memcpy(&magic, bytes, 4);
    if (magic == YMGUI_WIRE_MAGIC_FRAME) {
        return yetty_ymgui_figure_set_frame(f, bytes, bytes_len);
    }
    if (magic == YMGUI_WIRE_MAGIC_TEX) {
        if (bytes_len < sizeof(struct yetty_ymgui_wire_tex))
            return YETTY_ERR(yetty_ycore_void,
                             "ymgui_figure_process_bytes: tex too small");
        const struct yetty_ymgui_wire_tex *th =
            (const struct yetty_ymgui_wire_tex *)bytes;
        if (th->total_size != bytes_len)
            return YETTY_ERR(yetty_ycore_void,
                             "ymgui_figure_process_bytes: tex total_size mismatch");
        if (th->format != YMGUI_TEX_FMT_R8)
            return YETTY_ERR(yetty_ycore_void,
                             "ymgui_figure_process_bytes: tex format != R8 unsupported");
        const uint8_t *pixels = bytes + sizeof(*th);
        size_t pixel_bytes = (size_t)th->width * (size_t)th->height;
        return yetty_ymgui_figure_set_atlas(f, pixels, pixel_bytes,
                                            th->width, th->height);
    }
    return YETTY_ERR(yetty_ycore_void,
                     "ymgui_figure_process_bytes: unknown magic");
}

/*===========================================================================
 * Lifecycle / public API
 *=========================================================================*/

struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create(
    struct yetty_ycore_rectangle rect,
    struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context)
{
    if (!pipeline)
        return YETTY_ERR(yetty_ymgui_figure_ptr,
                         "ymgui_figure_create: NULL pipeline");
    if (!context)
        return YETTY_ERR(yetty_ymgui_figure_ptr,
                         "ymgui_figure_create: NULL context");

    struct yetty_ymgui_figure *f = calloc(1, sizeof(*f));
    if (!f)
        return YETTY_ERR(yetty_ymgui_figure_ptr,
                         "ymgui_figure_create: oom");

    static const struct yetty_yfigure_figure_ops ops = {
        .destroy = ymgui_figure_destroy,
        .render = ymgui_figure_render,
        .process_bytes = ymgui_figure_process_bytes,
    };
    f->base.ops = &ops;
    f->base.rect = rect;
    f->base.dirty = 1;
    f->pipeline = pipeline;
    return YETTY_OK(yetty_ymgui_figure_ptr, f);
}

struct yetty_yfigure_figure *yetty_ymgui_figure_as_figure(
    struct yetty_ymgui_figure *f)
{
    return &f->base;
}

struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(
    struct yetty_ymgui_figure *f, const uint8_t *frame_bytes,
    size_t frame_size)
{
    if (!f || !frame_bytes || frame_size == 0)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_frame: NULL/empty arg");
    if (frame_size < sizeof(struct yetty_ymgui_wire_frame))
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_frame: frame too small for header");
    const struct yetty_ymgui_wire_frame *fh =
        (const struct yetty_ymgui_wire_frame *)frame_bytes;
    if (fh->magic != YMGUI_WIRE_MAGIC_FRAME)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_frame: bad magic");
    if (fh->version != YMGUI_WIRE_VERSION)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_frame: version mismatch");
    if (fh->total_size != frame_size)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_frame: total_size mismatch");

    uint8_t *copy = malloc(frame_size);
    if (!copy)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_frame: oom");
    memcpy(copy, frame_bytes, frame_size);
    free(f->frame_bytes);
    f->frame_bytes = copy;
    f->frame_size = frame_size;
    f->has_frame = 1;
    f->base.dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(
    struct yetty_ymgui_figure *f, const uint8_t *atlas_bytes,
    size_t atlas_size, uint32_t atlas_w, uint32_t atlas_h)
{
    if (!f || !atlas_bytes || atlas_w == 0 || atlas_h == 0)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_atlas: NULL/empty arg");
    if (atlas_size != (size_t)atlas_w * (size_t)atlas_h)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_atlas: size mismatch (R8)");

    /* Drop old atlas + bind group; they'll be rebuilt on next render. */
    if (f->bind_group) {
        wgpuBindGroupRelease(f->bind_group);
        f->bind_group = NULL;
    }
    if (f->atlas_view) {
        wgpuTextureViewRelease(f->atlas_view);
        f->atlas_view = NULL;
    }
    if (f->atlas_texture) {
        wgpuTextureDestroy(f->atlas_texture);
        wgpuTextureRelease(f->atlas_texture);
        f->atlas_texture = NULL;
    }
    f->atlas_ready = 0;

    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = atlas_w;
    td.size.height = atlas_h;
    td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_R8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    f->atlas_texture = wgpuDeviceCreateTexture(f->pipeline->device, &td);
    if (!f->atlas_texture)
        return YETTY_ERR(yetty_ycore_void,
                         "ymgui_figure_set_atlas: texture create failed");

    WGPUTextureViewDescriptor vd = {0};
    vd.format = WGPUTextureFormat_R8Unorm;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    f->atlas_view = wgpuTextureCreateView(f->atlas_texture, &vd);

    WGPUTexelCopyTextureInfo dest = {0};
    dest.texture = f->atlas_texture;
    WGPUTexelCopyBufferLayout src_layout = {0};
    src_layout.bytesPerRow = atlas_w;
    src_layout.rowsPerImage = atlas_h;
    WGPUExtent3D extent = {atlas_w, atlas_h, 1};
    wgpuQueueWriteTexture(f->pipeline->queue, &dest, atlas_bytes, atlas_size,
                          &src_layout, &extent);

    f->atlas_w = atlas_w;
    f->atlas_h = atlas_h;
    f->atlas_ready = 1;
    f->base.dirty = 1;
    ydebug("ymgui_figure_set_atlas: %ux%u R8", atlas_w, atlas_h);
    return YETTY_OK_VOID();
}
