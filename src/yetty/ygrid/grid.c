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
#include <yetty/yruntime/yruntime.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ysdf/handler.h>
#include <yetty/yfont/font.h>
#include <yetty/yrender/font-dispatcher.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender/types.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yetty/yetty.h>

/* GLYPH primitive type — matches ydraw-layer.wgsl's YDRAW_SDF_GLYPH. */
#define YGRID_GLYPH_TYPE 200u

/* Font dispatcher generation lives below the struct definition. */

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

    /* Own-pipeline binder — flattens the rs tree, computes buffer
     * offsets, compiles the shader with those offsets baked in, and
     * uploads to those same offsets. One source of truth: any structural
     * change (size growth past a power-of-2 cap, etc.) triggers a
     * refinalize that re-derives offsets AND recompiles the shader. */
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;

    /* Font slots. Slot 0 is the default font (font_id=0 in GLYPH wire
     * payload). Pointers are borrowed; ygrid does not destroy.
     *
     * font_generation bumps on every set_font call; last_emitted_gen
     * tracks the value the dispatcher was last regenerated for. The
     * dispatcher rebuild path triggers a shader-hash change which makes
     * the binder refinalize on the next update(). */
    struct yetty_ydraw_font *fonts[YETTY_YRENDER_RS_MAX_CHILDREN - 1];
    uint32_t font_count;
    uint32_t font_generation;
    uint32_t last_emitted_font_generation;

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

/* Forward decls — the TEXT_SPAN expansion below uses
 * parse_and_index_record to bucket each generated glyph record, and
 * grow_bytes to extend grid->bytes for the new GLYPH records. The
 * normal SDF/GLYPH parse loop and the expansion call into each other. */
static struct yetty_ycore_void_result parse_and_index_record(
    struct yetty_ygrid_grid *g, uint32_t record_offset, size_t record_len);
static struct yetty_ycore_void_result grow_bytes(
    struct yetty_ygrid_grid *grid, size_t need);

/* Decode one UTF-8 codepoint at *ptr (clamped by `end`). Advances *ptr
 * past the consumed bytes and returns the codepoint, or 0xFFFD on a
 * malformed prefix (still consumes one byte to make forward progress).
 * Matches the same decode shape scene-canvas uses for TEXT_SPAN. */
static uint32_t decode_utf8(const uint8_t **ptr, const uint8_t *end)
{
    const uint8_t *cursor = *ptr;
    if (cursor >= end)
        return 0;
    uint8_t lead = *cursor;
    uint32_t codepoint;
    if ((lead & 0x80u) == 0u) {
        codepoint = lead;
        cursor += 1;
    } else if ((lead & 0xE0u) == 0xC0u) {
        codepoint = (uint32_t)(lead & 0x1Fu) << 6;
        cursor += 1;
        if (cursor < end) codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
    } else if ((lead & 0xF0u) == 0xE0u) {
        codepoint = (uint32_t)(lead & 0x0Fu) << 12;
        cursor += 1;
        if (cursor < end) codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 6;
        if (cursor < end) codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
    } else if ((lead & 0xF8u) == 0xF0u) {
        codepoint = (uint32_t)(lead & 0x07u) << 18;
        cursor += 1;
        if (cursor < end) codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 12;
        if (cursor < end) codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 6;
        if (cursor < end) codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
    } else {
        codepoint = 0xFFFDu;
        cursor += 1;
    }
    *ptr = cursor;
    return codepoint;
}

/* Expand one TEXT_SPAN wire record into glyph records, mirroring
 * scene-canvas's scene_expand_text_span_to_glyphs. The TEXT_SPAN's
 * font_id maps directly to a ygrid font slot (-1 means slot 0, the
 * default). Each generated glyph is appended to grid->bytes as its own
 * 7-word GLYPH wire record and bucketed via parse_and_index_record so
 * the rest of the pipeline treats it like any other glyph.
 *
 * `text_run` and `text_run_len` are passed in instead of read from the
 * view because the view's text pointer lives inside grid->bytes, which
 * may be realloc'd by grow_bytes when each generated glyph is emitted.
 * The caller takes a heap copy first to keep the pointer stable. */
static struct yetty_ycore_void_result expand_text_span(
    struct yetty_ygrid_grid *grid, const struct yetty_ydraw_text_span_drawable_view *span,
    const uint8_t *text_run, uint32_t text_run_len)
{
    /* font_id < 0 means "default" → slot 0. Otherwise the producer chose
     * an explicit slot via the slot-indexed set_font API. Out-of-range
     * or NULL-slot fonts are dropped silently — same as scene-canvas's
     * "no font registered yet" path. */
    uint32_t slot = (span->font_id < 0) ? 0u : (uint32_t)span->font_id;
    if (slot >= grid->font_count || !grid->fonts[slot]) {
        ydebug("ygrid: TEXT_SPAN font_id=%d -> slot %u has no font; dropped",
               span->font_id, slot);
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_font *font = grid->fonts[slot];

    struct yetty_yrender_gpu_resource_set_result font_rs_result =
        font->ops->get_gpu_resource_set(font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_rs_result,
                        "ygrid: text_span font rs");
    const struct yetty_ydraw_gpu_resource_set *font_rs = font_rs_result.value;
    if (font_rs->buffer_count == 0 || !font_rs->buffers[0].data) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygrid: text_span: font has no glyph metadata");
    }
    /* msdf-font's rs.buffers[0] is an array of 6-float entries:
     *   size_x, size_y, bearing_x, bearing_y, advance, cell_idx
     * The font lazily allocates a new metadata slot whenever
     * get_glyph_index sees a codepoint it hasn't rasterized yet, so
     * `font_rs->buffers[0].size` grows DURING this loop. Re-fetch it
     * after every get_glyph_index call (same pattern scene-canvas's
     * expand uses) so the bounds check stays in sync. */
    (void)font_rs;  /* the per-iteration re-fetch supersedes this snapshot */

    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0.0f) ? span->font_size / base_size : 1.0f;
    float cursor_x = span->x;

    const uint8_t *cursor = text_run;
    const uint8_t *end = text_run + text_run_len;
    while (cursor < end) {
        uint32_t codepoint = decode_utf8(&cursor, end);
        if (codepoint == 0)
            break;

        struct uint32_result glyph_idx_result =
            font->ops->get_glyph_index(font, codepoint);
        if (YETTY_IS_ERR(glyph_idx_result)) {
            /* No glyph for this codepoint — match scene-canvas's
             * fallback: advance by a quarter em + spacing. */
            yetty_ycore_error_destroy(glyph_idx_result.error);
            cursor_x += (span->font_size * 0.25f) + span->char_spacing;
            if (codepoint == 0x20)
                cursor_x += span->word_spacing;
            continue;
        }
        uint32_t glyph_index = glyph_idx_result.value;

        /* Re-fetch the metadata view AFTER get_glyph_index so any
         * lazy slot allocation it triggered is visible here. */
        struct yetty_yrender_gpu_resource_set_result fresh_rs_result =
            font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fresh_rs_result,
                            "ygrid: text_span font rs refetch");
        const struct yetty_ydraw_gpu_resource_set *fresh_rs = fresh_rs_result.value;
        const float *meta = (const float *)fresh_rs->buffers[0].data;
        uint32_t meta_count =
            (uint32_t)(fresh_rs->buffers[0].size / (6u * sizeof(float)));
        if (glyph_index >= meta_count)
            return YETTY_ERR(yetty_ycore_void,
                             "ygrid: text_span glyph_index out of metadata range");

        const float *glyph_meta = meta + glyph_index * 6u;
        float size_x = glyph_meta[0];
        float size_y = glyph_meta[1];
        float bearing_x = glyph_meta[2];
        float bearing_y = glyph_meta[3];
        float advance = glyph_meta[4];

        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * scale + span->char_spacing;
            if (codepoint == 0x20)
                cursor_x += span->word_spacing;
            continue;
        }

        float glyph_x = cursor_x + bearing_x * scale;
        float glyph_y = span->y - bearing_y * scale;

        /* Build the 7-word GLYPH wire record into a stack buffer,
         * append to grid->bytes (which may realloc), then bucket via
         * parse_and_index_record. Re-reading text_run / span from
         * grid->bytes after the realloc would be unsafe — that's why
         * the caller passed in a heap-stable text copy. */
        uint32_t glyph_record[7];
        glyph_record[0] = YGRID_GLYPH_TYPE;
        glyph_record[1] = 0u;                /* z_order */
        memcpy(&glyph_record[2], &glyph_x, sizeof(float));
        memcpy(&glyph_record[3], &glyph_y, sizeof(float));
        memcpy(&glyph_record[4], &span->font_size, sizeof(float));
        glyph_record[5] = (glyph_index & 0xFFFFu) | ((slot + 1u) << 16);
        glyph_record[6] = span->color;

        size_t glyph_bytes = sizeof(glyph_record);
        struct yetty_ycore_void_result grow_result = grow_bytes(grid, glyph_bytes);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, grow_result,
                            "ygrid: text_span grow_bytes");
        uint32_t glyph_offset = (uint32_t)grid->bytes_len;
        memcpy(grid->bytes + grid->bytes_len, glyph_record, glyph_bytes);
        grid->bytes_len += glyph_bytes;

        struct yetty_ycore_void_result index_result =
            parse_and_index_record(grid, glyph_offset, glyph_bytes);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, index_result,
                            "ygrid: text_span index glyph");

        cursor_x += advance * scale + span->char_spacing;
        if (codepoint == 0x20)
            cursor_x += span->word_spacing;
    }
    return YETTY_OK_VOID();
}

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
    /* TEXT_SPAN: not a rendered prim itself. Expand into one GLYPH
     * record per codepoint (same shape as scene-canvas's expansion);
     * each generated glyph is appended + bucketed normally and shows
     * up in prim_count/staging. The TEXT_SPAN bytes themselves stay in
     * grid->bytes but produce no prim entry. */
    if (type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        struct yetty_ydraw_text_span_drawable_view view;
        if (yetty_ydraw_text_span_drawable_parse(hdr, &view) != 0)
            return YETTY_ERR(yetty_ycore_void, "ygrid: TEXT_SPAN parse failed");
        /* `view.text` aliases into g->bytes; grow_bytes inside the
         * expansion may realloc that buffer and dangle the pointer.
         * Take a heap copy so each glyph emission has a stable read. */
        uint8_t *text_copy = NULL;
        if (view.text_len > 0) {
            text_copy = (uint8_t *)malloc(view.text_len);
            if (!text_copy)
                return YETTY_ERR(yetty_ycore_void, "ygrid: TEXT_SPAN text copy oom");
            memcpy(text_copy, view.text, view.text_len);
        }
        struct yetty_ycore_void_result expand_result =
            expand_text_span(g, &view, text_copy, view.text_len);
        free(text_copy);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, expand_result, "ygrid: TEXT_SPAN expand");
        return YETTY_OK_VOID();
    }

    struct rectangle_result ar;
    if (type == YGRID_GLYPH_TYPE) {
        /* GLYPH wire layout — 7 words, identical to scrolling-canvas's
         * YDRAW_GLYPH_WORDS so the same ydraw-layer.wgsl shader reads
         * both producers:
         *   word 0 type            (= 200)
         *   word 1 z_order
         *   word 2 x               ← read here
         *   word 3 y
         *   word 4 font_size
         *   word 5 packed          (glyph_idx | (slot+1) << 16)
         *   word 6 color
         * rebuild_prim_staging prepends a rolling_row=0 word, so
         * storage_buffer[drawable_offset+3] lands on word 2 (x) — which
         * is what glyph_read_x in the shader expects. */
        if (record_len < 7u * sizeof(uint32_t))
            return YETTY_ERR(yetty_ycore_void, "ygrid: GLYPH record truncated");
        float gx = *(const float *)&hdr[2];
        float gy = *(const float *)&hdr[3];
        float gs = *(const float *)&hdr[4];
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

/* Defined further down (alongside the other shader-build helpers, after
 * the resource_set / pipeline setup section). Declared here so the
 * render path can pull it in when the font set changes mid-frame. */
static struct yetty_ycore_void_result rebuild_font_dispatcher(
    struct yetty_ygrid_grid *grid);

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

    /* Font set changed since last render — regenerate the dispatcher and
     * rs.children. The shader-code hash change triggers a binder
     * refinalize on the next update(). */
    if (g->font_generation != g->last_emitted_font_generation) {
        struct yetty_ycore_void_result dispatcher_result =
            rebuild_font_dispatcher(g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result,
                            "ygrid_render: rebuild font dispatcher");
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

    ydebug("ygrid_render: view=%p target=%p target_vp=(%.1f,%.1f,%.1f,%.1f) "
           "viewport=(%.1f,%.1f,%.1fx%.1f) prim_count=%u",
           (void *)view, (void *)target,
           target->viewport.x, target->viewport.y,
           target->viewport.w, target->viewport.h,
           vx, vy, w, h, g->prim_count);

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
    ydebug("ygrid_render: pipe=%p quad_vb=%p scissor=(%u,%u,%u,%u)",
           (void *)pipe, (void *)quad_vb,
           (uint32_t)sx0, (uint32_t)sy0,
           (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0));
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

/* Load the raw ydraw-layer.wgsl bytes into layer_shader_code. The
 * combined shader (font-dispatcher + this file) is assembled lazily by
 * rebuild_font_dispatcher() and updated whenever the font set changes. */
static struct yetty_ycore_void_result load_layer_shader(
    struct yetty_ygrid_grid *grid, const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];
    snprintf(path, sizeof(path), "%s/ydraw-layer.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result file_result = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, file_result, "ygrid: read ydraw-layer.wgsl");
    grid->layer_shader_code = file_result.value;
    return YETTY_OK_VOID();
}

/* Regenerate combined shader (dispatcher + layer code) and refresh
 * rs.children with the currently active fonts. Called from ygrid_render
 * when font_generation differs from last_emitted_font_generation. The
 * resulting shader-code hash change makes the binder refinalize on the
 * next update(), which recompiles with the new dispatcher in place.
 *
 * The dispatcher itself comes from yetty_yrender_build_font_dispatcher_wgsl
 * (shared with ydraw-layer); this function deals only with collecting
 * per-slot namespaces, concatenating the dispatcher with the layer shader,
 * and wiring the active font rs's into rs.children. */
static struct yetty_ycore_void_result rebuild_font_dispatcher(struct yetty_ygrid_grid *grid)
{
    /* Resolve each active slot's font rs once. The rs pointers are
     * reused below for both the dispatcher namespace list and the
     * rs.children attachment, so we don't double-call the font op. */
    const struct yetty_ydraw_gpu_resource_set *font_rs[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    const char *slot_namespaces[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t slot = 0; slot < grid->font_count; slot++) {
        if (!grid->fonts[slot])
            continue;
        struct yetty_yrender_gpu_resource_set_result font_rs_result =
            grid->fonts[slot]->ops->get_gpu_resource_set(grid->fonts[slot]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_rs_result,
                            "ygrid: font get_gpu_resource_set");
        font_rs[slot] = font_rs_result.value;
        slot_namespaces[slot] = font_rs_result.value->namespace;
    }

    char *dispatcher_wgsl = NULL;
    size_t dispatcher_size = 0;
    struct yetty_ycore_void_result dispatcher_result =
        yetty_yrender_build_font_dispatcher_wgsl(
            slot_namespaces, grid->font_count, &dispatcher_wgsl, &dispatcher_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result,
                        "ygrid: build font dispatcher");

    size_t combined_size = dispatcher_size + grid->layer_shader_code.size;
    char *combined_buffer = (char *)malloc(combined_size + 1u);
    if (!combined_buffer) {
        free(dispatcher_wgsl);
        return YETTY_ERR(yetty_ycore_void, "ygrid: combined shader oom");
    }
    memcpy(combined_buffer, dispatcher_wgsl, dispatcher_size);
    memcpy(combined_buffer + dispatcher_size,
           grid->layer_shader_code.data, grid->layer_shader_code.size);
    combined_buffer[combined_size] = '\0';
    free(dispatcher_wgsl);

    free(grid->combined_shader);
    grid->combined_shader = combined_buffer;
    grid->combined_shader_size = combined_size;
    yetty_yrender_shader_code_set(&grid->rs.shader,
                                  grid->combined_shader,
                                  grid->combined_shader_size);

    /* rs.children[0] = sdf_lib, [1..N] = active font rs in slot order.
     * NULL slots are skipped — the dispatcher's switch cases are sparse
     * (skipped slots fall through to the default). */
    size_t children_used = 0;
    grid->rs.children[children_used++] = &grid->sdf_lib_rs;
    for (uint32_t slot = 0; slot < grid->font_count; slot++) {
        if (!font_rs[slot])
            continue;
        if (children_used >= YETTY_YRENDER_RS_MAX_CHILDREN)
            break;
        grid->rs.children[children_used++] =
            (struct yetty_ydraw_gpu_resource_set *)font_rs[slot];
    }
    grid->rs.children_count = children_used;
    grid->last_emitted_font_generation = grid->font_generation;
    ydebug("ygrid: rebuilt dispatcher fonts=%u children=%zu shader=%zuB",
           grid->font_count, children_used, grid->combined_shader_size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result load_sdf_lib(
    struct yetty_ygrid_grid *g, const struct yetty_context *context)
{
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char path[512];
    snprintf(path, sizeof(path), "%s/ysdf.gen.wgsl", shaders_dir);
    ydebug("ygrid: load_sdf_lib: shaders_dir='%s' path='%s'", shaders_dir, path);
    struct yetty_ycore_buffer_result fr = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ygrid: read ysdf.gen.wgsl");
    g->sdf_lib_code = fr.value;
    strncpy(g->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&g->sdf_lib_rs.shader,
                                  (const char *)g->sdf_lib_code.data,
                                  g->sdf_lib_code.size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_binder(struct yetty_ygrid_grid *grid)
{
    /* Mirror ydraw-layer's rs shape exactly so ydraw-layer.wgsl works
     * as our shader without modification. namespace "ydraw" gives the
     * binder-generated uniform field names the shader expects
     * (ydraw_ydraw_grid_size etc.). */
    strncpy(grid->rs.namespace, "ydraw", YETTY_YRENDER_NAME_MAX - 1);
    grid->rs.buffer_count = 2;
    strncpy(grid->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(grid->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    grid->rs.buffers[0].readonly = 1;
    strncpy(grid->rs.buffers[1].name, "prims", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(grid->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    grid->rs.buffers[1].readonly = 1;

    init_uniforms(&grid->rs);

    grid->rs.instance_count = 1;

    /* Build the initial (no-font) dispatcher → combined shader →
     * rs.children. With no fonts attached this is just the SDF lib
     * + the layer code with stub-default helpers; the shader compiles
     * and renders SDF prims correctly. set_font() rebuilds later. */
    struct yetty_ycore_void_result dispatcher_result = rebuild_font_dispatcher(grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result,
                        "ygrid: initial dispatcher");

    struct yetty_yrender_gpu_resource_binder_result binder_result =
        yetty_yrender_gpu_resource_binder_create(
            grid->device, grid->queue, grid->target_format, grid->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, binder_result, "ygrid: binder create");
    grid->binder = binder_result.value;

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
    g->device = context->runtime->gpu.device;
    g->queue = context->runtime->gpu.queue;
    g->target_format = context->runtime->gpu.surface_format;
    g->allocator = context->runtime->gpu.allocator;
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
    struct yetty_ycore_void_result br = build_binder(g);
    if (YETTY_IS_ERR(br)) {
        ygrid_destroy(&g->base);
        return YETTY_ERR(yetty_ygrid_grid_ptr, "ygrid_create: build_binder", br);
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

struct yetty_ycore_void_result yetty_ygrid_set_font(
    struct yetty_ygrid_grid *grid, uint32_t slot, struct yetty_ydraw_font *font)
{
    if (!grid)
        return YETTY_ERR(yetty_ycore_void, "ygrid_set_font: NULL grid");
    /* Cap at the rs.children[] capacity minus the SDF lib slot. */
    if (slot >= YETTY_YRENDER_RS_MAX_CHILDREN - 1u)
        return YETTY_ERR(yetty_ycore_void, "ygrid_set_font: slot out of range");

    grid->fonts[slot] = font;
    /* font_count is the high watermark used by the dispatcher loop —
     * keep it ≥ slot+1 when assigning; clearing a tail slot doesn't
     * shrink it (NULL slots fall through to the default case anyway). */
    if (font && (slot + 1u) > grid->font_count)
        grid->font_count = slot + 1u;
    grid->font_generation++;
    grid->base.dirty = 1;
    return YETTY_OK_VOID();
}
