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
#include "yetty/gen/impl/yfigure/figure.h"
#include <yetty/yfigure/registry.h>
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

struct YETTY_ANNOTATE("class@ymgui:figure") YETTY_ANNOTATE("parent@yfigure:figure")
    yetty_ymgui_figure {
    /* Borrowed — shared shader/pipeline/sampler. */
    struct yetty_ymgui_pipeline *pipeline;

    /* Decoded ImGui frame, fully denormalized: every cmd_list slot is
     * inlined (slots the wire delivered as REPEAT or CMD_DIFF were
     * rehydrated from the previous frame at set_frame time). Layout:
     * yetty_ymgui_wire_frame header followed by cmd_list_count ×
     * (cmd_list_hdr + vtx + idx + cmds). Owned. */
    uint8_t *frame_bytes;
    size_t frame_size;
    int has_frame;

    /* Per-slot byte offsets / sizes within frame_bytes. Lets the next
     * frame fill in REPEAT slots from this frame without re-walking.
     * slot_offsets[i] points at the slot's cmd_list_hdr; slot_sizes[i]
     * covers cmd_list_hdr + vtx + idx (padded) + cmds. Owned. */
    size_t *slot_offsets;
    size_t *slot_sizes;
    size_t slot_count;

    /* Stage 2 (CMD_DIFF) per-slot bookkeeping. The three parallel arrays
     * slot_cmd_hashes[i], slot_cmd_vtx_counts[i], slot_cmd_orig_indices[i]
     * each have slot_cmd_counts[i] entries — one per non-empty cmd in
     * draw order. orig_indices points back into the slot's wire_cmd
     * array so a CMD_DIFF reference can locate the source cmd's
     * (vtx_offset, idx_offset, clip, tex, elem_count) directly. Owned. */
    uint64_t **slot_cmd_hashes;
    uint32_t **slot_cmd_vtx_counts;
    uint32_t **slot_cmd_orig_indices;
    uint32_t *slot_cmd_counts;

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
struct YETTY_ANNOTATE("expose") yetty_ymgui_factory_args {
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

/*===========================================================================
 * Wire dedup rehydration (REPEAT / CMD_DIFF)
 *
 * The frontend may compress a frame's cmd_list slots against the previous
 * frame (see <yetty/ymgui/wire.h>): REPEAT ships only the 16-byte slot
 * header, CMD_DIFF ships a draw-order hash list plus only the cmds whose
 * content changed. set_frame flattens all slot modes back to one canonical
 * layout (cmd_list_hdr + vtx + idx + cmds, flags=0) so frame_measure /
 * frame_upload / render never need to know the wire was compressed.
 *=========================================================================*/

/* FNV-1a 64-bit update. Identical to the frontend's fnv64_update so
 * Stage-2 hashes computed on both sides agree on the same content. */
static inline uint64_t ymgui_fnv64_update(uint64_t hash, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/* Mirror of the frontend's cmd_vtx_count — returns the number of
 * vertices a single cmd actually references inside its cmd_list's vtx
 * buffer, i.e. max(idx_slice) + 1. The cmd's vtx slice is then
 * [vtx_offset, vtx_offset + vtx_count). idx32 must match the frame's
 * IDX32 flag. */
static uint32_t ymgui_cmd_vtx_count(const uint8_t *idx_slice, uint32_t elem_count, int idx32)
{
    if (elem_count == 0) {
        return 0;
    }
    uint32_t max_idx = 0;
    if (idx32) {
        const uint32_t *indices = (const uint32_t *)idx_slice;
        for (uint32_t i = 0; i < elem_count; i++) {
            if (indices[i] > max_idx) {
                max_idx = indices[i];
            }
        }
    } else {
        const uint16_t *indices = (const uint16_t *)idx_slice;
        for (uint32_t i = 0; i < elem_count; i++) {
            if ((uint32_t)indices[i] > max_idx) {
                max_idx = (uint32_t)indices[i];
            }
        }
    }
    return max_idx + 1u;
}

/* Mirror of the frontend's hash_cmd. Same field order, same salts.
 * Operates on raw wire bytes — vtx_slice points at the start of the
 * cmd's vertex slice within the cmd_list's vtx buffer (already at
 * vtx_offset), idx_slice at the start of the cmd's index slice (already
 * at idx_offset). idx32 picks 2-byte vs 4-byte index size. */
static uint64_t ymgui_hash_cmd(const struct yetty_ymgui_wire_cmd *wire_cmd,
                               const uint8_t *vtx_slice, uint32_t vtx_count,
                               const uint8_t *idx_slice, int idx32)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    float clip[4] = {wire_cmd->clip_min_x, wire_cmd->clip_min_y, wire_cmd->clip_max_x,
                     wire_cmd->clip_max_y};
    uint32_t tex_id = wire_cmd->tex_id;
    uint32_t elem_count = wire_cmd->elem_count;
    uint32_t idx_size = idx32 ? 4u : 2u;
    hash = ymgui_fnv64_update(hash, clip, sizeof(clip));
    hash = ymgui_fnv64_update(hash, &tex_id, sizeof(tex_id));
    hash = ymgui_fnv64_update(hash, &elem_count, sizeof(elem_count));
    hash = ymgui_fnv64_update(hash, &vtx_count, sizeof(vtx_count));
    hash = ymgui_fnv64_update(hash, &idx_size, sizeof(idx_size));
    if (vtx_count) {
        hash = ymgui_fnv64_update(hash, vtx_slice, (size_t)vtx_count * 20u);
    }
    if (elem_count) {
        hash = ymgui_fnv64_update(hash, idx_slice, (size_t)elem_count * idx_size);
    }
    return hash;
}

/* Per-slot Stage-2 index. One entry per non-empty cmd in the slot, in
 * original draw order. `cmd_indices` points into the slot's wire_cmd
 * array so the reconstruction path can pull (clip, tex, elem_count,
 * vtx_offset, idx_offset) directly. Empty cmds are skipped here AND on
 * the frontend, so the indices line up across both sides. */
struct ymgui_slot_index {
    uint64_t *hashes;
    uint32_t *vtx_counts;
    uint32_t *cmd_indices;
    uint32_t count;
};

/* Compute the per-slot index for a freshly-stored slot. Used both for
 * SLOT_FULL receives (we just memcpy'd the wire body, now we also build
 * the per-cmd index so the next frame can reference us) and for
 * CMD_DIFF-reconstructed slots. Returns 0 on OOM. */
static int ymgui_index_slot_cmds(const uint8_t *slot_bytes, int idx32, struct ymgui_slot_index *out)
{
    const struct yetty_ymgui_wire_cmd_list *clh =
        (const struct yetty_ymgui_wire_cmd_list *)slot_bytes;
    size_t idx_bpe = idx32 ? 4u : 2u;
    size_t vbytes = (size_t)clh->vtx_count * 20u;
    size_t ibytes = (size_t)clh->idx_count * idx_bpe;
    if (ibytes & 3u) {
        ibytes += 4u - (ibytes & 3u);
    }
    const uint8_t *vtx_base = slot_bytes + sizeof(*clh);
    const uint8_t *idx_base = vtx_base + vbytes;
    const struct yetty_ymgui_wire_cmd *cmds =
        (const struct yetty_ymgui_wire_cmd *)(idx_base + ibytes);

    uint64_t *hashes = NULL;
    uint32_t *vtx_counts = NULL;
    uint32_t *cmd_indices = NULL;
    if (clh->cmd_count) {
        hashes = malloc((size_t)clh->cmd_count * sizeof(uint64_t));
        vtx_counts = malloc((size_t)clh->cmd_count * sizeof(uint32_t));
        cmd_indices = malloc((size_t)clh->cmd_count * sizeof(uint32_t));
        if (!hashes || !vtx_counts || !cmd_indices) {
            free(hashes);
            free(vtx_counts);
            free(cmd_indices);
            return 0;
        }
    }

    uint32_t kept = 0;
    for (uint32_t k = 0; k < clh->cmd_count; k++) {
        const struct yetty_ymgui_wire_cmd *wire_cmd = &cmds[k];
        if (wire_cmd->elem_count == 0) {
            continue;
        }
        const uint8_t *idx_slice = idx_base + (size_t)wire_cmd->idx_offset * idx_bpe;
        uint32_t vtx_count = ymgui_cmd_vtx_count(idx_slice, wire_cmd->elem_count, idx32);
        const uint8_t *vtx_slice = vtx_base + (size_t)wire_cmd->vtx_offset * 20u;
        hashes[kept] = ymgui_hash_cmd(wire_cmd, vtx_slice, vtx_count, idx_slice, idx32);
        vtx_counts[kept] = vtx_count;
        cmd_indices[kept] = k;
        kept++;
    }
    out->hashes = hashes;
    out->vtx_counts = vtx_counts;
    out->cmd_indices = cmd_indices;
    out->count = kept;
    return 1;
}

/* Snapshot of one cmd's resolved content during CMD_DIFF reconstruction.
 * Source bytes live elsewhere — these are non-owning pointers + counts
 * used to compute slot size in pass 1 and to memcpy in pass 2. */
struct ymgui_cmd_view {
    struct yetty_ymgui_wire_cmd wire_cmd; /* clip / tex / elem_count (vtx_offset
                                           * and idx_offset get reassigned when
                                           * packing the rebuilt slot) */
    uint32_t vtx_count;
    const uint8_t *vtx_src;
    const uint8_t *idx_src;
};

/* Slot resolution mode for the rehydration pass 1. */
enum ymgui_slot_mode {
    YMGUI_SLOT_FROM_WIRE = 0, /* SLOT_FULL: copy bytes from wire */
    YMGUI_SLOT_FROM_PREV = 1, /* SLOT_REPEAT: copy bytes from prev frame */
    YMGUI_SLOT_FROM_DIFF = 2, /* SLOT_CMD_DIFF: gather from inline+prev */
};

struct ymgui_slot_resolve {
    enum ymgui_slot_mode mode;
    /* WIRE / PREV: */
    size_t src_off;
    size_t size;
    /* DIFF: array of resolved cmd views (owned). */
    struct ymgui_cmd_view *cmd_views;
    uint32_t cmd_view_count;
};

static void ymgui_free_slot_resolves(struct ymgui_slot_resolve *resolves, size_t count)
{
    if (!resolves) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(resolves[i].cmd_views);
    }
    free(resolves);
}

/* Resolve a CMD_DIFF body into a draw-order array of cmd_views, each
 * pointing either into the wire (for inline entries) or into prev_slot
 * bytes (for cached entries). On success returns 0 and *out_consumed is
 * the # of wire bytes the body occupied (after the cmd_list_hdr). */
static int ymgui_resolve_cmd_diff(const uint8_t *wire_body, size_t wire_remaining,
                                  uint32_t expected_cmd_count, const uint8_t *prev_slot,
                                  const uint64_t *prev_hashes, const uint32_t *prev_vtx_counts,
                                  const uint32_t *prev_orig_indices, uint32_t prev_count, int idx32,
                                  struct ymgui_cmd_view **out_views, uint32_t *out_count,
                                  size_t *out_consumed)
{
    if (wire_remaining < 8) {
        return -1;
    }
    uint32_t hash_count = *(const uint32_t *)(wire_body + 0);
    uint32_t inline_count = *(const uint32_t *)(wire_body + 4);
    if (hash_count != expected_cmd_count) {
        return -1;
    }
    size_t off = 8;
    if (off + (size_t)hash_count * 8u > wire_remaining) {
        return -1;
    }
    const uint64_t *draw_hashes = (const uint64_t *)(wire_body + off);
    off += (size_t)hash_count * 8u;

    size_t idx_bpe = idx32 ? 4u : 2u;

    /* Index inline entries by hash so the draw-order walk below can look
     * them up. Linear search — frames have a few dozen cmds max. */
    struct {
        uint64_t hash;
        const struct yetty_ymgui_wire_cmd_inline *hdr;
        const struct yetty_ymgui_wire_cmd *cmd;
        const uint8_t *vtx;
        const uint8_t *idx;
    } *inlines = NULL;
    if (inline_count) {
        inlines = calloc(inline_count, sizeof(*inlines));
        if (!inlines) {
            return -1;
        }
    }
    for (uint32_t i = 0; i < inline_count; i++) {
        if (off + sizeof(struct yetty_ymgui_wire_cmd_inline) > wire_remaining) {
            free(inlines);
            return -1;
        }
        const struct yetty_ymgui_wire_cmd_inline *inline_hdr =
            (const struct yetty_ymgui_wire_cmd_inline *)(wire_body + off);
        off += sizeof(*inline_hdr);
        if (off + sizeof(struct yetty_ymgui_wire_cmd) > wire_remaining) {
            free(inlines);
            return -1;
        }
        const struct yetty_ymgui_wire_cmd *wire_cmd =
            (const struct yetty_ymgui_wire_cmd *)(wire_body + off);
        off += sizeof(*wire_cmd);
        size_t vbytes = (size_t)inline_hdr->vtx_count * 20u;
        size_t ibytes = (size_t)wire_cmd->elem_count * idx_bpe;
        if (ibytes & 3u) {
            ibytes += 4u - (ibytes & 3u);
        }
        if (off + vbytes + ibytes > wire_remaining) {
            free(inlines);
            return -1;
        }
        inlines[i].hash = inline_hdr->hash;
        inlines[i].hdr = inline_hdr;
        inlines[i].cmd = wire_cmd;
        inlines[i].vtx = wire_body + off;
        off += vbytes;
        inlines[i].idx = wire_body + off;
        off += ibytes;
    }

    /* Precompute prev slot's wire_cmd array + vtx/idx base pointers for
     * looking up cached cmds by original index. */
    const struct yetty_ymgui_wire_cmd *prev_cmds = NULL;
    const uint8_t *prev_vtx_base = NULL;
    const uint8_t *prev_idx_base = NULL;
    if (prev_slot) {
        const struct yetty_ymgui_wire_cmd_list *prev_clh =
            (const struct yetty_ymgui_wire_cmd_list *)prev_slot;
        size_t prev_vbytes = (size_t)prev_clh->vtx_count * 20u;
        size_t prev_ibytes = (size_t)prev_clh->idx_count * idx_bpe;
        if (prev_ibytes & 3u) {
            prev_ibytes += 4u - (prev_ibytes & 3u);
        }
        prev_vtx_base = prev_slot + sizeof(*prev_clh);
        prev_idx_base = prev_vtx_base + prev_vbytes;
        prev_cmds = (const struct yetty_ymgui_wire_cmd *)(prev_idx_base + prev_ibytes);
    }

    struct ymgui_cmd_view *views = NULL;
    if (hash_count) {
        views = calloc(hash_count, sizeof(*views));
        if (!views) {
            free(inlines);
            return -1;
        }
    }

    for (uint32_t k = 0; k < hash_count; k++) {
        uint64_t want = draw_hashes[k];
        int found = 0;
        /* Inline first — fresh content, no extraction needed. */
        for (uint32_t i = 0; i < inline_count; i++) {
            if (inlines[i].hash != want) {
                continue;
            }
            views[k].wire_cmd = *inlines[i].cmd;
            views[k].vtx_count = inlines[i].hdr->vtx_count;
            views[k].vtx_src = inlines[i].vtx;
            views[k].idx_src = inlines[i].idx;
            found = 1;
            break;
        }
        if (found) {
            continue;
        }
        /* Fall back to prev slot — locate by hash in the prev index. */
        for (uint32_t i = 0; i < prev_count; i++) {
            if (prev_hashes[i] != want) {
                continue;
            }
            uint32_t orig = prev_orig_indices[i];
            views[k].wire_cmd = prev_cmds[orig];
            views[k].vtx_count = prev_vtx_counts[i];
            views[k].vtx_src = prev_vtx_base + (size_t)prev_cmds[orig].vtx_offset * 20u;
            views[k].idx_src = prev_idx_base + (size_t)prev_cmds[orig].idx_offset * idx_bpe;
            found = 1;
            break;
        }
        if (!found) {
            free(inlines);
            free(views);
            return -1;
        }
    }

    free(inlines);
    *out_views = views;
    *out_count = hash_count;
    *out_consumed = off;
    return 0;
}

/* Compute the denormalized slot size given resolved cmd_views. Layout:
 *   cl_hdr (16) + vtx (concat) + idx_padded (concat) + wire_cmds. */
static size_t ymgui_diff_slot_size(const struct ymgui_cmd_view *views, uint32_t count, int idx32)
{
    size_t idx_bpe = idx32 ? 4u : 2u;
    size_t vtx_total = 0;
    size_t idx_total_elems = 0;
    for (uint32_t k = 0; k < count; k++) {
        vtx_total += (size_t)views[k].vtx_count;
        idx_total_elems += (size_t)views[k].wire_cmd.elem_count;
    }
    size_t vbytes = vtx_total * 20u;
    size_t ibytes = idx_total_elems * idx_bpe;
    if (ibytes & 3u) {
        ibytes += 4u - (ibytes & 3u);
    }
    return sizeof(struct yetty_ymgui_wire_cmd_list) + vbytes + ibytes +
           (size_t)count * sizeof(struct yetty_ymgui_wire_cmd);
}

/* Write a denormalized slot from resolved cmd_views into `dst`. Returns
 * # of bytes written. */
static size_t ymgui_write_diff_slot(uint8_t *dst, const struct ymgui_cmd_view *views,
                                    uint32_t count, int idx32)
{
    size_t idx_bpe = idx32 ? 4u : 2u;
    size_t vtx_total = 0;
    size_t idx_total_elems = 0;
    for (uint32_t k = 0; k < count; k++) {
        vtx_total += (size_t)views[k].vtx_count;
        idx_total_elems += (size_t)views[k].wire_cmd.elem_count;
    }
    size_t vbytes = vtx_total * 20u;
    size_t ibytes = idx_total_elems * idx_bpe;
    size_t ipadded = ibytes;
    if (ipadded & 3u) {
        ipadded += 4u - (ipadded & 3u);
    }

    struct yetty_ymgui_wire_cmd_list *out_clh = (struct yetty_ymgui_wire_cmd_list *)dst;
    out_clh->vtx_count = (uint32_t)vtx_total;
    out_clh->idx_count = (uint32_t)idx_total_elems;
    out_clh->cmd_count = count;
    out_clh->flags = 0;

    uint8_t *vtx_dst = dst + sizeof(*out_clh);
    uint8_t *idx_dst = vtx_dst + vbytes;
    struct yetty_ymgui_wire_cmd *cmd_dst = (struct yetty_ymgui_wire_cmd *)(idx_dst + ipadded);

    uint32_t vtx_cursor = 0;
    uint32_t idx_cursor = 0;
    for (uint32_t k = 0; k < count; k++) {
        const struct ymgui_cmd_view *view = &views[k];
        if (view->vtx_count) {
            memcpy(vtx_dst + (size_t)vtx_cursor * 20u, view->vtx_src,
                   (size_t)view->vtx_count * 20u);
        }
        if (view->wire_cmd.elem_count) {
            memcpy(idx_dst + (size_t)idx_cursor * idx_bpe, view->idx_src,
                   (size_t)view->wire_cmd.elem_count * idx_bpe);
        }
        cmd_dst[k] = view->wire_cmd;
        cmd_dst[k].vtx_offset = vtx_cursor;
        cmd_dst[k].idx_offset = idx_cursor;
        vtx_cursor += view->vtx_count;
        idx_cursor += view->wire_cmd.elem_count;
    }
    if (ipadded > ibytes) {
        memset(idx_dst + ibytes, 0, ipadded - ibytes);
    }
    return sizeof(*out_clh) + vbytes + ipadded + (size_t)count * sizeof(*cmd_dst);
}

/* Free the Stage-2 per-slot cmd index arrays (sized by slot_count). */
static void figure_release_slot_caches(struct yetty_ymgui_figure *figure)
{
    if (figure->slot_cmd_hashes) {
        for (size_t i = 0; i < figure->slot_count; i++) {
            free(figure->slot_cmd_hashes[i]);
        }
        free(figure->slot_cmd_hashes);
        figure->slot_cmd_hashes = NULL;
    }
    if (figure->slot_cmd_vtx_counts) {
        for (size_t i = 0; i < figure->slot_count; i++) {
            free(figure->slot_cmd_vtx_counts[i]);
        }
        free(figure->slot_cmd_vtx_counts);
        figure->slot_cmd_vtx_counts = NULL;
    }
    if (figure->slot_cmd_orig_indices) {
        for (size_t i = 0; i < figure->slot_count; i++) {
            free(figure->slot_cmd_orig_indices[i]);
        }
        free(figure->slot_cmd_orig_indices);
        figure->slot_cmd_orig_indices = NULL;
    }
    free(figure->slot_cmd_counts);
    figure->slot_cmd_counts = NULL;
}

/* Drop the stored frame and every rehydration cache derived from it. */
static void figure_drop_frame(struct yetty_ymgui_figure *figure)
{
    figure_release_slot_caches(figure);
    free(figure->frame_bytes);
    figure->frame_bytes = NULL;
    figure->frame_size = 0;
    figure->has_frame = 0;
    free(figure->slot_offsets);
    figure->slot_offsets = NULL;
    free(figure->slot_sizes);
    figure->slot_sizes = NULL;
    figure->slot_count = 0;
}

/* Walk a validated wire frame, rehydrate REPEAT / CMD_DIFF slots from the
 * figure's previous denormalized frame, and swap the result in as the new
 * canonical frame_bytes (+ per-slot caches for the NEXT frame's dedup).
 *
 *   REPEAT   — copy the slot's bytes verbatim from the prev frame.
 *   CMD_DIFF — gather cmds from inline entries (this wire) + previous
 *              slot's cached cmds (matched by content hash), then pack
 *              vtx | idx | cmds in draw order with fresh offsets.
 *   FULL     — copy the slot's bytes verbatim from the wire.
 *
 * In all three cases the per-cmd hashes are (re)computed after composing
 * the slot so the next frame's CMD_DIFF can reference us. */
static struct yetty_ycore_void_result figure_apply_wire_frame(struct yetty_ymgui_figure *figure,
                                                              const uint8_t *wire_bytes,
                                                              size_t wire_size)
{
    const struct yetty_ymgui_wire_frame *fh = (const struct yetty_ymgui_wire_frame *)wire_bytes;
    int idx32 = (fh->flags & YMGUI_FRAME_FLAG_IDX32) ? 1 : 0;
    size_t idx_bpe = idx32 ? 4u : 2u;
    size_t cl_count = fh->cmd_list_count;

    /* Pass 1: classify each slot and (for CMD_DIFF) pre-resolve the
     * cmd_view list so pass 2 can just memcpy. */
    struct ymgui_slot_resolve *resolves = NULL;
    if (cl_count) {
        resolves = calloc(cl_count, sizeof(*resolves));
        if (!resolves) {
            return YETTY_ERR(yetty_ycore_void, "ymgui frame: oom (slot resolves)");
        }
    }

    const uint8_t *wire_end = wire_bytes + wire_size;
    size_t wire_off = sizeof(*fh);
    size_t denorm_size = sizeof(*fh);

    for (size_t i = 0; i < cl_count; i++) {
        if (wire_bytes + wire_off + sizeof(struct yetty_ymgui_wire_cmd_list) > wire_end) {
            ymgui_free_slot_resolves(resolves, cl_count);
            return YETTY_ERR(yetty_ycore_void, "ymgui frame: truncated cmd_list_hdr");
        }
        const struct yetty_ymgui_wire_cmd_list *clh =
            (const struct yetty_ymgui_wire_cmd_list *)(wire_bytes + wire_off);
        wire_off += sizeof(*clh);

        if (clh->flags & YMGUI_CMDLIST_FLAG_REPEAT) {
            if (i >= figure->slot_count || !figure->frame_bytes) {
                ymgui_free_slot_resolves(resolves, cl_count);
                return YETTY_ERR(yetty_ycore_void,
                                 "ymgui frame: REPEAT slot has no cached predecessor");
            }
            resolves[i].mode = YMGUI_SLOT_FROM_PREV;
            resolves[i].src_off = figure->slot_offsets[i];
            resolves[i].size = figure->slot_sizes[i];
        } else if (clh->flags & YMGUI_CMDLIST_FLAG_CMD_DIFF) {
            const uint8_t *body = wire_bytes + wire_off;
            size_t avail = (size_t)(wire_end - body);
            const uint8_t *prev_slot = (i < figure->slot_count && figure->frame_bytes)
                                           ? (figure->frame_bytes + figure->slot_offsets[i])
                                           : NULL;
            const uint64_t *prev_hashes = (figure->slot_cmd_hashes && i < figure->slot_count)
                                              ? figure->slot_cmd_hashes[i]
                                              : NULL;
            const uint32_t *prev_vtx_counts =
                (figure->slot_cmd_vtx_counts && i < figure->slot_count)
                    ? figure->slot_cmd_vtx_counts[i]
                    : NULL;
            const uint32_t *prev_orig_indices =
                (figure->slot_cmd_orig_indices && i < figure->slot_count)
                    ? figure->slot_cmd_orig_indices[i]
                    : NULL;
            uint32_t prev_count = (figure->slot_cmd_counts && i < figure->slot_count)
                                      ? figure->slot_cmd_counts[i]
                                      : 0;
            size_t consumed = 0;
            if (ymgui_resolve_cmd_diff(body, avail, clh->cmd_count, prev_slot, prev_hashes,
                                       prev_vtx_counts, prev_orig_indices, prev_count, idx32,
                                       &resolves[i].cmd_views, &resolves[i].cmd_view_count,
                                       &consumed) != 0) {
                ymgui_free_slot_resolves(resolves, cl_count);
                return YETTY_ERR(yetty_ycore_void, "ymgui frame: CMD_DIFF resolve failed");
            }
            wire_off += consumed;
            resolves[i].mode = YMGUI_SLOT_FROM_DIFF;
            resolves[i].size =
                ymgui_diff_slot_size(resolves[i].cmd_views, resolves[i].cmd_view_count, idx32);
        } else {
            size_t vbytes = (size_t)clh->vtx_count * 20u;
            size_t ibytes_padded = (size_t)clh->idx_count * idx_bpe;
            if (ibytes_padded & 3u) {
                ibytes_padded += 4u - (ibytes_padded & 3u);
            }
            size_t cmd_bytes = (size_t)clh->cmd_count * sizeof(struct yetty_ymgui_wire_cmd);
            size_t body_size = vbytes + ibytes_padded + cmd_bytes;
            if (wire_bytes + wire_off + body_size > wire_end) {
                ymgui_free_slot_resolves(resolves, cl_count);
                return YETTY_ERR(yetty_ycore_void, "ymgui frame: truncated slot body");
            }
            resolves[i].mode = YMGUI_SLOT_FROM_WIRE;
            resolves[i].src_off = wire_off - sizeof(*clh); /* include cl_hdr */
            resolves[i].size = sizeof(*clh) + body_size;
            wire_off += body_size;
        }
        denorm_size += resolves[i].size;
    }

    /* Pass 2: allocate the denormalized buffer and write slots. The prev
     * frame_bytes must stay readable until every FROM_PREV slot has been
     * copied, so the swap happens at the very end. */
    uint8_t *new_frame = malloc(denorm_size ? denorm_size : 1);
    size_t *new_offsets = NULL;
    size_t *new_sizes = NULL;
    if (cl_count) {
        new_offsets = malloc(cl_count * sizeof(*new_offsets));
        new_sizes = malloc(cl_count * sizeof(*new_sizes));
    }
    if (!new_frame || (cl_count && (!new_offsets || !new_sizes))) {
        free(new_frame);
        free(new_offsets);
        free(new_sizes);
        ymgui_free_slot_resolves(resolves, cl_count);
        return YETTY_ERR(yetty_ycore_void, "ymgui frame: oom (denormalized frame)");
    }

    memcpy(new_frame, fh, sizeof(*fh));
    ((struct yetty_ymgui_wire_frame *)new_frame)->total_size = (uint32_t)denorm_size;

    size_t out_off = sizeof(*fh);
    for (size_t i = 0; i < cl_count; i++) {
        new_offsets[i] = out_off;
        new_sizes[i] = resolves[i].size;
        if (resolves[i].mode == YMGUI_SLOT_FROM_PREV) {
            memcpy(new_frame + out_off, figure->frame_bytes + resolves[i].src_off,
                   resolves[i].size);
        } else if (resolves[i].mode == YMGUI_SLOT_FROM_WIRE) {
            memcpy(new_frame + out_off, wire_bytes + resolves[i].src_off, resolves[i].size);
        } else { /* DIFF */
            ymgui_write_diff_slot(new_frame + out_off, resolves[i].cmd_views,
                                  resolves[i].cmd_view_count, idx32);
        }
        /* Always clear any wire flags in the denormalized cl_hdr. */
        struct yetty_ymgui_wire_cmd_list *out_clh =
            (struct yetty_ymgui_wire_cmd_list *)(new_frame + out_off);
        out_clh->flags = 0;
        out_off += resolves[i].size;
    }

    /* Build the per-slot Stage 2 index from the just-composed slots so
     * the next frame's CMD_DIFF can look up against fresh arrays. */
    uint64_t **new_cmd_hashes = NULL;
    uint32_t **new_cmd_vtx_counts = NULL;
    uint32_t **new_cmd_orig_indices = NULL;
    uint32_t *new_cmd_counts = NULL;
    if (cl_count) {
        new_cmd_hashes = calloc(cl_count, sizeof(*new_cmd_hashes));
        new_cmd_vtx_counts = calloc(cl_count, sizeof(*new_cmd_vtx_counts));
        new_cmd_orig_indices = calloc(cl_count, sizeof(*new_cmd_orig_indices));
        new_cmd_counts = calloc(cl_count, sizeof(*new_cmd_counts));
        if (!new_cmd_hashes || !new_cmd_vtx_counts || !new_cmd_orig_indices || !new_cmd_counts) {
            free(new_cmd_hashes);
            free(new_cmd_vtx_counts);
            free(new_cmd_orig_indices);
            free(new_cmd_counts);
            free(new_frame);
            free(new_offsets);
            free(new_sizes);
            ymgui_free_slot_resolves(resolves, cl_count);
            return YETTY_ERR(yetty_ycore_void, "ymgui frame: oom (slot index)");
        }
    }
    for (size_t i = 0; i < cl_count; i++) {
        struct ymgui_slot_index slot_index = {0};
        if (!ymgui_index_slot_cmds(new_frame + new_offsets[i], idx32, &slot_index)) {
            for (size_t j = 0; j < i; j++) {
                free(new_cmd_hashes[j]);
                free(new_cmd_vtx_counts[j]);
                free(new_cmd_orig_indices[j]);
            }
            free(new_cmd_hashes);
            free(new_cmd_vtx_counts);
            free(new_cmd_orig_indices);
            free(new_cmd_counts);
            free(new_frame);
            free(new_offsets);
            free(new_sizes);
            ymgui_free_slot_resolves(resolves, cl_count);
            return YETTY_ERR(yetty_ycore_void, "ymgui frame: oom (cmd hash array)");
        }
        new_cmd_hashes[i] = slot_index.hashes;
        new_cmd_vtx_counts[i] = slot_index.vtx_counts;
        new_cmd_orig_indices[i] = slot_index.cmd_indices;
        new_cmd_counts[i] = slot_index.count;
    }

    {
        size_t full_slots = 0;
        size_t repeat_slots = 0;
        size_t diff_slots = 0;
        for (size_t i = 0; i < cl_count; i++) {
            if (resolves[i].mode == YMGUI_SLOT_FROM_PREV) {
                repeat_slots++;
            } else if (resolves[i].mode == YMGUI_SLOT_FROM_DIFF) {
                diff_slots++;
            } else {
                full_slots++;
            }
        }
        ydebug("ymgui frame: slots=%zu full=%zu repeat=%zu diff=%zu wire=%zu denorm=%zu", cl_count,
               full_slots, repeat_slots, diff_slots, wire_size, denorm_size);
    }

    ymgui_free_slot_resolves(resolves, cl_count);

    /* Swap in the new caches. */
    figure_release_slot_caches(figure);
    free(figure->frame_bytes);
    free(figure->slot_offsets);
    free(figure->slot_sizes);
    figure->frame_bytes = new_frame;
    figure->frame_size = denorm_size;
    figure->slot_offsets = new_offsets;
    figure->slot_sizes = new_sizes;
    figure->slot_count = cl_count;
    figure->slot_cmd_hashes = new_cmd_hashes;
    figure->slot_cmd_vtx_counts = new_cmd_vtx_counts;
    figure->slot_cmd_orig_indices = new_cmd_orig_indices;
    figure->slot_cmd_counts = new_cmd_counts;
    figure->has_frame = 1;
    return YETTY_OK_VOID();
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
    figure_drop_frame(f);
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
    /* `pixels` points into this record's buffer, which holds exactly
     * bytes_len - sizeof(*th) pixel bytes. set_atlas's own size guard is a
     * tautology on this path (it is handed pixel_bytes == width*height), so
     * the bound must be enforced here: reject a header whose declared
     * width*height exceeds the bytes actually delivered, else the R8 atlas
     * upload reads out of bounds past the record tail. */
    if (pixel_bytes > bytes_len - sizeof(*th)) {
        return YETTY_ERR(yetty_ycore_void, "ymgui_figure: tex pixels exceed payload");
    }
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
        figure_drop_frame(f);
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
        figure_drop_frame(f);
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

YETTY_ANNOTATE("override@yfigure:figure:render")
static struct yetty_ycore_void_result ymgui_figure_render_slot(struct yetty_yclass_object *obj,
                                                               struct yetty_ydraw_target *target)
{
    return ymgui_figure_render(obj, target);
}

YETTY_ANNOTATE("override@yfigure:figure:destroy")
static struct yetty_ycore_void_result ymgui_figure_destroy_slot(struct yetty_yclass_object *obj)
{
    return ymgui_figure_destroy(obj);
}

YETTY_ANNOTATE("override@yfigure:figure:process_input")
static struct yetty_ycore_void_result ymgui_figure_process_input_slot(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *statemachine)
{
    return ymgui_figure_process_input(obj, statemachine);
}

YETTY_ANNOTATE("override@yfigure:figure:process_bytes")
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

YETTY_ANNOTATE("expose")
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create_local(
    struct yetty_ycore_rectangle rect, struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context)
{
    struct yetty_yclass_object_ptr_result obj_r =
        ymgui_figure_create_object(rect, pipeline, context);
    YETTY_RETURN_IF_ERR(yetty_ymgui_figure_ptr, obj_r, "ymgui_figure_create_local: create");
    return yetty_ymgui_figure_from(obj_r.value);
}

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
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

    /* Rehydrate REPEAT / CMD_DIFF slots against the previous frame and
     * store the denormalized result (full slots pass through verbatim). */
    struct yetty_ycore_void_result apply_res = figure_apply_wire_frame(f, frame_bytes, frame_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "ymgui_figure_set_frame: rehydrate");
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
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

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymgui_register_factory(struct yetty_yfigure_registry *registry,
                                                            struct yetty_ymgui_factory_args *args)
{
    if (!registry || !args) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymgui_register_factory: NULL arg");
    }
    return yetty_yfigure_registry_register(registry, yetty_yfigure_kind_token("ymgui"),
                                           ymgui_factory, args);
}

YETTY_ANNOTATE("expose")
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
#include "yetty/gen/impl/ymgui/figure.c"
