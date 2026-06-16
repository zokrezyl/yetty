/*
 * figure.c — one Dear-ImGui frame as a compositor figure.
 *
 * Owns: frame bytes (denormalized per <yetty/ymgui/wire.h>), R8 atlas
 * texture, per-instance vtx/idx/uniform buffers, bind group.
 *
 * Borrows: yetty_ymgui_pipeline * (shared shader + sampler + render
 * pipeline + bind group layout). Lifetime is the host's problem.
 *
 * Position: the figure-base rect is absolute target pixel space, set by
 * the compositor via yfigure container set_rect on move/resize.
 * The render path reads it for the viewport + scissor + frame-origin
 * uniform. Vertex coords inside the frame stay in their authored
 * frame-local pixel space; the shader translates by frame_top and
 * normalizes by display_size (read from the frame header).
 *
 * This TU deliberately does NOT include its own generated public header
 * `yetty/ymgui/figure.h` — that header is a downstream artifact for other
 * modules. The foundational types it needs (yclass identity, Result,
 * rectangle) are pulled in directly here, and this TU declares its own
 * `yetty_ymgui_figure_ptr_result` below (the same one figure.h publishes
 * for consumers). The figure base type comes from the parent header
 * `yfigure/figure.h`.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

#include "pipeline.h"

/*===========================================================================
 * Figure struct
 *
 * The ymgui-figure data slice sits AFTER the figure-base slice in the
 * shared yclass object. It has no cached back-pointer to the base or the
 * owning object: the base figure is reached through the codegen downcast
 * `yetty_yfigure_figure_from(obj)`, and the figure-base property
 * accessors (yetty_yfigure_figure_<field>_get/_set) take the owning
 * object pointer directly.
 *=========================================================================*/

struct [[clang::annotate("class@ymgui:figure")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_ymgui_figure {
    /* Borrowed — shared shader/pipeline/sampler. */
    struct yetty_ymgui_pipeline *pipeline;

    /* Decoded ImGui frame. Layout: yetty_ymgui_wire_frame header followed
     * by cmd_list_count × (cmd_list_hdr + vtx + idx + cmds). Owned. */
    uint8_t *frame_bytes;
    size_t frame_size;
    int has_frame;

    /* Font atlas (R8). Owned. */
    int atlas_ready;
    uint32_t atlas_w;
    uint32_t atlas_h;
    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;

    /* Per-instance GPU buffers. Owned. */
    WGPUBuffer uniform_buffer; /* 32 B */
    WGPUBuffer vtx_buf;
    size_t vtx_buf_capacity;
    WGPUBuffer idx_buf;
    size_t idx_buf_capacity;
    WGPUBindGroup bind_group; /* rebuilt when atlas changes */
};

/* Result wrapper for the ymgui-figure handle. Declared here (not pulled
 * from figure.h, which this TU does not include) so the appended
 * figure.gen.c — which defines yetty_ymgui_figure_from() returning it —
 * has the type in scope. The public figure.h publishes the identical
 * declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_ymgui_figure_ptr, struct yetty_ymgui_figure *);

/* The shared factory args struct is public (hosts embed it by value and
 * hand its address to the registry). `expose` makes codegen re-emit this
 * full definition into the generated figure.h for consumers; this TU has
 * its own copy and the two never share a translation unit. */
struct [[clang::annotate("expose")]] yetty_ymgui_factory_args {
    /* Borrowed — used to lazily build `pipeline` on first mint. The
     * underlying context (GPU device / queue / config) must outlive the
     * args struct. */
    const struct yetty_context *context;

    /* Owned by the args once built. NULL until the first factory mint
     * triggers `yetty_ymgui_pipeline_create`. Host MUST call
     * `yetty_ymgui_factory_args_release` at shutdown to free it. */
    struct yetty_ymgui_pipeline *pipeline;
};

/* Defined in the appended figure.gen.c (foot of this TU). Forward-
 * declared here because this TU does not include its own generated
 * header — the class accessor and the obj→body downcast are used by the
 * helpers and the object-keyed public API below. */
struct yetty_yclass_ptr_result yetty_ymgui_figure_class_get(void);
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_from(struct yetty_yclass_object *obj);

/* Object-keyed public setters, defined further down but reached from the
 * wire-routing helpers above their definition. */
struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(struct yetty_yclass_object *obj,
                                                            const uint8_t *frame_bytes,
                                                            size_t frame_size);
struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(struct yetty_yclass_object *obj,
                                                            const uint8_t *atlas_bytes,
                                                            size_t atlas_size, uint32_t atlas_w,
                                                            uint32_t atlas_h);

/* This kind's own data slice (its fields sit after the figure
 * base slice in the shared yclass object). */
static struct yetty_yclass_void_ptr_result ymgui_figure_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ymgui_figure_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "ymgui_figure_from_obj: class");
    return yetty_yclass_object_data(obj, class_r.value);
}

/*===========================================================================
 * GPU helpers — lifted from the old ymgui-layer, stripped of all card
 * placement / rolling-row plumbing.
 *=========================================================================*/

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

static int ensure_uniform_buffer(struct yetty_ymgui_figure *f)
{
    if (f->uniform_buffer) {
        return 1;
    }
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
    if (!f->atlas_ready || !f->uniform_buffer) {
        return;
    }
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

static int frame_measure(const struct yetty_ymgui_figure *f, size_t *out_vtx_bytes,
                         size_t *out_idx_bytes, int *out_idx32)
{
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)f->frame_bytes;
    const uint8_t *cur = f->frame_bytes + sizeof(*fh);
    const uint8_t *end = f->frame_bytes + f->frame_size;
    int idx32 = (fh->flags & YMGUI_FRAME_FLAG_IDX32) ? 1 : 0;
    size_t idx_bpe = idx32 ? 4u : 2u;

    size_t total_vtx = 0;
    size_t total_idx_bytes = 0;
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

/* idx_stage is a per-call scratch — sized up the moment a 16-bit
 * cmd-list arrives and we have to widen. Caller owns lifetime. */
static int frame_upload(struct yetty_ymgui_figure *f, struct cl_offsets *cls, size_t cls_max,
                        size_t *cls_count, int idx32, uint32_t **idx_stage, size_t *idx_stage_cap)
{
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)f->frame_bytes;
    const uint8_t *cur = f->frame_bytes + sizeof(*fh);
    const uint8_t *end = f->frame_bytes + f->frame_size;
    size_t idx_bpe = idx32 ? 4u : 2u;
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
            wgpuQueueWriteBuffer(f->pipeline->queue, f->vtx_buf, vtx_off, vtx, vbytes);
        }

        size_t i32_bytes = (size_t)clh->idx_count * 4u;
        if (i32_bytes) {
            if (idx32) {
                wgpuQueueWriteBuffer(f->pipeline->queue, f->idx_buf, idx_off_u32 * 4u, idx,
                                     i32_bytes);
            } else {
                if (*idx_stage_cap < clh->idx_count) {
                    free(*idx_stage);
                    *idx_stage = (uint32_t *)malloc((size_t)clh->idx_count * sizeof(uint32_t));
                    *idx_stage_cap = clh->idx_count;
                    if (!*idx_stage) {
                        return 0;
                    }
                }
                const uint16_t *src = (const uint16_t *)idx;
                for (uint32_t i = 0; i < clh->idx_count; i++) {
                    (*idx_stage)[i] = src[i];
                }
                wgpuQueueWriteBuffer(f->pipeline->queue, f->idx_buf, idx_off_u32 * 4u, *idx_stage,
                                     i32_bytes);
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

static struct yetty_ycore_void_result ymgui_figure_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "ymgui_figure_destroy: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;
    if (f->bind_group) {
        wgpuBindGroupRelease(f->bind_group);
    }
    if (f->atlas_view) {
        wgpuTextureViewRelease(f->atlas_view);
    }
    if (f->atlas_texture) {
        wgpuTextureDestroy(f->atlas_texture);
        wgpuTextureRelease(f->atlas_texture);
    }
    if (f->uniform_buffer) {
        wgpuBufferRelease(f->uniform_buffer);
    }
    if (f->vtx_buf) {
        wgpuBufferRelease(f->vtx_buf);
    }
    if (f->idx_buf) {
        wgpuBufferRelease(f->idx_buf);
    }
    free(f->frame_bytes);
    /* Free the yclass allocation (header + every slice). */
    return yetty_yclass_object_free(obj);
}

static struct yetty_ycore_void_result ymgui_figure_render(struct yetty_yclass_object *obj,
                                                          struct yetty_ydraw_target *target)
{
    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "ymgui_figure_render: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;
    ydebug("ymgui_figure_render: has_frame=%d atlas_ready=%d rect=(%.1f,%.1f)-(%.1f,%.1f)",
           f->has_frame, f->atlas_ready, yetty_yfigure_figure_rect_get(obj).value.min.x,
           yetty_yfigure_figure_rect_get(obj).value.min.y,
           yetty_yfigure_figure_rect_get(obj).value.max.x,
           yetty_yfigure_figure_rect_get(obj).value.max.y);
    if (!f->has_frame || !f->atlas_ready) {
        return YETTY_OK_VOID();
    }

    if (!ensure_uniform_buffer(f)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: uniform buffer alloc failed");
    }
    if (!f->bind_group) {
        rebuild_bind_group(f);
    }
    if (!f->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: bind group not ready");
    }

    size_t total_vtx_bytes = 0;
    size_t total_idx_bytes = 0;
    int idx32 = 0;
    if (!frame_measure(f, &total_vtx_bytes, &total_idx_bytes, &idx32)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: frame layout invalid");
    }
    if (total_vtx_bytes == 0 || total_idx_bytes == 0) {
        return YETTY_OK_VOID();
    }

    if (!ensure_buffer(f->pipeline->device, &f->vtx_buf, &f->vtx_buf_capacity, total_vtx_bytes,
                       WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: vtx alloc");
    }
    if (!ensure_buffer(f->pipeline->device, &f->idx_buf, &f->idx_buf_capacity, total_idx_bytes,
                       WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: idx alloc");
    }

    enum { MAX_CL = 32 };
    struct cl_offsets cls[MAX_CL];
    size_t cls_count = 0;
    uint32_t *idx_stage = NULL;
    size_t idx_stage_cap = 0;
    int ok = frame_upload(f, cls, MAX_CL, &cls_count, idx32, &idx_stage, &idx_stage_cap);
    free(idx_stage);
    if (!ok) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: upload");
    }

    /* Vertex coords are frame-local pixels in [0, display_size].
     * The render pass sets a per-figure viewport (below) that already
     * maps NDC [-1, +1] onto the figure's absolute target rect, so the
     * shader only needs to normalize against display_size. frame_top
     * stays zero — the legacy "translate vertices by card origin"
     * trick was a substitute for not having a viewport. */
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)f->frame_bytes;
    float frame_w = fh->display_size_x;
    float frame_h = fh->display_size_y;
    float ox = yetty_yfigure_figure_rect_get(obj).value.min.x;
    float oy = yetty_yfigure_figure_rect_get(obj).value.min.y;
    float uniforms[8] = {frame_w, frame_h, 0.0f, 0.0f, 0, 0, 0, 0};
    wgpuQueueWriteBuffer(f->pipeline->queue, f->uniform_buffer, 0, uniforms, sizeof(uniforms));

    /* Begin a render pass for this figure. Load existing pixels so we
     * compose on top of whatever the compositor's earlier figures drew. */
    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_render: NULL view");
    }

    WGPUCommandEncoderDescriptor ed = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(f->pipeline->device, &ed);

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

    wgpuRenderPassEncoderSetPipeline(pass, f->pipeline->pipeline);

    /* Viewport is the figure's rect in absolute target pixel space.
     * target->viewport tells us the pane the compositor draws into;
     * we honour it as an outer clamp. */
    struct yetty_yrender_viewport vp = target->viewport;
    float fig_w = yetty_yfigure_figure_rect_get(obj).value.max.x -
                  yetty_yfigure_figure_rect_get(obj).value.min.x;
    float fig_h = yetty_yfigure_figure_rect_get(obj).value.max.y -
                  yetty_yfigure_figure_rect_get(obj).value.min.y;
    if (fig_w > 0.0f && fig_h > 0.0f) {
        wgpuRenderPassEncoderSetViewport(pass, ox, oy, fig_w, fig_h, 0.0f, 1.0f);
    }
    /* Outer scissor = figure rect intersected with target viewport. */
    float sx0 = ox > vp.x ? ox : vp.x;
    float sy0 = oy > vp.y ? oy : vp.y;
    float vp_max_x = vp.x + vp.w;
    float vp_max_y = vp.y + vp.h;
    float fx1 = yetty_yfigure_figure_rect_get(obj).value.max.x;
    float fy1 = yetty_yfigure_figure_rect_get(obj).value.max.y;
    float sx1 = fx1 < vp_max_x ? fx1 : vp_max_x;
    float sy1 = fy1 < vp_max_y ? fy1 : vp_max_y;
    if (sx1 <= sx0 || sy1 <= sy0) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(enc);
        return YETTY_OK_VOID();
    }

    wgpuRenderPassEncoderSetBindGroup(pass, 0, f->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, f->vtx_buf, 0, total_vtx_bytes);
    wgpuRenderPassEncoderSetIndexBuffer(pass, f->idx_buf, WGPUIndexFormat_Uint32, 0,
                                        total_idx_bytes);

    /* Iterate cmd-lists × cmds. Cmd clip rects are in frame-local
     * pixels (ImGui DisplayPos=(0,0)); translate to absolute by adding
     * the figure's origin, then clamp to the figure's visible rect. */
    for (size_t i = 0; i < cls_count; i++) {
        const struct cl_offsets *cl = &cls[i];
        uint32_t base_vtx_idx = (uint32_t)(cl->vtx_byte_offset / 20u);
        for (uint32_t k = 0; k < cl->cmd_count; k++) {
            const struct yetty_ymgui_wire_cmd *dc = &cl->cmds[k];
            if (dc->elem_count == 0) {
                continue;
            }

            float cx0 = ox + dc->clip_min_x;
            float cy0 = oy + dc->clip_min_y;
            float cx1 = ox + dc->clip_max_x;
            float cy1 = oy + dc->clip_max_y;
            if (cx0 < sx0) {
                cx0 = sx0;
            }
            if (cy0 < sy0) {
                cy0 = sy0;
            }
            if (cx1 > sx1) {
                cx1 = sx1;
            }
            if (cy1 > sy1) {
                cy1 = sy1;
            }
            if (cx1 <= cx0 || cy1 <= cy0) {
                continue;
            }

            wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)cx0, (uint32_t)cy0,
                                                (uint32_t)(cx1 - cx0), (uint32_t)(cy1 - cy0));
            wgpuRenderPassEncoderDrawIndexed(pass, dc->elem_count, 1,
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

static struct yetty_ycore_void_result handle_frame_payload(struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len)
{
    return yetty_ymgui_figure_set_frame(obj, bytes, bytes_len);
}

static struct yetty_ycore_void_result handle_tex_payload(struct yetty_yclass_object *obj,
                                                         const uint8_t *bytes, size_t bytes_len)
{
    if (bytes_len < sizeof(struct yetty_ymgui_wire_tex)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure: tex payload too small");
    }
    const struct yetty_ymgui_wire_tex *th = (const struct yetty_ymgui_wire_tex *)bytes;
    if (th->total_size != bytes_len) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure: tex total_size mismatch");
    }
    if (th->format != YMGUI_TEX_FMT_R8) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure: tex format != R8 unsupported");
    }
    const uint8_t *pixels = bytes + sizeof(*th);
    size_t pixel_bytes = (size_t)th->width * (size_t)th->height;
    return yetty_ymgui_figure_set_atlas(obj, pixels, pixel_bytes, th->width, th->height);
}

/*===========================================================================
 * Streaming entry point: process_input(obj, sm).
 *
 * The figure pumps the wire-statemachine directly, yielding the coro
 * whenever there are no bytes ready. Self-describing payloads — the
 * sub-op tag dictates how many bytes follow, and the per-op struct
 * headers (frame.total_size, tex.{width,height}) bound the rest. The
 * container that dispatched us is trusted to have passed the right
 * record boundary; we consume exactly that many bytes by following the
 * sub-op's own size fields.
 *=========================================================================*/

static struct yetty_ycore_void_result ymgui_sm_read_exact(struct yetty_ywire_wire_statemachine *sm,
                                                          void *out, size_t n)
{
    uint8_t *p = (uint8_t *)out;
    size_t got = 0;
    while (got < n) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, p + got, n - got);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ymgui sm_read_exact");
        if (rr.value == 0) {
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                return YETTY_ERR(yetty_ycore_void, "ymgui sm_read_exact: EOE mid-read");
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        got += rr.value;
    }
    return YETTY_OK_VOID();
}

/* Read a FRAME body off the SM. `tag` was peeked by the caller; the
 * frame header `magic` may already equal `tag` (legacy magic-prefixed
 * path) or be a fresh struct following a tagged sub_op. In both cases
 * the frame's `total_size` covers the header + cmd lists; we read the
 * remainder of the header first, then the body, then apply. */
static struct yetty_ycore_void_result stream_frame(struct yetty_yclass_object *obj,
                                                   struct yetty_ywire_wire_statemachine *sm,
                                                   int tag_is_magic)
{
    struct yetty_ymgui_wire_frame fh;
    if (tag_is_magic) {
        fh.magic = YMGUI_WIRE_MAGIC_FRAME;
        struct yetty_ycore_void_result r = ymgui_sm_read_exact(
            sm, ((uint8_t *)&fh) + sizeof(uint32_t), sizeof(fh) - sizeof(uint32_t));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "stream_frame: header rest");
    } else {
        struct yetty_ycore_void_result r = ymgui_sm_read_exact(sm, &fh, sizeof(fh));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "stream_frame: header");
        if (fh.magic != YMGUI_WIRE_MAGIC_FRAME) {
            return YETTY_ERR(yetty_ycore_void, "stream_frame: header magic mismatch");
        }
    }
    if (fh.total_size < sizeof(fh)) {
        return YETTY_ERR(yetty_ycore_void, "stream_frame: total_size < header");
    }

    size_t body_bytes = fh.total_size - sizeof(fh);
    uint8_t *buf = malloc(fh.total_size);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "stream_frame: oom");
    }
    memcpy(buf, &fh, sizeof(fh));
    if (body_bytes) {
        struct yetty_ycore_void_result r = ymgui_sm_read_exact(sm, buf + sizeof(fh), body_bytes);
        if (YETTY_IS_ERR(r)) {
            free(buf);
            return YETTY_ERR(yetty_ycore_void, "stream_frame: body", r);
        }
    }
    struct yetty_ycore_void_result rr = yetty_ymgui_figure_set_frame(obj, buf, fh.total_size);
    free(buf);
    return rr;
}

static struct yetty_ycore_void_result stream_tex(struct yetty_yclass_object *obj,
                                                 struct yetty_ywire_wire_statemachine *sm,
                                                 int tag_is_magic)
{
    struct yetty_ymgui_wire_tex th;
    if (tag_is_magic) {
        th.magic = YMGUI_WIRE_MAGIC_TEX;
        struct yetty_ycore_void_result r = ymgui_sm_read_exact(
            sm, ((uint8_t *)&th) + sizeof(uint32_t), sizeof(th) - sizeof(uint32_t));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "stream_tex: header rest");
    } else {
        struct yetty_ycore_void_result r = ymgui_sm_read_exact(sm, &th, sizeof(th));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "stream_tex: header");
        if (th.magic != YMGUI_WIRE_MAGIC_TEX) {
            return YETTY_ERR(yetty_ycore_void, "stream_tex: header magic mismatch");
        }
    }
    if (th.format != YMGUI_TEX_FMT_R8) {
        return YETTY_ERR(yetty_ycore_void, "stream_tex: format != R8 unsupported");
    }

    size_t pixel_bytes = (size_t)th.width * (size_t)th.height;
    uint8_t *pixels = malloc(pixel_bytes ? pixel_bytes : 1);
    if (!pixels) {
        return YETTY_ERR(yetty_ycore_void, "stream_tex: oom");
    }
    if (pixel_bytes) {
        struct yetty_ycore_void_result r = ymgui_sm_read_exact(sm, pixels, pixel_bytes);
        if (YETTY_IS_ERR(r)) {
            free(pixels);
            return YETTY_ERR(yetty_ycore_void, "stream_tex: pixels", r);
        }
    }
    struct yetty_ycore_void_result rr =
        yetty_ymgui_figure_set_atlas(obj, pixels, pixel_bytes, th.width, th.height);
    free(pixels);
    return rr;
}

static struct yetty_ycore_void_result ymgui_figure_process_input(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "ymgui_figure_process_input: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;
    uint32_t tag;
    struct yetty_ycore_void_result r = ymgui_sm_read_exact(sm, &tag, sizeof(tag));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymgui process_input: tag");

    switch (tag) {
    /* Legacy magic-prefixed bodies — first u32 IS the wire struct's
     * magic. Common in producers that don't use the SUB_* tagging. */
    case YMGUI_WIRE_MAGIC_FRAME:
        return stream_frame(obj, sm, /*tag_is_magic=*/1);
    case YMGUI_WIRE_MAGIC_TEX:
        return stream_tex(obj, sm, /*tag_is_magic=*/1);

    /* Tagged sub-records — first u32 is the sub-op enum; the wire
     * struct follows with its own magic field. */
    case YETTY_YMGUI_FIGURE_SUB_FRAME:
        return stream_frame(obj, sm, /*tag_is_magic=*/0);
    case YETTY_YMGUI_FIGURE_SUB_TEX_UPLOAD:
        return stream_tex(obj, sm, /*tag_is_magic=*/0);
    case YETTY_YMGUI_FIGURE_SUB_CLEAR:
        free(f->frame_bytes);
        f->frame_bytes = NULL;
        f->frame_size = 0;
        f->has_frame = 0;
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "drop: yetty_yfigure_figure_dirty_set");
        }
        return YETTY_OK_VOID();
    case YETTY_YMGUI_FIGURE_SUB_TEX_RELEASE:
    case YETTY_YMGUI_FIGURE_SUB_TERM_INPUT_SUB:
        ydebug("ymgui process_input: sub_op=%u not yet implemented", tag);
        return YETTY_OK_VOID();
    default:
        return YETTY_ERR(yetty_ycore_void, "ymgui process_input: unknown tag");
    }
}

/* Two payload shapes coexist during the figure-tree migration:
 *
 *   (a) Legacy magic-prefixed bodies — the first u32 is one of
 *       YMGUI_WIRE_MAGIC_FRAME / _TEX and the rest is the full wire
 *       struct (its own header carries the size). New producers may
 *       still emit these for ad-hoc routing.
 *
 *   (b) Tagged sub-records — the first u32 is a YETTY_YMGUI_FIGURE_SUB_*
 *       enum value, followed by the same struct + payload. This is the
 *       form the figure-tree producer in the C++ frontend emits when
 *       run under YMGUI_USE_FIGURE_TREE=1.
 *
 * Both share the same per-payload dispatch — only the discriminator
 * width differs. */
static struct yetty_ycore_void_result ymgui_figure_process_bytes(struct yetty_yclass_object *obj,
                                                                 const uint8_t *bytes,
                                                                 size_t bytes_len)
{
    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "ymgui_figure_process_bytes: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;
    if (bytes_len < 4) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_process_bytes: too small for tag");
    }

    uint32_t tag;
    memcpy(&tag, bytes, 4);

    if (tag == YMGUI_WIRE_MAGIC_FRAME) {
        return handle_frame_payload(obj, bytes, bytes_len);
    }
    if (tag == YMGUI_WIRE_MAGIC_TEX) {
        return handle_tex_payload(obj, bytes, bytes_len);
    }

    /* Tagged sub-record — body starts at byte 4. */
    const uint8_t *body = bytes + 4;
    size_t body_len = bytes_len - 4;
    switch (tag) {
    case YETTY_YMGUI_FIGURE_SUB_FRAME:
        return handle_frame_payload(obj, body, body_len);
    case YETTY_YMGUI_FIGURE_SUB_TEX_UPLOAD:
        return handle_tex_payload(obj, body, body_len);
    case YETTY_YMGUI_FIGURE_SUB_CLEAR:
        /* "Drop the visible frame" — represented by no-frame state. The
         * figure stays in the tree until the parent DELETE_CHILD removes
         * it; this just blanks its rect. */
        free(f->frame_bytes);
        f->frame_bytes = NULL;
        f->frame_size = 0;
        f->has_frame = 0;
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "drop: yetty_yfigure_figure_dirty_set");
        }
        return YETTY_OK_VOID();
    case YETTY_YMGUI_FIGURE_SUB_TEX_RELEASE:
    case YETTY_YMGUI_FIGURE_SUB_TERM_INPUT_SUB:
        ydebug("ymgui_figure_process_bytes: sub_op=%u not yet implemented", tag);
        return YETTY_OK_VOID();
    default:
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_process_bytes: unknown tag");
    }
}

/*===========================================================================
 * Cross-domain yfigure slot overrides. Each takes the owning yclass
 * object and forwards to the object-keyed impl above.
 *=========================================================================*/

[[clang::annotate("override@ymgui:figure:yfigure:render")]]
static struct yetty_ycore_void_result ymgui_figure_render_slot(struct yetty_yclass_object *obj,
                                                               struct yetty_ydraw_target *target)
{
    return ymgui_figure_render(obj, target);
}

[[clang::annotate("override@ymgui:figure:yfigure:destroy")]]
static struct yetty_ycore_void_result ymgui_figure_destroy_slot(struct yetty_yclass_object *obj)
{
    return ymgui_figure_destroy(obj);
}

[[clang::annotate("override@ymgui:figure:yfigure:process_input")]]
static struct yetty_ycore_void_result ymgui_figure_process_input_slot(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *statemachine)
{
    return ymgui_figure_process_input(obj, statemachine);
}

[[clang::annotate("override@ymgui:figure:yfigure:process_bytes")]]
static struct yetty_ycore_void_result ymgui_figure_process_bytes_slot(
    struct yetty_yclass_object *obj, const uint8_t *bytes, size_t bytes_len)
{
    return ymgui_figure_process_bytes(obj, bytes, bytes_len);
}

/*===========================================================================
 * Lifecycle / public API
 *=========================================================================*/

/* Allocate a fresh ymgui figure as a yclass object and initialise its
 * rect / dirty flag. Returns the owning object; the figure base is the
 * first slice (obj + 1), the ymgui data slice follows. */
static struct yetty_yclass_object_ptr_result ymgui_figure_create_object(
    struct yetty_ycore_rectangle rect, struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context)
{
    if (!pipeline) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymgui_figure_create: NULL pipeline");
    }
    if (!context) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymgui_figure_create: NULL context");
    }

    struct yetty_yclass_ptr_result figure_class_r = yetty_ymgui_figure_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, figure_class_r,
                        "ymgui_figure_create: figure class");
    struct yetty_yclass_object_ptr_result figure_obj_r =
        yetty_yclass_object_alloc(figure_class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, figure_obj_r, "ymgui_figure_create: object_alloc");
    struct yetty_yclass_object *obj = figure_obj_r.value;

    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, figure_r, "ymgui_figure_create: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;

    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(obj, rect);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, set_r, "drop: yetty_yfigure_figure_rect_set");
    }
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    f->pipeline = pipeline;
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

[[clang::annotate("expose")]]
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create_local(
    struct yetty_ycore_rectangle rect, struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context)
{
    struct yetty_yclass_object_ptr_result obj_r =
        ymgui_figure_create_object(rect, pipeline, context);
    YETTY_RETURN_IF_ERR(yetty_ymgui_figure_ptr, obj_r, "ymgui_figure_create_local: create");
    return yetty_ymgui_figure_from(obj_r.value);
}

[[clang::annotate("expose")]]
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_from_base(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ymgui_figure_ptr, "yetty_ymgui_figure_from_base: NULL object");
    }
    struct yetty_yclass_ptr_result cls_r = yetty_ymgui_figure_class_get();
    if (YETTY_IS_ERR(cls_r)) {
        return YETTY_ERR(yetty_ymgui_figure_ptr, "yetty_ymgui_figure_from_base: class", cls_r);
    }
    if (obj->klass != cls_r.value) {
        /* obj is not an ymgui figure — a valid downcast miss, not an error. */
        return YETTY_OK(yetty_ymgui_figure_ptr, NULL);
    }
    return yetty_ymgui_figure_from(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(struct yetty_yclass_object *obj,
                                                            const uint8_t *frame_bytes,
                                                            size_t frame_size)
{
    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "ymgui_figure_set_frame: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;
    if (!frame_bytes || frame_size == 0) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_frame: NULL/empty arg");
    }
    if (frame_size < sizeof(struct yetty_ymgui_wire_frame)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_frame: frame too small for header");
    }
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)frame_bytes;
    if (fh->magic != YMGUI_WIRE_MAGIC_FRAME) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_frame: bad magic");
    }
    if (fh->version != YMGUI_WIRE_VERSION) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_frame: version mismatch");
    }
    if (fh->total_size != frame_size) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_frame: total_size mismatch");
    }

    uint8_t *copy = malloc(frame_size);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_frame: oom");
    }
    memcpy(copy, frame_bytes, frame_size);
    free(f->frame_bytes);
    f->frame_bytes = copy;
    f->frame_size = frame_size;
    f->has_frame = 1;
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(struct yetty_yclass_object *obj,
                                                            const uint8_t *atlas_bytes,
                                                            size_t atlas_size, uint32_t atlas_w,
                                                            uint32_t atlas_h)
{
    struct yetty_yclass_void_ptr_result figure_r = ymgui_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "ymgui_figure_set_atlas: from_obj");
    struct yetty_ymgui_figure *f = figure_r.value;
    if (!atlas_bytes || atlas_w == 0 || atlas_h == 0) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_atlas: NULL/empty arg");
    }
    if (atlas_size != (size_t)atlas_w * (size_t)atlas_h) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_atlas: size mismatch (R8)");
    }

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
    if (!f->atlas_texture) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure_set_atlas: texture create failed");
    }

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
    wgpuQueueWriteTexture(f->pipeline->queue, &dest, atlas_bytes, atlas_size, &src_layout, &extent);

    f->atlas_w = atlas_w;
    f->atlas_h = atlas_h;
    f->atlas_ready = 1;
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    ydebug("ymgui_figure_set_atlas: %ux%u R8", atlas_w, atlas_h);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Factory — invoked by yetty_yfigure_registry_mint on admin CREATE_CHILD
 * records with kind=YMGUI. `user` is a borrowed `yetty_ymgui_factory_args*`.
 * The pipeline is shared across every minted figure; the first mint
 * builds it and the host releases it via yetty_ymgui_factory_args_release.
 *=========================================================================*/

YETTY_EXTERNAL_CALLBACK
static struct yetty_yfigure_figure_ptr_result ymgui_factory(struct yetty_ycore_rectangle rect,
                                                            const struct yetty_context *context,
                                                            void *user)
{
    struct yetty_ymgui_factory_args *args = (struct yetty_ymgui_factory_args *)user;
    if (!args) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "ymgui_factory: NULL factory args");
    }

    if (!args->pipeline) {
        /* Lazy build — first ymgui figure on this host. Prefer the
         * registry-supplied context (the container hands us the host's
         * context at mint time); fall back to the args' stashed context
         * for tooling that registers without a context. */
        const struct yetty_context *ctx = context ? context : args->context;
        if (!ctx) {
            return YETTY_ERR(yetty_yfigure_figure_ptr,
                             "ymgui_factory: no context to build pipeline");
        }
        struct yetty_ymgui_pipeline_ptr_result pr = yetty_ymgui_pipeline_create(ctx);
        YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, pr, "ymgui_factory: pipeline create");
        args->pipeline = pr.value;
    }

    struct yetty_yclass_object_ptr_result obj_r =
        ymgui_figure_create_object(rect, args->pipeline, context);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, obj_r, "ymgui_factory: figure create");
    /* The figure base slice is the first slice in the object. */
    struct yetty_yfigure_figure_ptr_result base_r = yetty_yfigure_figure_from(obj_r.value);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, base_r, "ymgui_factory: figure base");
    return YETTY_OK(yetty_yfigure_figure_ptr, base_r.value);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ymgui_register_factory(struct yetty_yfigure_registry *registry,
                                                            struct yetty_ymgui_factory_args *args)
{
    if (!registry || !args) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymgui_register_factory: NULL arg");
    }
    return yetty_yfigure_registry_register(registry, YETTY_YFIGURE_KIND_YMGUI, ymgui_factory, args);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ymgui_factory_args_release(
    struct yetty_ymgui_factory_args *args)
{
    if (!args) {
        return YETTY_OK_VOID();
    }
    if (args->pipeline) {
        struct yetty_ycore_void_result r = yetty_ymgui_pipeline_destroy(args->pipeline);
        args->pipeline = NULL;
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "yetty_ymgui_factory_args_release: pipeline destroy");
    }
    return YETTY_OK_VOID();
}

/* yclass class accessor + slot table + obj→body downcast, generated from
 * the annotations above. */
#include "figure.gen.c"
