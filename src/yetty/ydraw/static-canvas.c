/* static-canvas.c — static (non-scrolling) ydraw canvas.
 *
 * Used by yui chrome. Cursor pinned at (0,0); rolling_row_0 always 0;
 * primitives whose AABB falls entirely outside the grid are dropped,
 * partially-visible primitives have their cell refs clipped to the
 * grid bounds. No scrollback, no eviction, no over-wide grid growth.
 *
 * Shares grid types, font cache, flyweight registry, complex-prim
 * factory, and staging buffers with scrolling-canvas via the embedded
 * `struct ydraw_canvas_common base`. See canvas-internal.h.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/complex-prim-types.h>
#include <yetty/ydraw-factory/complex-prim-factory.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/prim-iter.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ydraw/static-canvas.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>

/* Forward decl — vtable is constructed at the bottom of the file. */
static const struct yetty_ydraw_canvas_ops static_canvas_ops;

struct yetty_ydraw_static_canvas;
static struct yetty_ycore_void_result yetty_ydraw_static_canvas_clear(
    struct yetty_ydraw_static_canvas *canvas);

struct yetty_ydraw_static_canvas {
    struct yetty_ydraw_canvas *base;

    /* Lines sized to grid_size.rows at set_grid_size time; fixed thereafter.
     * Out-of-bounds writes are discarded. */
    struct ydraw_canvas_grid_line *lines;
    uint32_t                        lines_count;

    /* Per-envelope streaming state — see scrolling-canvas.c for the
     * detailed shape. Static-canvas has no scroll viewport to advance
     * on finalisation, so the tail is just attach-pass + font-map
     * release. */
    bool                                     env_active;
    struct yetty_ydraw_core_prim_iter       env_iter;
    struct ydraw_canvas_font_map            env_fonts_map;
    struct ydraw_canvas_buffer_attach_list  env_attach_list;
};

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ydraw_canvas_ptr_result yetty_ydraw_static_canvas_create(
    const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "context is NULL");
    }

    struct yetty_ydraw_static_canvas *canvas =
        calloc(1, sizeof(struct yetty_ydraw_static_canvas));
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "canvas alloc failed");
    }

    /* Polymorphic base: flyweight, factory, font cache, default font,
     * dirs/family/render-method. */
    struct yetty_ydraw_canvas_ptr_result base_res =
        ydraw_canvas_create(context, &static_canvas_ops, canvas);
    if (YETTY_IS_ERR(base_res)) {
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "static-canvas: base create failed", base_res);
    }
    canvas->base = base_res.value;

    return YETTY_OK(yetty_ydraw_canvas_ptr, base_res.value);
}

static void static_canvas_free_lines(struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas->lines) {
        return;
    }
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        struct yetty_ycore_void_result r =
            ydraw_canvas_grid_line_free(&canvas->lines[i], canvas->base->font_cache);
        if (YETTY_IS_ERR(r)) {
            yerror("static-canvas: grid_line_free at row %u failed: %s",
                   i, r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }
    free(canvas->lines);
    canvas->lines = NULL;
    canvas->lines_count = 0;
}

/* Vtable destroy — frees variant-specific state and the variant struct
 * itself. The polymorphic `yetty_ydraw_canvas_destroy` runs this first,
 * then frees the base and its shared state. */
static struct yetty_ycore_void_result static_destroy_impl(
    struct yetty_ydraw_canvas *base)
{
    struct yetty_ydraw_static_canvas *canvas = base->impl;
    if (!canvas) {
        return YETTY_OK_VOID();
    }
    if (canvas->env_active) {
        yetty_ydraw_core_prim_iter_destroy(&canvas->env_iter);
        ydraw_canvas_buffer_attach_free(&canvas->env_attach_list);
        ydraw_canvas_font_map_release_all(&canvas->env_fonts_map, base->font_cache);
        free(canvas->env_fonts_map.entries);
        canvas->env_active = false;
    }
    static_canvas_free_lines(canvas);
    free(canvas);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Configuration
 *===========================================================================*/

static struct yetty_ycore_void_result yetty_ydraw_static_canvas_set_grid_size(
    struct yetty_ydraw_static_canvas *canvas, struct yetty_ycore_grid_size size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    /* Resize wipes the lines — easier than migrating and yui chrome resizes
     * rarely. The dirty flag is set so the next render rebuilds staging. */
    static_canvas_free_lines(canvas);
    canvas->base->grid_size = size;

    if (size.rows > 0) {
        canvas->lines = calloc(size.rows, sizeof(struct ydraw_canvas_grid_line));
        if (!canvas->lines) {
            return YETTY_ERR(yetty_ycore_void, "static-canvas: lines alloc failed");
        }
        canvas->lines_count = size.rows;
        for (uint32_t i = 0; i < size.rows; i++) {
            struct yetty_ycore_void_result r =
                ydraw_canvas_grid_line_init(&canvas->lines[i]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "static-canvas: grid_line_init");
        }
    }

    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}


/*===========================================================================
 * Buffer ingestion — no cursor, no scroll, drop-if-doesn't-fit.
 *===========================================================================*/

/* Add a single SDF / complex primitive. Returns the bottom (base) grid
 * row for the primitive on success, or UINT32_MAX if the primitive was
 * dropped because its AABB falls entirely outside the grid. */
static struct uint32_result static_canvas_add_primitive(
    struct yetty_ydraw_static_canvas *canvas,
    const struct yetty_ydraw_core_prim_flyweight *fw)
{
    if (!fw || !fw->data || !fw->ops) {
        return YETTY_ERR(uint32, "invalid flyweight");
    }
    if (canvas->base->cell_size.width <= 0.0f ||
        canvas->base->cell_size.height <= 0.0f) {
        return YETTY_ERR(uint32, "cell size must be > 0");
    }
    if (canvas->base->grid_size.cols == 0 || canvas->base->grid_size.rows == 0) {
        return YETTY_ERR(uint32, "grid size is 0");
    }
    if (!fw->ops->aabb || !fw->ops->size) {
        return YETTY_ERR(uint32, "handler missing ops");
    }

    uint32_t prim_type = fw->data[0];

    struct rectangle_result aabb_res = fw->ops->aabb(fw->data);
    YETTY_RETURN_IF_ERR(uint32, aabb_res, "static-canvas: aabb");
    struct yetty_ycore_rectangle aabb = aabb_res.value;

    struct yetty_ycore_size_result size_res = fw->ops->size(fw->data);
    YETTY_RETURN_IF_ERR(uint32, size_res, "static-canvas: size");
    uint32_t word_count = size_res.value / sizeof(uint32_t);

    if (aabb.min.y > aabb.max.y) {
        float tmp = aabb.min.y;
        aabb.min.y = aabb.max.y;
        aabb.max.y = tmp;
    }

    /* Drop primitives that fall entirely outside the grid. Partial overlap
     * is fine — we clip the cell refs to the grid bounds below. */
    float    cell_h = canvas->base->cell_size.height;
    float    cell_w = canvas->base->cell_size.width;
    uint32_t grid_rows = canvas->base->grid_size.rows;
    uint32_t grid_cols = canvas->base->grid_size.cols;

    if (aabb.max.y < 0 || aabb.max.x < 0) {
        return YETTY_OK(uint32, UINT32_MAX);
    }
    if (aabb.min.y >= (float)grid_rows * cell_h ||
        aabb.min.x >= (float)grid_cols * cell_w) {
        return YETTY_OK(uint32, UINT32_MAX);
    }

    if (aabb.min.y < 0) aabb.min.y = 0;
    if (aabb.min.x < 0) aabb.min.x = 0;

    uint32_t row_max = (uint32_t)floorf(aabb.max.y / cell_h);
    if (row_max >= grid_rows) {
        row_max = grid_rows - 1;
    }
    uint32_t row_min = (uint32_t)floorf(aabb.min.y / cell_h);
    if (row_min > row_max) {
        return YETTY_OK(uint32, UINT32_MAX);
    }

    uint32_t col_max = (uint32_t)floorf(aabb.max.x / cell_w);
    if (col_max >= grid_cols) {
        col_max = grid_cols - 1;
    }
    uint32_t col_min = (uint32_t)floorf(aabb.min.x / cell_w);
    if (col_min > col_max) {
        return YETTY_OK(uint32, UINT32_MAX);
    }

    /* Prim payload lives on the bottom (base) row. rolling_row is always 0
     * for static-canvas — the shader uses (prim.rolling_row - row_origin)
     * and row_origin == 0, so the absolute canvas y stays as encoded. */
    struct ydraw_canvas_grid_line *base_line = &canvas->lines[row_max];
    struct uint32_result push_res = ydraw_canvas_grid_line_push_prim(
        base_line, 0, (const float *)fw->data, word_count);
    YETTY_RETURN_IF_ERR(uint32, push_res, "static-canvas: push_prim");
    uint32_t prim_index = push_res.value;

    for (uint32_t row = row_min; row <= row_max; row++) {
        struct ydraw_canvas_grid_line *line = &canvas->lines[row];
        struct yetty_ycore_void_result ec =
            ydraw_canvas_grid_line_ensure_cells(line, col_max + 1);
        YETTY_RETURN_IF_ERR(uint32, ec, "static-canvas: ensure_cells");
        uint16_t lines_ahead = (uint16_t)(row_max - row);
        for (uint32_t col = col_min; col <= col_max; col++) {
            struct ydraw_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_index};
            struct yetty_ycore_void_result rp =
                ydraw_canvas_prim_ref_array_push(&line->cells[col].refs, ref);
            YETTY_RETURN_IF_ERR(uint32, rp, "static-canvas: ref_array_push");
        }
    }

    if (yetty_ydraw_core_is_complex_type(prim_type)) {
        struct yetty_ydraw_core_figure_instance_ptr_result inst_res =
            yetty_ydraw_core_figure_factory_create_instance(
                canvas->base->figure_factory, fw->data,
                word_count * sizeof(uint32_t), 0);
        if (YETTY_IS_ERR(inst_res)) {
            return YETTY_ERR(uint32, "static-canvas: create_instance failed",
                             inst_res);
        }
        if (base_line->figure_count >= base_line->figure_capacity) {
            uint32_t new_cap = base_line->figure_capacity == 0
                                   ? 4
                                   : base_line->figure_capacity * 2;
            struct yetty_ydraw_core_figure_instance **grown = realloc(
                base_line->figures,
                new_cap * sizeof(struct yetty_ydraw_core_figure_instance *));
            if (!grown) {
                yetty_ydraw_core_figure_instance_destroy(inst_res.value);
                return YETTY_ERR(uint32, "realloc figures failed");
            }
            base_line->figures = grown;
            base_line->figure_capacity = new_cap;
        }
        base_line->figures[base_line->figure_count++] = inst_res.value;
    }

    canvas->base->dirty = true;
    return YETTY_OK(uint32, row_max);
}

/* Expand a TEXT_SPAN into per-glyph SDF primitives. Glyphs outside the
 * grid are dropped silently. Returns the highest grid row a glyph was
 * placed on; used by the per-buffer attach pass to keep the font cache
 * ref pinned to a visible line. */
static struct uint32_result static_canvas_expand_text_span(
    struct yetty_ydraw_static_canvas *canvas,
    const struct yetty_ydraw_core_text_span_prim_view *ts,
    struct yetty_ydraw_font *font, yetty_yfont_cache_handle font_handle)
{
    static uint32_t glyph_z_order = 0;

    float    cell_h = canvas->base->cell_size.height;
    float    cell_w = canvas->base->cell_size.width;
    uint32_t grid_rows = canvas->base->grid_size.rows;
    uint32_t grid_cols = canvas->base->grid_size.cols;

    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0) ? ts->font_size / base_size : 1.0f;
    float cursor_x = ts->x;
    uint32_t glyph_max_row = 0;
    bool any_placed = false;

    const uint8_t *ptr = (const uint8_t *)ts->text;
    const uint8_t *end = ptr + ts->text_len;

    while (ptr < end) {
        uint32_t cp = 0;
        if ((*ptr & 0x80) == 0) {
            cp = *ptr++;
        } else if ((*ptr & 0xE0) == 0xC0) {
            cp = (*ptr++ & 0x1F) << 6;
            if (ptr < end) cp |= (*ptr++ & 0x3F);
        } else if ((*ptr & 0xF0) == 0xE0) {
            cp = (*ptr++ & 0x0F) << 12;
            if (ptr < end) cp |= (*ptr++ & 0x3F) << 6;
            if (ptr < end) cp |= (*ptr++ & 0x3F);
        } else if ((*ptr & 0xF8) == 0xF0) {
            cp = (*ptr++ & 0x07) << 18;
            if (ptr < end) cp |= (*ptr++ & 0x3F) << 12;
            if (ptr < end) cp |= (*ptr++ & 0x3F) << 6;
            if (ptr < end) cp |= (*ptr++ & 0x3F);
        } else {
            ptr++;
            continue;
        }

        struct uint32_result gi_res = font->ops->get_glyph_index(font, cp);
        YETTY_RETURN_IF_ERR(uint32, gi_res,
                            "expand_text_span: get_glyph_index");
        uint32_t glyph_index = gi_res.value;

        struct yetty_yrender_gpu_resource_set_result rs_res =
            font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(uint32, rs_res,
                            "expand_text_span: get_gpu_resource_set");
        const struct yetty_ydraw_core_gpu_resource_set *rs = rs_res.value;
        if (rs->buffer_count == 0 || !rs->buffers[0].data) {
            return YETTY_ERR(uint32,
                             "expand_text_span: font resource set has no glyph metadata buffer");
        }

        const float *meta = (const float *)rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(rs->buffers[0].size / (6 * sizeof(float)));
        if (glyph_index >= meta_count) {
            return YETTY_ERR(uint32,
                             "expand_text_span: glyph_index out of metadata range");
        }
        const float *gm = meta + glyph_index * 6;
        float size_x = gm[0], size_y = gm[1];
        float bearing_x = gm[2], bearing_y = gm[3];
        float advance = gm[4];

        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * scale;
            continue;
        }

        float gx = cursor_x + bearing_x * scale;
        float gy = ts->y - bearing_y * scale;
        float gw = size_x * scale;
        float gh = size_y * scale;

        /* Glyph SDF prim (7 words). slot+1 in the high half encodes the
         * font cache slot; 0 means "use default". */
        uint32_t slot =
            (font_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) ? font_handle : 0u;
        float glyph_data[YDRAW_GLYPH_WORDS];
        uint32_t tmp;
        tmp = YETTY_YSDF_GLYPH;
        memcpy(&glyph_data[0], &tmp, sizeof(float));
        tmp = glyph_z_order++;
        memcpy(&glyph_data[1], &tmp, sizeof(float));
        glyph_data[2] = gx;
        glyph_data[3] = gy;
        glyph_data[4] = ts->font_size;
        uint32_t packed_gf =
            (glyph_index & 0xFFFF) | (((uint32_t)(slot + 1) & 0xFFFF) << 16);
        memcpy(&glyph_data[5], &packed_gf, sizeof(float));
        memcpy(&glyph_data[6], &ts->color, sizeof(float));

        /* Drop if entirely outside the grid. */
        if (gy + gh < 0 || gx + gw < 0 ||
            gy >= (float)grid_rows * cell_h || gx >= (float)grid_cols * cell_w) {
            cursor_x += advance * scale + ts->char_spacing;
            if (cp == 0x20) cursor_x += ts->word_spacing;
            continue;
        }

        uint32_t row_max = (uint32_t)floorf((gy + gh) / cell_h);
        if (row_max >= grid_rows) row_max = grid_rows - 1;
        float clipped_gy = gy < 0 ? 0 : gy;
        uint32_t row_min = (uint32_t)floorf(clipped_gy / cell_h);
        if (row_min > row_max) {
            cursor_x += advance * scale + ts->char_spacing;
            if (cp == 0x20) cursor_x += ts->word_spacing;
            continue;
        }

        uint32_t col_max = (uint32_t)floorf((gx + gw) / cell_w);
        if (col_max >= grid_cols) col_max = grid_cols - 1;
        float clipped_gx = gx < 0 ? 0 : gx;
        uint32_t col_min = (uint32_t)floorf(clipped_gx / cell_w);
        if (col_min > col_max) {
            cursor_x += advance * scale + ts->char_spacing;
            if (cp == 0x20) cursor_x += ts->word_spacing;
            continue;
        }

        struct ydraw_canvas_grid_line *base_line = &canvas->lines[row_max];
        struct uint32_result push_res = ydraw_canvas_grid_line_push_prim(
            base_line, 0, glyph_data, YDRAW_GLYPH_WORDS);
        YETTY_RETURN_IF_ERR(uint32, push_res,
                            "expand_text_span: push_prim");
        uint32_t prim_idx = push_res.value;

        for (uint32_t row = row_min; row <= row_max; row++) {
            struct ydraw_canvas_grid_line *line = &canvas->lines[row];
            struct yetty_ycore_void_result ec =
                ydraw_canvas_grid_line_ensure_cells(line, col_max + 1);
            YETTY_RETURN_IF_ERR(uint32, ec, "expand_text_span: ensure_cells");
            uint16_t lines_ahead = (uint16_t)(row_max - row);
            for (uint32_t col = col_min; col <= col_max; col++) {
                struct ydraw_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_idx};
                struct yetty_ycore_void_result rp =
                    ydraw_canvas_prim_ref_array_push(&line->cells[col].refs, ref);
                YETTY_RETURN_IF_ERR(uint32, rp,
                                    "expand_text_span: ref_array_push");
            }
        }

        if (row_max > glyph_max_row) glyph_max_row = row_max;
        any_placed = true;

        cursor_x += advance * scale + ts->char_spacing;
        if (cp == 0x20) cursor_x += ts->word_spacing;
    }

    return YETTY_OK(uint32, any_placed ? glyph_max_row : 0);
}

/* Attach a cache handle to lines[row]. If the handle is already attached
 * to some other line, migrate (no refcount change); else add a fresh
 * entry and bump the cache refcount. */
static struct yetty_ycore_void_result static_canvas_attach_handle(
    struct yetty_ydraw_static_canvas *canvas,
    yetty_yfont_cache_handle handle, uint32_t row)
{
    if (handle == YETTY_YFONT_CACHE_HANDLE_INVALID ||
        handle == canvas->base->default_handle || row >= canvas->lines_count) {
        return YETTY_OK_VOID();
    }
    struct ydraw_canvas_grid_line *target = &canvas->lines[row];

    for (uint32_t li = 0; li < canvas->lines_count; li++) {
        struct ydraw_canvas_grid_line *l = &canvas->lines[li];
        for (uint32_t fi = 0; fi < l->font_count; fi++) {
            if (l->fonts[fi].handle == handle) {
                if (li == row) return YETTY_OK_VOID();
                if (target->font_count >= target->font_capacity) {
                    uint32_t new_cap =
                        target->font_capacity == 0 ? 4 : target->font_capacity * 2;
                    struct ydraw_canvas_font_entry *grown = realloc(
                        target->fonts,
                        new_cap * sizeof(struct ydraw_canvas_font_entry));
                    if (!grown) {
                        return YETTY_ERR(yetty_ycore_void,
                                         "static_canvas_attach_handle: fonts realloc failed (migrate)");
                    }
                    target->fonts = grown;
                    target->font_capacity = new_cap;
                }
                target->fonts[target->font_count++] = l->fonts[fi];
                l->fonts[fi] = l->fonts[--l->font_count];
                return YETTY_OK_VOID();
            }
        }
    }

    if (target->font_count >= target->font_capacity) {
        uint32_t new_cap =
            target->font_capacity == 0 ? 4 : target->font_capacity * 2;
        struct ydraw_canvas_font_entry *grown = realloc(
            target->fonts, new_cap * sizeof(struct ydraw_canvas_font_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void,
                             "static_canvas_attach_handle: fonts realloc failed (new)");
        }
        target->fonts = grown;
        target->font_capacity = new_cap;
    }
    yetty_yfont_cache_retain(canvas->base->font_cache, handle);
    target->fonts[target->font_count++].handle = handle;
    return YETTY_OK_VOID();
}

#if 0  /* DEAD: legacy buffer-based add_buffer for static-canvas.
        * Replaced by streaming static_process_input_impl. Kept #if 0
        * for a release as reference; delete after yui content lands. */
static struct yetty_ycore_void_result yetty_ydraw_static_canvas_add_buffer_legacy(
    struct yetty_ydraw_static_canvas *canvas, struct yetty_ydraw_core_draw_list *buffer)
{
    struct yetty_ydraw_core_primitive_iter_result iter_res =
        yetty_ydraw_core_draw_list_prim_first(buffer, canvas->base->flyweight_registry);
    bool has_primitives = YETTY_IS_OK(iter_res);

    if (!has_primitives) {
        canvas->base->dirty = true;
        return YETTY_OK_VOID();
    }

    struct ydraw_canvas_font_map fonts_map;
    ydraw_canvas_font_map_init(&fonts_map);

    struct ydraw_canvas_buffer_attach_list attach_list;
    ydraw_canvas_buffer_attach_init(&attach_list);

    struct yetty_ydraw_core_primitive_iter iter = iter_res.value;

    while (1) {
        uint32_t prim_type = iter.fw.data[0];

        if (prim_type <= YETTY_YDRAW_CMD_END) {
            if (prim_type == YETTY_YDRAW_CMD_ZERO) {
                yetty_ydraw_static_canvas_clear(canvas);
            }
        } else if (prim_type == YETTY_YDRAW_TYPE_FONT) {
            struct yetty_ydraw_core_font_prim_view fv;
            if (yetty_ydraw_core_font_prim_parse(iter.fw.data, &fv) == 0 &&
                fv.font_id >= 0) {
                char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
                size_t hl =
                    fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
                memcpy(hint, fv.name, hl);
                hint[hl] = '\0';
                char hex[17];
                struct yetty_ycore_void_result er =
                    ydraw_canvas_ensure_blob_font_cdb(
                        canvas->base, fv.ttf, fv.ttf_len, hint, hex);
                if (YETTY_IS_ERR(er)) {
                    yetty_ycore_error_destroy(er.error);
                } else {
                    ydraw_canvas_font_map_grow(&fonts_map, (uint32_t)fv.font_id + 1);
                    memcpy(fonts_map.entries[fv.font_id].hex, hex, 17);
                    fonts_map.entries[fv.font_id].declared = true;
                }
            }
        } else if (prim_type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
            struct yetty_ydraw_core_text_span_prim_view tv;
            if (yetty_ydraw_core_text_span_prim_parse(iter.fw.data, &tv) == 0) {
                struct yetty_ydraw_font *font = NULL;
                yetty_yfont_cache_handle handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
                if (tv.font_id >= 0 && (uint32_t)tv.font_id < fonts_map.capacity) {
                    struct ydraw_canvas_font_map_entry *e =
                        &fonts_map.entries[tv.font_id];
                    if (e->resolved) {
                        font = e->font;
                        handle = e->handle;
                    } else if (e->declared) {
                        struct yetty_yfont_cache_ref_result rr =
                            ydraw_canvas_resolve_blob_font_handle(
                                canvas->base, e->hex);
                        if (YETTY_IS_OK(rr)) {
                            e->font = rr.value.font;
                            e->handle = rr.value.handle;
                            e->resolved = true;
                            font = e->font;
                            handle = e->handle;
                        } else {
                            yetty_ycore_error_destroy(rr.error);
                            e->declared = false;
                        }
                    }
                }
                if (!font) {
                    font = canvas->base->default_font;
                    handle = canvas->base->default_handle;
                }
                if (font) {
                    struct uint32_result gmr =
                        static_canvas_expand_text_span(canvas, &tv, font, handle);
                    if (YETTY_IS_OK(gmr)) {
                        ydraw_canvas_buffer_attach_note(&attach_list, handle,
                                                         gmr.value);
                    } else {
                        yetty_ycore_error_destroy(gmr.error);
                    }
                }
            }
        } else {
            struct uint32_result prim_res = static_canvas_add_primitive(canvas, &iter.fw);
            if (YETTY_IS_ERR(prim_res)) {
                yerror("static-canvas: add_primitive failed (continuing): %s",
                       prim_res.error.msg);
                yetty_ycore_error_destroy(prim_res.error);
            }
        }

        struct yetty_ydraw_core_primitive_iter_result nx =
            yetty_ydraw_core_draw_list_prim_next(buffer, canvas->base->flyweight_registry,
                                               &iter);
        if (YETTY_IS_ERR(nx)) {
            break;
        }
        iter = nx.value;
    }

    for (uint32_t i = 0; i < attach_list.count; i++) {
        static_canvas_attach_handle(canvas, attach_list.entries[i].handle,
                                    attach_list.entries[i].max_row);
    }
    ydraw_canvas_buffer_attach_free(&attach_list);

    ydraw_canvas_font_map_release_all(&fonts_map, canvas->base->font_cache);
    free(fonts_map.entries);

    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}
#endif /* legacy static add_buffer */

/*===========================================================================
 * Packed GPU format
 *===========================================================================*/

static struct yetty_ycore_void_result yetty_ydraw_static_canvas_rebuild_grid(
    struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->base->dirty && canvas->base->grid_staging_count > 0) {
        return YETTY_OK_VOID();
    }

    uint32_t grid_w = canvas->base->grid_size.cols;
    uint32_t grid_h = canvas->base->grid_size.rows;

    if (grid_w == 0 || grid_h == 0) {
        canvas->base->grid_staging_count = 0;
        canvas->base->dirty = false;
        return YETTY_OK_VOID();
    }

    /* Prefix-sum of prim counts across all lines. The GPU prim buffer holds
     * every prim so cells can reference prims on lines further down via
     * lines_ahead. */
    uint32_t total_prims = 0;
    uint32_t *line_base_prim_idx = NULL;
    if (canvas->lines_count > 0) {
        line_base_prim_idx = malloc(canvas->lines_count * sizeof(uint32_t));
        if (!line_base_prim_idx) {
            return YETTY_ERR(yetty_ycore_void, "alloc line_base_prim_idx failed");
        }
        for (uint32_t i = 0; i < canvas->lines_count; i++) {
            line_base_prim_idx[i] = total_prims;
            total_prims += canvas->lines[i].prims.count;
        }
    }

    uint32_t num_cells = grid_w * grid_h;
    struct yetty_ycore_void_result es =
        ydraw_canvas_ensure_grid_staging(canvas->base, num_cells * 4);
    if (YETTY_IS_ERR(es)) {
        free(line_base_prim_idx);
        return YETTY_ERR(yetty_ycore_void,
                         "static rebuild_grid: ensure_grid_staging", es);
    }
    canvas->base->grid_staging_count = num_cells;

    for (uint32_t gpu_y = 0; gpu_y < grid_h; gpu_y++) {
        struct ydraw_canvas_grid_line *line =
            gpu_y < canvas->lines_count ? &canvas->lines[gpu_y] : NULL;
        uint32_t line_cell_count = line ? line->cell_count : 0;

        for (uint32_t x = 0; x < grid_w; x++) {
            uint32_t cell_idx = gpu_y * grid_w + x;

            struct yetty_ycore_void_result es2 = ydraw_canvas_ensure_grid_staging(
                canvas->base, canvas->base->grid_staging_count + 2);
            if (YETTY_IS_ERR(es2)) {
                free(line_base_prim_idx);
                return YETTY_ERR(yetty_ycore_void,
                                 "static rebuild_grid: ensure_grid_staging (header)",
                                 es2);
            }

            canvas->base->grid_staging[cell_idx] = canvas->base->grid_staging_count;
            uint32_t count_pos = canvas->base->grid_staging_count++;
            canvas->base->grid_staging[count_pos] = 0;
            uint32_t count = 0;

            if (line && x < line_cell_count) {
                struct ydraw_canvas_grid_cell *cell = &line->cells[x];
                for (uint32_t ri = 0; ri < cell->refs.count; ri++) {
                    struct ydraw_canvas_prim_ref *ref = &cell->refs.data[ri];
                    uint32_t bl = gpu_y + ref->lines_ahead;
                    if (bl < canvas->lines_count && line_base_prim_idx) {
                        struct yetty_ycore_void_result es3 =
                            ydraw_canvas_ensure_grid_staging(
                                canvas->base, canvas->base->grid_staging_count + 1);
                        if (YETTY_IS_ERR(es3)) {
                            free(line_base_prim_idx);
                            return YETTY_ERR(yetty_ycore_void,
                                             "static rebuild_grid: ensure_grid_staging (ref)",
                                             es3);
                        }
                        canvas->base->grid_staging[canvas->base->grid_staging_count++] =
                            line_base_prim_idx[bl] + ref->prim_index;
                        count++;
                    }
                }
            }
            canvas->base->grid_staging[count_pos] = count;
        }
    }

    free(line_base_prim_idx);
    canvas->base->dirty = false;
    return YETTY_OK_VOID();
}

static struct yetty_ydraw_prim_staging_result yetty_ydraw_static_canvas_build_prim_staging(
    struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_prim_staging, "canvas is NULL");
    }

    uint32_t prim_count = 0;
    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        struct ydraw_canvas_grid_line *line = &canvas->lines[i];
        for (uint32_t p = 0; p < line->prims.count; p++) {
            prim_count++;
            total_words += line->prims.data[p].word_count + 1;
        }
    }

    if (prim_count == 0) {
        canvas->base->prim_staging_count = 0;
        struct yetty_ydraw_prim_staging empty = {.data = NULL, .word_count = 0};
        return YETTY_OK(yetty_ydraw_prim_staging, empty);
    }

    uint32_t total_size = prim_count + total_words;
    struct yetty_ycore_void_result eps =
        ydraw_canvas_ensure_prim_staging(canvas->base, total_size);
    YETTY_RETURN_IF_ERR(yetty_ydraw_prim_staging, eps,
                        "static-canvas: ensure_prim_staging failed");

    uint32_t data_offset = 0;
    uint32_t prim_idx = 0;
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        struct ydraw_canvas_grid_line *line = &canvas->lines[i];
        for (uint32_t p = 0; p < line->prims.count; p++) {
            struct ydraw_canvas_prim_data *prim = &line->prims.data[p];
            canvas->base->prim_staging[prim_idx] = data_offset;
            canvas->base->prim_staging[prim_count + data_offset] = prim->rolling_row;
            const uint32_t *payload = line->arena + prim->arena_offset;
            memcpy(&canvas->base->prim_staging[prim_count + data_offset + 1], payload,
                   prim->word_count * sizeof(uint32_t));
            data_offset += prim->word_count + 1;
            prim_idx++;
        }
    }

    canvas->base->prim_staging_count = total_size;
    struct yetty_ydraw_prim_staging out = {.data = canvas->base->prim_staging,
                                            .word_count = total_size};
    return YETTY_OK(yetty_ydraw_prim_staging, out);
}

/*===========================================================================
 * State management
 *===========================================================================*/

static struct yetty_ycore_void_result yetty_ydraw_static_canvas_clear(
    struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    /* Free + re-init each line in place; preserves the lines[] array shape
     * so the next add_buffer doesn't need to reallocate. */
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        struct yetty_ycore_void_result r =
            ydraw_canvas_grid_line_free(&canvas->lines[i], canvas->base->font_cache);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "static-canvas clear: grid_line_free");
        struct yetty_ycore_void_result ir =
            ydraw_canvas_grid_line_init(&canvas->lines[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir,
                            "static-canvas clear: grid_line_init");
    }
    canvas->base->grid_staging_count = 0;
    canvas->base->prim_staging_count = 0;
    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

static uint32_t yetty_ydraw_static_canvas_primitive_count(
    struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        count += canvas->lines[i].prims.count;
    }
    return count;
}

static uint32_t yetty_ydraw_static_canvas_figure_count(
    struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        count += canvas->lines[i].figure_count;
    }
    return count;
}

static struct yetty_ydraw_core_figure_instance *
yetty_ydraw_static_canvas_get_figure(struct yetty_ydraw_static_canvas *canvas,
                                            uint32_t index)
{
    if (!canvas) return NULL;
    uint32_t current = 0;
    for (uint32_t i = 0; i < canvas->lines_count; i++) {
        struct ydraw_canvas_grid_line *line = &canvas->lines[i];
        if (index < current + line->figure_count) {
            return line->figures[index - current];
        }
        current += line->figure_count;
    }
    return NULL;
}

static void yetty_ydraw_static_canvas_for_each_glyph(
    struct yetty_ydraw_static_canvas *canvas,
    yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    if (!canvas || !visitor) return;
    /* rolling_row is 0 for every static-canvas prim, so the absolute canvas
     * y stored in word[3] of the glyph prim is already what visitors expect. */
    for (uint32_t li = 0; li < canvas->lines_count; li++) {
        const struct ydraw_canvas_grid_line *line = &canvas->lines[li];
        for (uint32_t pi = 0; pi < line->prims.count; pi++) {
            const struct ydraw_canvas_prim_data *pd = &line->prims.data[pi];
            if (pd->word_count < YDRAW_GLYPH_WORDS) continue;
            const uint32_t *words = line->arena + pd->arena_offset;
            uint32_t type_word;
            memcpy(&type_word, &words[0], sizeof(type_word));
            if (type_word != YETTY_YSDF_GLYPH) continue;
            float gx, gy;
            uint32_t packed;
            memcpy(&gx, &words[2], sizeof(gx));
            memcpy(&gy, &words[3], sizeof(gy));
            memcpy(&packed, &words[5], sizeof(packed));

            struct yetty_ydraw_glyph_view view;
            view.x = gx;
            view.y = gy;
            view.glyph_idx = packed & 0xFFFFu;
            uint32_t slot_plus_one = (packed >> 16) & 0xFFFFu;
            view.font_slot = slot_plus_one ? (int32_t)(slot_plus_one - 1) : -1;
            visitor(&view, user);
        }
    }
}

/*===========================================================================
 * Vtable thunks — bridge the polymorphic ops to the existing variant API.
 *
 * The polymorphic surface (yetty_ydraw_canvas_*) takes a base pointer;
 * the impl recovers the variant via base->impl and delegates to the
 * existing public function. process_input is a placeholder until the
 * streaming iterator refactor lands.
 *===========================================================================*/

static struct yetty_ycore_void_result static_set_grid_size_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_size size)
{
    return yetty_ydraw_static_canvas_set_grid_size(
        (struct yetty_ydraw_static_canvas *)base->impl, size);
}

/* Dispatch one prim into the static canvas. Any per-prim failure aborts
 * the envelope — the wire is corrupt or we're out of resources, and
 * continuing would mask the cause. */
static struct yetty_ycore_void_result static_dispatch_one(
    struct yetty_ydraw_static_canvas *canvas,
    const struct yetty_ydraw_core_prim_flyweight *fw)
{
    uint32_t prim_type = fw->data[0];

    if (prim_type <= YETTY_YDRAW_CMD_END) {
        if (prim_type == YETTY_YDRAW_CMD_ZERO) {
            struct yetty_ycore_void_result cr =
                yetty_ydraw_static_canvas_clear(canvas);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr,
                                "static dispatch: clear");
        }
        return YETTY_OK_VOID();
    }
    if (prim_type == YETTY_YDRAW_TYPE_FONT) {
        struct yetty_ydraw_core_font_prim_view fv;
        if (yetty_ydraw_core_font_prim_parse(fw->data, &fv) != 0 ||
            fv.font_id < 0) {
            return YETTY_OK_VOID();
        }
        char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
        size_t hl = fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
        memcpy(hint, fv.name, hl);
        hint[hl] = '\0';
        char hex[17];
        struct yetty_ycore_void_result er = ydraw_canvas_ensure_blob_font_cdb(
            canvas->base, fv.ttf, fv.ttf_len, hint, hex);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er,
                            "static dispatch: ensure_blob_font_cdb");
        struct yetty_ycore_void_result gr = ydraw_canvas_font_map_grow(
            &canvas->env_fonts_map, (uint32_t)fv.font_id + 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr,
                            "static dispatch: font_map_grow");
        memcpy(canvas->env_fonts_map.entries[fv.font_id].hex, hex, 17);
        canvas->env_fonts_map.entries[fv.font_id].declared = true;
        return YETTY_OK_VOID();
    }
    if (prim_type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        struct yetty_ydraw_core_text_span_prim_view tv;
        if (yetty_ydraw_core_text_span_prim_parse(fw->data, &tv) != 0) {
            return YETTY_OK_VOID();
        }
        struct yetty_ydraw_font *font = NULL;
        yetty_yfont_cache_handle handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
        if (tv.font_id >= 0 && (uint32_t)tv.font_id < canvas->env_fonts_map.capacity) {
            struct ydraw_canvas_font_map_entry *e =
                &canvas->env_fonts_map.entries[tv.font_id];
            if (e->resolved) {
                font = e->font;
                handle = e->handle;
            } else if (e->declared) {
                struct yetty_yfont_cache_ref_result rr =
                    ydraw_canvas_resolve_blob_font_handle(canvas->base, e->hex);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                                    "static dispatch: resolve_blob_font_handle");
                e->font = rr.value.font;
                e->handle = rr.value.handle;
                e->resolved = true;
                font = e->font;
                handle = e->handle;
            }
        }
        if (!font) {
            font = canvas->base->default_font;
            handle = canvas->base->default_handle;
        }
        if (!font) {
            return YETTY_OK_VOID();
        }
        struct uint32_result gmr =
            static_canvas_expand_text_span(canvas, &tv, font, handle);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gmr,
                            "static dispatch: expand_text_span");
        struct yetty_ycore_void_result an = ydraw_canvas_buffer_attach_note(
            &canvas->env_attach_list, handle, gmr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, an,
                            "static dispatch: buffer_attach_note");
        return YETTY_OK_VOID();
    }
    struct uint32_result prim_res = static_canvas_add_primitive(canvas, fw);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, prim_res,
                        "static dispatch: add_primitive");
    return YETTY_OK_VOID();
}

static void static_env_reset(struct yetty_ydraw_static_canvas *canvas)
{
    if (!canvas->env_active) {
        return;
    }
    yetty_ydraw_core_prim_iter_destroy(&canvas->env_iter);
    ydraw_canvas_buffer_attach_free(&canvas->env_attach_list);
    ydraw_canvas_font_map_release_all(&canvas->env_fonts_map, canvas->base->font_cache);
    free(canvas->env_fonts_map.entries);
    canvas->env_fonts_map.entries = NULL;
    canvas->env_fonts_map.capacity = 0;
    canvas->env_active = false;
}

static struct yetty_ycore_void_result static_process_input_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_ydraw_static_canvas *canvas = base->impl;

    if (!canvas->env_active) {
        struct yetty_ycore_void_result ir = yetty_ydraw_core_prim_iter_init(
            &canvas->env_iter, sm, canvas->base->flyweight_registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "static process_input: iter init");
        ydraw_canvas_font_map_init(&canvas->env_fonts_map);
        ydraw_canvas_buffer_attach_init(&canvas->env_attach_list);
        canvas->env_active = true;
    }

    for (;;) {
        struct yetty_ydraw_core_prim_iter_status_result sr =
            yetty_ydraw_core_prim_iter_next(&canvas->env_iter);
        if (YETTY_IS_ERR(sr)) {
            static_env_reset(canvas);
            return YETTY_ERR(yetty_ycore_void,
                             "static process_input: prim_iter_next failed", sr);
        }
        if (sr.value == YETTY_PRIM_ITER_WOULD_BLOCK) {
            return YETTY_OK_VOID();
        }
        if (sr.value == YETTY_PRIM_ITER_DONE) {
            break;
        }
        struct yetty_ycore_void_result dr =
            static_dispatch_one(canvas, &canvas->env_iter.fw);
        if (YETTY_IS_ERR(dr)) {
            static_env_reset(canvas);
            return YETTY_ERR(yetty_ycore_void,
                             "static process_input: dispatch_one failed", dr);
        }
    }

    /* End of envelope — run attach pass; no viewport scroll, no eviction. */
    for (uint32_t i = 0; i < canvas->env_attach_list.count; i++) {
        struct yetty_ycore_void_result ar = static_canvas_attach_handle(
            canvas, canvas->env_attach_list.entries[i].handle,
            canvas->env_attach_list.entries[i].max_row);
        if (YETTY_IS_ERR(ar)) {
            static_env_reset(canvas);
            return YETTY_ERR(yetty_ycore_void,
                             "static process_input: attach_handle failed", ar);
        }
    }
    canvas->base->dirty = true;
    static_env_reset(canvas);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result static_clear_impl(
    struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_static_canvas_clear(
        (struct yetty_ydraw_static_canvas *)base->impl);
}

static uint32_t static_primitive_count_impl(const struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_static_canvas_primitive_count(
        (struct yetty_ydraw_static_canvas *)base->impl);
}

static struct yetty_ycore_void_result static_rebuild_grid_impl(
    struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_static_canvas_rebuild_grid(
        (struct yetty_ydraw_static_canvas *)base->impl);
}

static struct yetty_ydraw_prim_staging_result static_build_prim_staging_impl(
    struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_static_canvas_build_prim_staging(
        (struct yetty_ydraw_static_canvas *)base->impl);
}

static uint32_t static_prim_gpu_size_impl(const struct yetty_ydraw_canvas *base)
{
    /* Static-canvas has no dedicated prim_gpu_size; compute from base
     * staging count (matches the prim_staging layout built by
     * build_prim_staging). */
    if (!base) return 0;
    return base->prim_staging_count * (uint32_t)sizeof(uint32_t);
}

static uint32_t static_figure_count_impl(const struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_static_canvas_figure_count(
        (struct yetty_ydraw_static_canvas *)base->impl);
}

static struct yetty_ydraw_core_figure_instance *static_get_figure_impl(
    const struct yetty_ydraw_canvas *base, uint32_t index)
{
    return yetty_ydraw_static_canvas_get_figure(
        (struct yetty_ydraw_static_canvas *)base->impl, index);
}

static void static_for_each_glyph_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    yetty_ydraw_static_canvas_for_each_glyph(
        (struct yetty_ydraw_static_canvas *)base->impl,
        visitor, user);
}

/* Scroll/cursor ops: no-ops on static-canvas (cursor pinned at (0,0); no
 * scrollback). The public wrappers in canvas.c return OK without
 * dispatching when these are NULL, but we wire OK-returning stubs for
 * predictability. */
static struct yetty_ycore_void_result static_set_cursor_pos_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_cursor_pos pos)
{
    (void)base;
    (void)pos;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result static_scroll_lines_impl(
    struct yetty_ydraw_canvas *base, uint16_t num_lines)
{
    (void)base;
    (void)num_lines;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result static_set_view_top_impl(
    struct yetty_ydraw_canvas *base, bool active, uint32_t view_top)
{
    (void)base;
    (void)active;
    (void)view_top;
    return YETTY_OK_VOID();
}

static uint32_t static_rolling_row_0_impl(struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static uint32_t static_live_rolling_row_0_impl(struct yetty_ydraw_canvas *base)
{
    (void)base;
    return 0;
}

static struct yetty_ycore_void_result static_set_scroll_callback_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_scroll_callback callback, void *userdata)
{
    (void)base;
    (void)callback;
    (void)userdata;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result static_set_cursor_callback_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_cursor_callback callback, void *userdata)
{
    (void)base;
    (void)callback;
    (void)userdata;
    return YETTY_OK_VOID();
}

static const struct yetty_ydraw_canvas_ops static_canvas_ops = {
    .name               = "static",
    .destroy            = static_destroy_impl,
    .set_grid_size      = static_set_grid_size_impl,
    .process_input      = static_process_input_impl,
    .clear              = static_clear_impl,
    .primitive_count    = static_primitive_count_impl,
    .rebuild_grid       = static_rebuild_grid_impl,
    .build_prim_staging = static_build_prim_staging_impl,
    .prim_gpu_size      = static_prim_gpu_size_impl,
    .figure_count = static_figure_count_impl,
    .get_figure   = static_get_figure_impl,
    .for_each_glyph     = static_for_each_glyph_impl,
    .set_cursor_pos     = static_set_cursor_pos_impl,
    .scroll_lines       = static_scroll_lines_impl,
    .set_view_top       = static_set_view_top_impl,
    .rolling_row_0      = static_rolling_row_0_impl,
    .live_rolling_row_0 = static_live_rolling_row_0_impl,
    .set_scroll_callback = static_set_scroll_callback_impl,
    .set_cursor_callback = static_set_cursor_callback_impl,
};
