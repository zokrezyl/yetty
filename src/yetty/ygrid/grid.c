/*
 * ygrid — figure kind: spatial-bucketed batch of SDF primitives.
 *
 * Standalone implementation: owns its prim storage, spatial bucketing,
 * GPU pipeline, binder, and inline shader. Depends only on foundational
 * modules:
 *   ycore        — Result + buffer + rectangle types
 *   yfigure      — figure base type
 *   yrender      — pipeline + binder + resource set machinery
 *   ydraw-core   — flyweight registry (wire-format parsing primitives)
 *   ysdf         — SDF handler (size + aabb) and ysdf.gen.wgsl (SDF math)
 *
 * No coupling to scene-canvas / scrolling-canvas / ydraw-layer. Those
 * modules are kept alive only for backward compatibility while the
 * compositor migration is in flight; they will be retired once the new
 * stack is complete.
 */

#include <yetty/ygrid/ygrid.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/yconfig/config.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ysdf/handler.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/pipeline.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender/types.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yetty/yetty.h>

/* GLYPH primitive type — matches ydraw-layer.wgsl's YDRAW_SDF_GLYPH. */
#define YGRID_GLYPH_TYPE 200u

/*===========================================================================
 * Stub font dispatcher prepended to ydraw-layer.wgsl. ygrid v1 has no
 * font cache wired in yet — these stubs make the shader compile while
 * producing transparent glyphs. When font support lands they get
 * replaced with a real per-slot dispatcher (same shape as
 * ydraw-layer.c emits today).
 *=========================================================================*/
static const char *ygrid_stub_font_dispatcher_wgsl(void)
{
    static const char src[] =
        "// ygrid v1: stub font dispatcher (no fonts wired yet)\n"
        "fn font_base_size(slot: u32) -> f32 { return 1.0; }\n"
        "fn font_glyph_size(slot: u32, glyph_index: u32) -> vec2<f32> {\n"
        "    return vec2<f32>(0.0, 0.0);\n"
        "}\n"
        "fn font_glyph_sample(slot: u32, glyph_index: u32, uv: vec2<f32>, ps: f32) -> f32 {\n"
        "    return 0.0;\n"
        "}\n"
        "\n";
    return src;
}

#if 0
/*===========================================================================
 * (Retired) inline custom shader. Kept here as a doc reference only —
 * superseded by reading ydraw-layer.wgsl from disk so the new figure
 * uses the same proven SDF + glyph dispatch the existing layer uses.
 *
 * Shader-side data layout:
 *   storage_buffer holds the binder's flat merge of all buffers.
 *   `ygrid_grid_offset` and `ygrid_prims_offset` are u32 consts the
 *   binder emits, indexing the merged storage_buffer.
 *
 *   grid sub-buffer:
 *     words[0..num_cells)         — per-cell start offset in this buffer
 *     at each cell's offset:
 *       words[off]                — cell_count
 *       words[off+1..]            — global prim indices (prim_count)
 *
 *   prims sub-buffer:
 *     words[0..prim_count)        — offset table: each entry = offset
 *                                    (in u32 words) from end of table
 *                                    to that prim's first word
 *     after table:                — concatenated prim records.
 *                                    record layout matches ysdf wire:
 *                                    [type, z_order, fill, stroke,
 *                                     stroke_width, geom_floats...]
 *                                    evaluate_sdf_2d expects this exact
 *                                    layout starting from drawable_offset.
 *=========================================================================*/
static const char *ygrid_layer_wgsl(void)
{
    static const char src[] =
        "// ygrid layer shader — SDF primitives, spatial-bucketed dispatch.\n"
        "\n"
        "struct VertexInput {\n"
        "    @location(0) position: vec2<f32>,\n"
        "};\n"
        "\n"
        "struct VertexOutput {\n"
        "    @builtin(position) position: vec4<f32>,\n"
        "    @location(0) @interpolate(linear) local_pixel: vec2<f32>,\n"
        "};\n"
        "\n"
        "@vertex\n"
        "fn vs_main(input: VertexInput) -> VertexOutput {\n"
        "    var output: VertexOutput;\n"
        "    output.position = vec4<f32>(input.position, 0.0, 1.0);\n"
        "    let grid_size = uniforms.ygrid_grid_size;\n"
        "    let cell_size = uniforms.ygrid_cell_size;\n"
        "    let local_w = grid_size.x * cell_size.x;\n"
        "    let local_h = grid_size.y * cell_size.y;\n"
        "    output.local_pixel = vec2<f32>(\n"
        "        (input.position.x * 0.5 + 0.5) * local_w,\n"
        "        (0.5 - input.position.y * 0.5) * local_h\n"
        "    );\n"
        "    return output;\n"
        "}\n"
        "\n"
        "fn ygrid_read_type(off: u32) -> u32 {\n"
        "    return storage_buffer[off + 0u];\n"
        "}\n"
        "\n"
        "fn ygrid_read_fill_color(off: u32) -> u32 {\n"
        "    return storage_buffer[off + 2u];\n"
        "}\n"
        "\n"
        "@fragment\n"
        "fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {\n"
        "    let prim_count = uniforms.ygrid_prim_count;\n"
        "    if (prim_count == 0u) { discard; }\n"
        "\n"
        "    let grid_size = uniforms.ygrid_grid_size;\n"
        "    let cell_size = uniforms.ygrid_cell_size;\n"
        "    let grid_w = u32(grid_size.x);\n"
        "    let grid_h = u32(grid_size.y);\n"
        "    if (grid_w == 0u || grid_h == 0u) { discard; }\n"
        "\n"
        "    let pixel_pos = input.local_pixel;\n"
        "    let grid_pixel_w = grid_size.x * cell_size.x;\n"
        "    let grid_pixel_h = grid_size.y * cell_size.y;\n"
        "    if (pixel_pos.x < 0.0 || pixel_pos.y < 0.0 ||\n"
        "        pixel_pos.x >= grid_pixel_w || pixel_pos.y >= grid_pixel_h) {\n"
        "        discard;\n"
        "    }\n"
        "\n"
        "    let cell_x = u32(clamp(pixel_pos.x / cell_size.x, 0.0, f32(grid_w - 1u)));\n"
        "    let cell_y = u32(clamp(pixel_pos.y / cell_size.y, 0.0, f32(grid_h - 1u)));\n"
        "    let cell_index = cell_y * grid_w + cell_x;\n"
        "\n"
        "    let grid_off = ygrid_grid_offset;\n"
        "    let prims_off = ygrid_prims_offset;\n"
        "\n"
        "    let cell_start = storage_buffer[grid_off + cell_index];\n"
        "    let cell_count = storage_buffer[grid_off + cell_start];\n"
        "    let loop_count = min(cell_count, 64u);\n"
        "\n"
        "    var result_color = vec3<f32>(0.0);\n"
        "    var result_alpha = 0.0;\n"
        "\n"
        "    for (var i = 0u; i < loop_count; i++) {\n"
        "        let raw_idx = storage_buffer[grid_off + cell_start + 1u + i];\n"
        "        let data_offset = storage_buffer[prims_off + raw_idx];\n"
        "        let drawable_offset = prims_off + prim_count + data_offset;\n"
        "\n"
        "        let drawable_type = ygrid_read_type(drawable_offset);\n"
        "        let d = evaluate_sdf_2d(drawable_offset, pixel_pos);\n"
        "\n"
        "        var fill_rgba: vec4<f32>;\n"
        "        var has_fill: bool;\n"
        "        if (yetty_ysdf_is_gradient_2d(drawable_type)) {\n"
        "            fill_rgba = yetty_ysdf_eval_gradient_color_2d(drawable_offset, pixel_pos);\n"
        "            has_fill = fill_rgba.a > 0.0;\n"
        "        } else {\n"
        "            let fill_packed = ygrid_read_fill_color(drawable_offset);\n"
        "            fill_rgba = yetty_ysdf_unpack_color(fill_packed);\n"
        "            has_fill = fill_packed != 0u;\n"
        "        }\n"
        "\n"
        "        if (d < 0.0 && has_fill) {\n"
        "            let edge_alpha = clamp(-d * 2.0, 0.0, 1.0);\n"
        "            let alpha = edge_alpha * fill_rgba.a;\n"
        "            result_color = mix(result_color, fill_rgba.rgb, alpha);\n"
        "            result_alpha = max(result_alpha, alpha);\n"
        "        }\n"
        "    }\n"
        "\n"
        "    if (result_alpha < 0.001) { discard; }\n"
        "    return vec4<f32>(result_color, result_alpha);\n"
        "}\n";
    return src;
}
#endif

/*===========================================================================
 * Per-prim metadata (parsed once from wire bytes at add time)
 *=========================================================================*/

struct ygrid_prim_meta {
    /* Offset in `bytes[]` to the wire record's TYPE+PAYLOAD_SIZE header
     * (the FAM `[type|payload_size|bytes...]` block). */
    uint32_t record_offset;
    /* Offset (within `bytes[]`) to the prim's first payload word — what
     * evaluate_sdf_2d expects to receive as drawable_offset (after rebase
     * into the prim_staging buffer). I.e. `record_offset + 8` bytes. */
    uint32_t prim_payload_offset;
    /* Prim payload size in u32 words. */
    uint32_t prim_payload_words;
    uint32_t type;
    /* AABB in figure-local pixel coords. */
    float min_x, min_y, max_x, max_y;
};

/*===========================================================================
 * Per-cell bucket (list of prim indices into the prims[] array)
 *=========================================================================*/

struct ygrid_cell {
    uint32_t *indices;
    uint32_t count;
    uint32_t cap;
};

/*===========================================================================
 * Uniform layout
 *=========================================================================*/

/* Mirrors ydraw-layer.c — same order, same names, same shader. */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_ROLLING_ROW_0 2
#define U_PRIM_COUNT 3
#define U_VZ_SCALE 4
#define U_VZ_OFF 5
#define U_CZ_SCALE 6
#define U_CZ_OFF 7
#define U_COUNT 8

/*===========================================================================
 * The figure
 *=========================================================================*/

struct yetty_ygrid_grid {
    struct yetty_yfigure_figure base;

    uint32_t grid_cols;
    uint32_t grid_rows;

    /* Wire bytes — concatenated records, copied verbatim from
     * yetty_ygrid_add_record callers. */
    uint8_t *bytes;
    size_t bytes_len;
    size_t bytes_cap;

    struct ygrid_prim_meta *prims;
    uint32_t prim_count;
    uint32_t prim_cap;

    /* grid_cols * grid_rows cells. NULL until first set_size. */
    struct ygrid_cell *cells;

    /* GPU */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Staging buffers — rebuilt when `staging_dirty`. */
    uint32_t *grid_staging;
    size_t grid_staging_words;
    size_t grid_staging_cap;
    uint32_t *prim_staging;
    size_t prim_staging_words;
    size_t prim_staging_cap;

    /* Resource set + child SDF lib */
    struct yetty_ydraw_gpu_resource_set rs;
    struct yetty_ydraw_gpu_resource_set sdf_lib_rs;
    struct yetty_ycore_buffer sdf_lib_code;
    /* ydraw-layer.wgsl raw bytes, loaded from paths/shaders. Combined
     * shader = stub font dispatcher + this file. */
    struct yetty_ycore_buffer layer_shader_code;
    char *combined_shader;
    size_t combined_shader_size;

    /* Pipeline + per-instance binder */
    struct yetty_yrender_pipeline *pipeline;
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;

    int staging_dirty;
};

/*===========================================================================
 * Cell helpers
 *=========================================================================*/

static struct yetty_ycore_void_result cell_push(struct ygrid_cell *cell, uint32_t prim_index)
{
    if (cell->count == cell->cap) {
        uint32_t cap = cell->cap ? cell->cap * 2u : 4u;
        uint32_t *grown = (uint32_t *)realloc(cell->indices, cap * sizeof(uint32_t));
        if (!grown)
            return YETTY_ERR(yetty_ycore_void, "ygrid: cell index oom");
        cell->indices = grown;
        cell->cap = cap;
    }
    cell->indices[cell->count++] = prim_index;
    return YETTY_OK_VOID();
}

static void cells_free(struct ygrid_cell *cells, size_t n)
{
    if (!cells)
        return;
    for (size_t i = 0; i < n; ++i)
        free(cells[i].indices);
    free(cells);
}

static struct yetty_ycore_void_result cells_alloc(struct yetty_ygrid_grid *g)
{
    size_t n = (size_t)g->grid_cols * (size_t)g->grid_rows;
    cells_free(g->cells, n);
    g->cells = (struct ygrid_cell *)calloc(n, sizeof(struct ygrid_cell));
    if (!g->cells)
        return YETTY_ERR(yetty_ycore_void, "ygrid: cells alloc oom");
    return YETTY_OK_VOID();
}

static void cells_clear(struct yetty_ygrid_grid *g)
{
    size_t n = (size_t)g->grid_cols * (size_t)g->grid_rows;
    for (size_t i = 0; i < n; ++i)
        g->cells[i].count = 0;
}

/*===========================================================================
 * Spatial bucketing — insert one prim into every cell it overlaps.
 *=========================================================================*/

static struct yetty_ycore_void_result bucket_prim(
    struct yetty_ygrid_grid *g, uint32_t prim_index)
{
    const struct ygrid_prim_meta *p = &g->prims[prim_index];
    float cw = (g->base.rect.max.x - g->base.rect.min.x) / (float)g->grid_cols;
    float ch = (g->base.rect.max.y - g->base.rect.min.y) / (float)g->grid_rows;
    if (cw <= 0.0f || ch <= 0.0f)
        return YETTY_OK_VOID();

    int col_min = (int)(p->min_x / cw);
    int col_max = (int)(p->max_x / cw);
    int row_min = (int)(p->min_y / ch);
    int row_max = (int)(p->max_y / ch);
    if (col_min < 0) col_min = 0;
    if (row_min < 0) row_min = 0;
    if (col_max >= (int)g->grid_cols) col_max = (int)g->grid_cols - 1;
    if (row_max >= (int)g->grid_rows) row_max = (int)g->grid_rows - 1;
    ydebug("ygrid: bucket prim_index=%u aabb=(%.1f,%.1f)-(%.1f,%.1f) cw=%.2f ch=%.2f → cells (%d..%d, %d..%d)",
           prim_index, p->min_x, p->min_y, p->max_x, p->max_y, cw, ch,
           col_min, col_max, row_min, row_max);
    if (col_max < col_min || row_max < row_min)
        return YETTY_OK_VOID();

    for (int r = row_min; r <= row_max; ++r) {
        for (int c = col_min; c <= col_max; ++c) {
            struct yetty_ycore_void_result pr =
                cell_push(&g->cells[(size_t)r * g->grid_cols + (size_t)c], prim_index);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid: cell_push");
        }
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Wire-record parsing — append one record's metadata to prims[].
 *
 * Record sizes are NOT uniform on the wire. Two shapes coexist:
 *
 *   SDF prims (ysdf types):  FIXED size by type (no payload_size word).
 *                              Layout: [type, z_order, fill, stroke,
 *                                       stroke_w, geom_words...]
 *                              Size derived from `yetty_ysdf_word_count(type)`.
 *
 *   FAM prims (TEXT_SPAN/FONT etc.):  Self-describing.
 *                              Layout: [type, payload_size, payload...]
 *                              Size = 8 + payload_size bytes.
 *
 * The caller hands us the FULL record size as `record_len` (computed
 * upstream by the iterator's ops->size or by command_parse). We don't
 * try to re-derive it — that's where my earlier bug was: I read
 * hdr[1] as payload_size, but for SDF that slot is z_order. Wrong.
 *
 * Staging layout per prim (matches ydraw-layer.wgsl expectations):
 *   word 0: rolling_row (= 0 for compositor figures, no scrolling)
 *   word 1: type
 *   word 2..N: rest of the wire record (z_order onwards), copied as-is.
 *
 * We store `prim_payload_offset = record_offset` (BYTE offset to the
 * start of the wire record — the TYPE word) and
 * `prim_payload_words = record_len / 4` (total record size in u32).
 * Staging build memcpy's the full record after the rolling_row prefix
 * — no need to synthesize the type separately.
 *=========================================================================*/

static struct yetty_ycore_void_result parse_and_index_record(
    struct yetty_ygrid_grid *g, uint32_t record_offset, size_t record_len)
{
    if (record_len < 4u || record_len % 4u != 0)
        return YETTY_ERR(yetty_ycore_void,
                         "ygrid: record_len must be a non-zero u32-multiple");
    if ((size_t)record_offset + record_len > g->bytes_len)
        return YETTY_ERR(yetty_ycore_void, "ygrid: record overruns byte buffer");
    const uint32_t *hdr = (const uint32_t *)(g->bytes + record_offset);
    uint32_t type = hdr[0];
    (void)hdr;  /* hdr[1] is NOT payload_size for SDF prims — see comment. */

    /* Compute aabb. ygrid currently handles two prim categories:
     *
     *   SDF prims (handler returns OK on ysdf tier types)
     *     — aabb derived from prim geometry
     *
     *   GLYPH prims (type 200 — ydraw's GLYPH layout, no separate
     *     handler in ysdf since glyphs aren't SDF) — aabb derived from
     *     [x, y, font_size]; glyph_size unknown without font, so use
     *     font_size as a conservative square (refined when font support
     *     lands).
     *
     * Anything else (TEXT_SPAN, FONT, complex prims) is RECORDED in
     * the byte buffer but NOT indexed: no entry in prims[] or the
     * spatial buckets, so it doesn't reach the shader. Returning OK
     * keeps the wire decoder flowing — ygui sends a mix of records
     * and dropping the unrendered ones silently is the v1 contract.
     * The bytes consumed by these records aren't reclaimed; they live
     * until the next yetty_ygrid_clear. */
    struct rectangle_result ar;
    if (type == YGRID_GLYPH_TYPE) {
        /* GLYPH wire layout (record_len/4 = 8 words):
         *   word 0 type
         *   word 1 z_order / rolling-equivalent
         *   word 2 (unused, fill slot)
         *   word 3 x   ← read here
         *   word 4 y
         *   word 5 font_size
         *   word 6 packed (glyph_idx | font_id)
         *   word 7 color
         * Matches ydraw-layer.wgsl's glyph_read_x = storage[off+3] convention. */
        if (record_len < 8u * sizeof(uint32_t))
            return YETTY_ERR(yetty_ycore_void, "ygrid: GLYPH record truncated");
        float gx = *(const float *)&hdr[3];
        float gy = *(const float *)&hdr[4];
        float gs = *(const float *)&hdr[5];
        ar = YETTY_OK(rectangle, ((struct yetty_ycore_rectangle){
            .min = {.x = gx, .y = gy},
            .max = {.x = gx + gs, .y = gy + gs},
        }));
    } else {
        struct yetty_ydraw_drawable_base_ops_ptr_result ops_r = yetty_ysdf_handler(type);
        if (YETTY_IS_ERR(ops_r)) {
            /* Not an SDF type and not a glyph — drop the error,
             * leave the wire bytes in place, and report success.
             * v1 renders nothing for unsupported types. */
            ydebug("ygrid: drop unrenderable type=0x%08x len=%zu", type, record_len);
            yetty_ycore_error_destroy(ops_r.error);
            return YETTY_OK_VOID();
        }
        const struct yetty_ydraw_drawable_base_ops *ops = ops_r.value;
        ar = ops->aabb(hdr);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "ygrid: SDF prim aabb");
        ydebug("ygrid: SDF type=0x%08x aabb=(%.1f,%.1f)-(%.1f,%.1f) len=%zu",
               type, ar.value.min.x, ar.value.min.y, ar.value.max.x, ar.value.max.y, record_len);
    }

    if (g->prim_count == g->prim_cap) {
        uint32_t cap = g->prim_cap ? g->prim_cap * 2u : 16u;
        struct ygrid_prim_meta *grown =
            (struct ygrid_prim_meta *)realloc(g->prims, cap * sizeof(struct ygrid_prim_meta));
        if (!grown)
            return YETTY_ERR(yetty_ycore_void, "ygrid: prims table oom");
        g->prims = grown;
        g->prim_cap = cap;
    }

    struct ygrid_prim_meta *meta = &g->prims[g->prim_count];
    meta->record_offset = record_offset;
    /* prim_payload_offset is BYTE offset of the start of the WHOLE wire
     * record (= the TYPE word). At staging time we prefix a single
     * rolling_row=0 word, then memcpy the whole record verbatim — that
     * lands the type at staging word 1 (where the shader expects it). */
    meta->prim_payload_offset = record_offset;
    meta->prim_payload_words = (uint32_t)(record_len / 4u);
    meta->type = type;
    meta->min_x = ar.value.min.x;
    meta->min_y = ar.value.min.y;
    meta->max_x = ar.value.max.x;
    meta->max_y = ar.value.max.y;

    uint32_t prim_index = g->prim_count++;
    return bucket_prim(g, prim_index);
}

/*===========================================================================
 * Staging — rebuild grid_staging + prim_staging u32 buffers
 *=========================================================================*/

static struct yetty_ycore_void_result ensure_words(
    uint32_t **buf, size_t *cap, size_t want)
{
    if (*cap >= want)
        return YETTY_OK_VOID();
    size_t new_cap = *cap ? *cap : 64u;
    while (new_cap < want)
        new_cap *= 2u;
    uint32_t *grown = (uint32_t *)realloc(*buf, new_cap * sizeof(uint32_t));
    if (!grown)
        return YETTY_ERR(yetty_ycore_void, "ygrid: staging buffer oom");
    *buf = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_grid_staging(struct yetty_ygrid_grid *g)
{
    size_t num_cells = (size_t)g->grid_cols * (size_t)g->grid_rows;
    /* Layout:
     *   [0..num_cells)      per-cell start offset (in u32 words inside
     *                        this same buffer)
     *   then concatenated   [count, idx_0, idx_1, ...] blocks per cell.
     * Empty cells share a single sentinel block holding count=0. */
    size_t need = num_cells + 1u; /* sentinel block (one word = count=0) */
    for (size_t i = 0; i < num_cells; ++i) {
        if (g->cells[i].count > 0)
            need += 1u + g->cells[i].count;
    }
    struct yetty_ycore_void_result r =
        ensure_words(&g->grid_staging, &g->grid_staging_cap, need);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ygrid: grid_staging ensure_words");

    /* Write the sentinel block right after the offset table. */
    uint32_t sentinel_off = (uint32_t)num_cells;
    g->grid_staging[sentinel_off] = 0u; /* count = 0 */
    uint32_t cursor = sentinel_off + 1u;

    for (size_t i = 0; i < num_cells; ++i) {
        const struct ygrid_cell *cell = &g->cells[i];
        if (cell->count == 0) {
            g->grid_staging[i] = sentinel_off;
            continue;
        }
        g->grid_staging[i] = cursor;
        g->grid_staging[cursor++] = cell->count;
        for (uint32_t k = 0; k < cell->count; ++k)
            g->grid_staging[cursor++] = cell->indices[k];
    }
    g->grid_staging_words = need;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_prim_staging(struct yetty_ygrid_grid *g)
{
    /* Layout (matches ydraw-layer.wgsl's expectations exactly):
     *   [0..prim_count)   offset table: each entry = data_offset in
     *                      u32 words from end of table to that prim's
     *                      first word.
     *   after table:      concatenated prim records, each shaped as
     *                      [rolling_row=0, FULL_WIRE_RECORD_WORDS...]
     *                      where the wire record starts with `type` at
     *                      its word 0 (followed by z_order, fill,
     *                      stroke, stroke_w, geometry — for SDF — or
     *                      payload_size + bytes for FAM prims). The
     *                      rolling_row prefix puts the type at staging
     *                      word 1, where evaluate_sdf_2d expects it
     *                      (drawable_offset+1u in the shader). The
     *                      rolling_row constant 0 is the no-scroll
     *                      contract — see feedback_rolling_row_scope. */
    size_t total_record_words = 0;
    for (uint32_t i = 0; i < g->prim_count; ++i)
        total_record_words += 1u /* rolling_row */ + g->prims[i].prim_payload_words;

    size_t need = (size_t)g->prim_count + total_record_words;
    struct yetty_ycore_void_result r =
        ensure_words(&g->prim_staging, &g->prim_staging_cap, need);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ygrid: prim_staging ensure_words");

    uint32_t cursor = (uint32_t)g->prim_count; /* offset table comes first */
    for (uint32_t i = 0; i < g->prim_count; ++i) {
        const struct ygrid_prim_meta *m = &g->prims[i];
        uint32_t data_offset = cursor - (uint32_t)g->prim_count;
        g->prim_staging[i] = data_offset;

        g->prim_staging[cursor++] = 0u;  /* rolling_row prefix */
        /* Copy the WHOLE wire record (starting from the type word). */
        memcpy(&g->prim_staging[cursor],
               g->bytes + m->prim_payload_offset,
               (size_t)m->prim_payload_words * sizeof(uint32_t));
        cursor += m->prim_payload_words;
    }
    g->prim_staging_words = need;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure ops
 *=========================================================================*/

static struct yetty_ycore_void_result ygrid_destroy(struct yetty_yfigure_figure *self)
{
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)self;

    if (g->binder)
        g->binder->ops->destroy(g->binder);
    if (g->pipeline)
        yetty_yrender_pipeline_destroy(g->pipeline);

    free(g->sdf_lib_code.data);
    free(g->layer_shader_code.data);
    free(g->combined_shader);
    free(g->grid_staging);
    free(g->prim_staging);
    if (g->cells)
        cells_free(g->cells, (size_t)g->grid_cols * (size_t)g->grid_rows);
    free(g->prims);
    free(g->bytes);

    free(g);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ygrid_render(
    struct yetty_yfigure_figure *self,
    struct yetty_ydraw_target *target)
{
    struct yetty_ygrid_grid *g = (struct yetty_ygrid_grid *)self;
    ydebug("ygrid_render: rect=(%.1f,%.1f)-(%.1f,%.1f) prims=%u staging_dirty=%d",
           self->rect.min.x, self->rect.min.y, self->rect.max.x, self->rect.max.y,
           g->prim_count, g->staging_dirty);

    if (g->staging_dirty) {
        struct yetty_ycore_void_result gr = rebuild_grid_staging(g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "ygrid_render: grid staging");
        struct yetty_ycore_void_result pr = rebuild_prim_staging(g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid_render: prim staging");
        g->staging_dirty = 0;
    }

    /* Wire current staging into the rs and re-submit to the binder. */
    g->rs.buffers[0].data = (uint8_t *)g->grid_staging;
    g->rs.buffers[0].size = g->grid_staging_words * sizeof(uint32_t);
    g->rs.buffers[0].dirty = 1;
    g->rs.buffers[1].data = (uint8_t *)g->prim_staging;
    g->rs.buffers[1].size = g->prim_staging_words * sizeof(uint32_t);
    g->rs.buffers[1].dirty = 1;

    g->rs.uniforms[U_GRID_SIZE].vec2[0] = (float)g->grid_cols;
    g->rs.uniforms[U_GRID_SIZE].vec2[1] = (float)g->grid_rows;
    float w = self->rect.max.x - self->rect.min.x;
    float h = self->rect.max.y - self->rect.min.y;
    g->rs.uniforms[U_CELL_SIZE].vec2[0] = w / (float)g->grid_cols;
    g->rs.uniforms[U_CELL_SIZE].vec2[1] = h / (float)g->grid_rows;
    g->rs.uniforms[U_PRIM_COUNT].u32 = g->prim_count;
    g->rs.pixel_size.width = w;
    g->rs.pixel_size.height = h;

    struct yetty_ycore_void_result sr = g->binder->ops->submit(g->binder, &g->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ygrid_render: binder submit");
    if (!g->binder_finalized) {
        struct yetty_ycore_void_result fr = g->binder->ops->finalize(g->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ygrid_render: binder finalize");
        g->binder_finalized = 1;
    }
    struct yetty_ycore_void_result ur = g->binder->ops->update(g->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "ygrid_render: binder update");

    /* Draw — yplot-style direct wgpu, scissored + viewport-mapped to
     * the figure's rect. */
    WGPUTextureView view = target->ops->get_view(target);
    if (!view)
        return YETTY_ERR(yetty_ycore_void, "ygrid_render: target view NULL");

    float vx = self->rect.min.x;
    float vy = self->rect.min.y;
    if (w <= 0.0f || h <= 0.0f)
        return YETTY_OK_VOID();

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g->device, &enc_desc);
    if (!enc)
        return YETTY_ERR(yetty_ycore_void, "ygrid_render: encoder create");

    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pass_desc);
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, w, h, 0.0f, 1.0f);
    /* SetScissorRect MUST stay within the render-area bounds. The
     * figure's rect may extend slightly beyond the pane (window's
     * absolute screen rect can exceed the target by a few pixels —
     * rounding, or the producer not knowing the target size). Clamp
     * to the target's viewport before submitting. */
    float tx0 = target->viewport.x;
    float ty0 = target->viewport.y;
    float tx1 = target->viewport.x + target->viewport.w;
    float ty1 = target->viewport.y + target->viewport.h;
    float sx0 = vx > tx0 ? vx : tx0;
    float sy0 = vy > ty0 ? vy : ty0;
    float sx1 = (vx + w) < tx1 ? (vx + w) : tx1;
    float sy1 = (vy + h) < ty1 ? (vy + h) : ty1;
    if (sx1 <= sx0 || sy1 <= sy0) {
        /* Entirely off-pane — nothing visible. Skip draw cleanly. */
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBufferDescriptor cb_desc_skip = {0};
        WGPUCommandBuffer cb_skip = wgpuCommandEncoderFinish(enc, &cb_desc_skip);
        wgpuQueueSubmit(g->queue, 1, &cb_skip);
        wgpuCommandBufferRelease(cb_skip);
        wgpuCommandEncoderRelease(enc);
        self->dirty = 0;
        return YETTY_OK_VOID();
    }
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)sx0, (uint32_t)sy0,
                                        (uint32_t)(sx1 - sx0),
                                        (uint32_t)(sy1 - sy0));

    WGPURenderPipeline pipe = g->binder->ops->get_pipeline(g->binder);
    WGPUBuffer quad_vb = g->binder->ops->get_quad_vertex_buffer(g->binder);
    wgpuRenderPassEncoderSetPipeline(pass, pipe);
    if (quad_vb)
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);
    struct yetty_ycore_void_result br = g->binder->ops->bind(g->binder, pass, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ygrid_render: binder bind");
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cb_desc);
    wgpuQueueSubmit(g->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    self->dirty = 0;
    return YETTY_OK_VOID();
}

static const struct yetty_yfigure_figure_ops *ygrid_ops(void)
{
    static const struct yetty_yfigure_figure_ops ops = {
        .destroy = ygrid_destroy,
        .render = ygrid_render,
    };
    return &ops;
}

/*===========================================================================
 * Resource set + pipeline setup
 *=========================================================================*/

/* Mirrors ydraw-layer.c::init_uniforms — same names, same order, same
 * types. With namespace "ydraw" the binder generates the shader-side
 * uniform field names ydraw_ydraw_grid_size, ydraw_ydraw_cell_size,
 * etc., which is what ydraw-layer.wgsl references. */
static void init_uniforms(struct yetty_ydraw_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;
    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_ROLLING_ROW_0] =
        (struct yetty_yrender_uniform){"ydraw_rolling_row_0", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_PRIM_COUNT] =
        (struct yetty_yrender_uniform){"ydraw_drawable_count", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_CZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};

    /* Static-figure defaults: no scrolling, no per-frame zoom. */
    rs->uniforms[U_ROLLING_ROW_0].u32 = 0;
    rs->uniforms[U_VZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_VZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_VZ_OFF].vec2[1] = 0.0f;
    rs->uniforms[U_CZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_CZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_CZ_OFF].vec2[1] = 0.0f;
}

static struct yetty_ycore_void_result load_layer_shader(
    struct yetty_ygrid_grid *g, const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];
    snprintf(path, sizeof(path), "%s/ydraw-layer.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result fr = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ygrid: read ydraw-layer.wgsl");
    g->layer_shader_code = fr.value;

    /* Combine stub-font-dispatcher + ydraw-layer.wgsl. The dispatcher
     * defines font_base_size / font_glyph_size / font_glyph_sample as
     * no-ops so the shader compiles without a real font cache. */
    const char *stub = ygrid_stub_font_dispatcher_wgsl();
    size_t stub_len = strlen(stub);
    size_t total = stub_len + g->layer_shader_code.size + 1u;
    char *buf = (char *)malloc(total);
    if (!buf)
        return YETTY_ERR(yetty_ycore_void, "ygrid: combined shader oom");
    memcpy(buf, stub, stub_len);
    memcpy(buf + stub_len, g->layer_shader_code.data, g->layer_shader_code.size);
    buf[total - 1u] = '\0';
    g->combined_shader = buf;
    g->combined_shader_size = total - 1u;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result load_sdf_lib(
    struct yetty_ygrid_grid *g, const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];
    snprintf(path, sizeof(path), "%s/ysdf.gen.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result fr = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ygrid: read ysdf.gen.wgsl");
    g->sdf_lib_code = fr.value;
    strncpy(g->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&g->sdf_lib_rs.shader,
                                  (const char *)g->sdf_lib_code.data,
                                  g->sdf_lib_code.size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_pipeline(struct yetty_ygrid_grid *g)
{
    /* Mirror ydraw-layer's rs shape exactly so ydraw-layer.wgsl works
     * as our shader without modification. namespace "ydraw" gives the
     * binder-generated uniform field names the shader expects
     * (ydraw_ydraw_grid_size etc.). */
    strncpy(g->rs.namespace, "ydraw", YETTY_YRENDER_NAME_MAX - 1);
    g->rs.buffer_count = 2;
    strncpy(g->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(g->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    g->rs.buffers[0].readonly = 1;
    strncpy(g->rs.buffers[1].name, "prims", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(g->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    g->rs.buffers[1].readonly = 1;

    init_uniforms(&g->rs);

    yetty_yrender_shader_code_set(&g->rs.shader, g->combined_shader,
                                  g->combined_shader_size);

    g->rs.children[0] = &g->sdf_lib_rs;
    g->rs.children_count = 1;
    g->rs.instance_count = 1;

    struct yetty_yrender_pipeline_ptr_result pr = yetty_yrender_pipeline_create(
        g->device, g->target_format, g->allocator, &g->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid: pipeline create");
    g->pipeline = pr.value;

    struct yetty_yrender_gpu_resource_binder_result br =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(
            g->device, g->queue, g->allocator, g->pipeline);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ygrid: binder create");
    g->binder = br.value;

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Byte buffer growth
 *=========================================================================*/

static struct yetty_ycore_void_result grow_bytes(struct yetty_ygrid_grid *g, size_t need)
{
    if (g->bytes_len + need <= g->bytes_cap)
        return YETTY_OK_VOID();
    size_t cap = g->bytes_cap ? g->bytes_cap * 2u : 256u;
    while (g->bytes_len + need > cap)
        cap *= 2u;
    uint8_t *grown = (uint8_t *)realloc(g->bytes, cap);
    if (!grown)
        return YETTY_ERR(yetty_ycore_void, "ygrid: byte buffer oom");
    g->bytes = grown;
    g->bytes_cap = cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_ygrid_grid_ptr_result yetty_ygrid_create(
    struct yetty_ycore_rectangle rect, uint32_t grid_cols, uint32_t grid_rows,
    const struct yetty_context *context)
{
    if (!context)
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: NULL context");
    if (grid_cols == 0 || grid_rows == 0)
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: grid dims must be non-zero");

    struct yetty_ygrid_grid *g =
        (struct yetty_ygrid_grid *)calloc(1, sizeof(struct yetty_ygrid_grid));
    if (!g)
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: oom");

    g->base.ops = ygrid_ops();
    g->base.rect = rect;
    g->base.dirty = 1;
    g->grid_cols = grid_cols;
    g->grid_rows = grid_rows;
    g->device = context->gpu_context.device;
    g->queue = context->gpu_context.queue;
    g->target_format = context->gpu_context.surface_format;
    g->allocator = context->gpu_context.allocator;
    g->staging_dirty = 1;

    struct yetty_ycore_void_result cr = cells_alloc(g);
    if (YETTY_IS_ERR(cr)) {
        ygrid_destroy(&g->base);
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: cells_alloc", cr);
    }
    struct yetty_ycore_void_result lr = load_sdf_lib(g, context);
    if (YETTY_IS_ERR(lr)) {
        ygrid_destroy(&g->base);
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: load_sdf_lib", lr);
    }
    struct yetty_ycore_void_result ls = load_layer_shader(g, context);
    if (YETTY_IS_ERR(ls)) {
        ygrid_destroy(&g->base);
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: load_layer_shader", ls);
    }
    struct yetty_ycore_void_result br = build_pipeline(g);
    if (YETTY_IS_ERR(br)) {
        ygrid_destroy(&g->base);
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: build_pipeline", br);
    }

    return YETTY_OK(yetty_ygrid_grid_ptr, g);
}

struct yetty_yfigure_figure *yetty_ygrid_as_figure(struct yetty_ygrid_grid *grid)
{
    if (!grid)
        return NULL;
    return &grid->base;
}

struct yetty_ycore_void_result yetty_ygrid_add_record(
    struct yetty_ygrid_grid *grid,
    const uint8_t *record_bytes, size_t record_len)
{
    if (!grid || !record_bytes)
        return YETTY_ERR(yetty_ycore_void, "ygrid_add_record: NULL arg");
    if (record_len < 4u || record_len % 4u != 0)
        return YETTY_ERR(yetty_ycore_void,
                         "ygrid_add_record: record_len must be a non-zero u32-multiple");
    struct yetty_ycore_void_result gr = grow_bytes(grid, record_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "ygrid_add_record: grow_bytes");

    uint32_t record_offset = (uint32_t)grid->bytes_len;
    memcpy(grid->bytes + grid->bytes_len, record_bytes, record_len);
    grid->bytes_len += record_len;

    struct yetty_ycore_void_result pr =
        parse_and_index_record(grid, record_offset, record_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ygrid_add_record: parse_and_index");

    grid->staging_dirty = 1;
    grid->base.dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_ygrid_grid *grid)
{
    if (!grid)
        return YETTY_ERR(yetty_ycore_void, "ygrid_clear: NULL arg");
    grid->bytes_len = 0;
    grid->prim_count = 0;
    cells_clear(grid);
    grid->staging_dirty = 1;
    grid->base.dirty = 1;
    return YETTY_OK_VOID();
}
