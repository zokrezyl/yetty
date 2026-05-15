// YPaint Canvas - Implementation
// Rolling offset approach for O(1) scrolling

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/yplatform/compat.h>
#include <yetty/yplatform/fs.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/buffer.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/complex-prim-types.h>
#include <yetty/ydraw-factory/complex-prim-factory.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/prim-iter.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/ydraw/flyweight.h>
#include <yetty/ydraw/scrolling-canvas.h>
#include <yetty/ydraw/scrollbuffer.h>
#include "canvas-internal.h"
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yfont/raster-font.h>
#if YETTY_HAS_YMSDF_GEN
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ymsdf/generator.h>
#endif
#include <yetty/ysdf/types.gen.h>
#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yimage/yimage-gen.h>
#if YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh-gen.h>
#endif
#include <yetty/ytrace/ytrace.h>

#include <yetty/yplatform/paths.h>

/* Glyph primitive type (not in ysdf types.gen.h since not SDF) */
#define YETTY_YSDF_GLYPH 200

/* Glyph primitive: type, z_order, x, y, font_size, packed(glyph_idx|font_id), color */
#define YDRAW_GLYPH_WORDS 7

/* Canvas structure — variant. `base` owns the polymorphic canvas (created
 * via ydraw_canvas_create and freed by the polymorphic destroy). Scroll-
 * specific state lives directly here. */
struct yetty_ydraw_scrolling_canvas {
    struct yetty_ydraw_canvas *base;

    /* Cursor (screen-row offset from rolling_row_0). */
    uint16_t cursor_col;
    uint16_t cursor_row;

    /* Rolling row of visible line 0 (increments on scroll). Always tracks
     * the *live* viewport top — never reset by scrollback view. */
    uint32_t rolling_row_0;

    /* Scrollback view override. While active, the shader uniform and
     * rebuild_grid use view_top_override instead of rolling_row_0, so the
     * user sees a frozen historical viewport even as rolling_row_0
     * advances in the background due to new content. */
    bool     view_top_override_active;
    uint32_t view_top_override;

    /* Lines (grown on demand by canvas_ensure_lines). */
    struct ydraw_canvas_line_buffer lines;

    /* Scroll callback (called when add_buffer triggers a viewport scroll). */
    yetty_ydraw_canvas_scroll_callback   scroll_callback;
    void                                 *scroll_callback_user_data;

    /* Cursor set callback (when cursor moves without scroll). */
    yetty_ydraw_canvas_cursor_callback   cursor_set_callback;
    void                                 *cursor_set_callback_user_data;

    /* Scrollbuffer: lines whose absolute canvas-row index has fallen below
     * the live viewport are serialised to a compact binary form here, and
     * their expanded grid_line content (prims/arena/cells) is freed.
     * `sb_offsets[i]` is the byte offset of line `i`'s record in the
     * scrollbuffer, or SB_OFFSET_UNSET if the line still lives in
     * canvas->lines.lines[i] in expanded form. */
    struct yetty_ydraw_scrollbuffer scrollbuffer;
    uint32_t *sb_offsets;
    uint32_t  sb_offsets_count;
    uint32_t  sb_offsets_capacity;

    /* Per-envelope streaming state. Lives on the variant struct so the
     * canvas resumes correctly after process_input returns mid-envelope.
     * `env_active` is true between the first prim_iter init and the
     * end-of-envelope finalisation. */
    bool                                     env_active;
    struct yetty_ydraw_core_prim_iter       env_iter;
    struct ydraw_canvas_font_map            env_fonts_map;
    struct ydraw_canvas_buffer_attach_list  env_attach_list;
    uint32_t                                 env_initial_canvas_line;
    uint32_t                                 env_max_row_seen;
};

#define SB_OFFSET_UNSET 0xFFFFFFFFu

/* Forward decls — bodies live next to the scrollbuffer machinery; the
 * mutation paths above call into them. canvas_dirty_line restores any
 * previously-evicted content into the expanded form before clearing
 * sb_offsets so the line's history is preserved across re-mutation. */
static void canvas_dirty_line(struct yetty_ydraw_scrolling_canvas *canvas, uint32_t idx);
static struct yetty_ycore_void_result canvas_restore_line(struct yetty_ydraw_scrolling_canvas *canvas,
                                                          uint32_t idx);

/* Forward decls for the now-static helpers used cross-function inside this
 * compilation unit. */
static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_clear(
    struct yetty_ydraw_scrolling_canvas *canvas);
static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_scroll_lines(
    struct yetty_ydraw_scrolling_canvas *canvas, uint16_t num_lines);
static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_cursor_pos(
    struct yetty_ydraw_scrolling_canvas *canvas, struct yetty_ycore_grid_cursor_pos pos);

/* Vtable forward decl + impl methods (defined throughout this file). The
 * ops table itself is constructed at the bottom of the file. */
static const struct yetty_ydraw_canvas_ops scrolling_canvas_ops;

/* Grow canvas->lines so that index `min_count - 1` is addressable. New
 * lines are initialised empty (no prims, no cells). Scrolling-specific —
 * the line buffer grows as content arrives past the live viewport. */
static struct yetty_ycore_void_result canvas_ensure_lines(
    struct yetty_ydraw_scrolling_canvas *canvas, uint32_t min_count)
{
    struct ydraw_canvas_line_buffer *buf = &canvas->lines;

    if (min_count > buf->capacity) {
        uint32_t new_cap =
            buf->capacity == 0 ? YDRAW_CANVAS_INITIAL_LINE_CAPACITY : buf->capacity;
        while (new_cap < min_count) {
            new_cap *= 2;
        }

        struct ydraw_canvas_grid_line *new_lines =
            realloc(buf->lines, new_cap * sizeof(struct ydraw_canvas_grid_line));
        if (!new_lines) {
            return YETTY_ERR(yetty_ycore_void, "realloc failed for line buffer");
        }
        buf->lines = new_lines;
        buf->capacity = new_cap;
    }

    while (buf->count < min_count) {
        struct yetty_ycore_void_result r =
            ydraw_canvas_grid_line_init(&buf->lines[buf->count]);
        if (!r.ok) {
            return r;
        }
        buf->count++;
    }
    return YETTY_OK_VOID();
}

//=============================================================================
// Canvas implementation
//=============================================================================

struct yetty_ydraw_canvas_ptr_result yetty_ydraw_scrolling_canvas_create(
    const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "context is NULL");
    }

    struct yetty_ydraw_scrolling_canvas *canvas =
        calloc(1, sizeof(struct yetty_ydraw_scrolling_canvas));
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "canvas alloc failed");
    }

    canvas->rolling_row_0 = 0;

    ydraw_canvas_line_buffer_init(&canvas->lines);
    yetty_ydraw_scrollbuffer_init(&canvas->scrollbuffer);
    canvas->sb_offsets = NULL;
    canvas->sb_offsets_count = 0;
    canvas->sb_offsets_capacity = 0;

    /* Polymorphic base: flyweight, factory, font cache, default font,
     * dirs/family/render-method. The base owns the canvas vtable; we wire
     * scrolling_canvas_ops and pass `canvas` as the impl back-pointer so
     * vtable methods recover the variant via base->impl. */
    struct yetty_ydraw_canvas_ptr_result base_res =
        ydraw_canvas_create(context, &scrolling_canvas_ops, canvas);
    if (YETTY_IS_ERR(base_res)) {
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr,
                         "scrolling-canvas: base create failed", base_res);
    }
    canvas->base = base_res.value;

    return YETTY_OK(yetty_ydraw_canvas_ptr, base_res.value);
}

/* Vtable destroy — frees variant-specific state and the variant struct
 * itself. The polymorphic `yetty_ydraw_canvas_destroy` runs this first,
 * then frees the base and its shared state. */
static struct yetty_ycore_void_result scrolling_destroy_impl(
    struct yetty_ydraw_canvas *base)
{
    struct yetty_ydraw_scrolling_canvas *canvas = base->impl;
    if (!canvas) {
        return YETTY_OK_VOID();
    }
    /* Release any in-flight envelope state — torn-down mid-envelope
     * tears down the iter's scratch and the per-envelope font / attach
     * tracking too. */
    if (canvas->env_active) {
        yetty_ydraw_core_prim_iter_destroy(&canvas->env_iter);
        ydraw_canvas_buffer_attach_free(&canvas->env_attach_list);
        ydraw_canvas_font_map_release_all(&canvas->env_fonts_map, base->font_cache);
        free(canvas->env_fonts_map.entries);
        canvas->env_active = false;
    }
    struct yetty_ycore_void_result res =
        ydraw_canvas_line_buffer_free(&canvas->lines, base->font_cache);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "scrolling destroy: lines free");
    yetty_ydraw_scrollbuffer_free(&canvas->scrollbuffer);
    free(canvas->sb_offsets);
    free(canvas);
    return YETTY_OK_VOID();
}

//=============================================================================
// Configuration
//=============================================================================

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_cell_size(struct yetty_ydraw_scrolling_canvas *canvas,
                                                                 struct yetty_ycore_pixel_size size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (size.width <= 0.0f || size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "cell size must be > 0");
    }
    canvas->base->cell_size = size;
    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_grid_size(struct yetty_ydraw_scrolling_canvas *canvas,
                                                                 struct yetty_ycore_grid_size size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->base->grid_size = size;
    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

//=============================================================================
// Accessors
//=============================================================================

static struct yetty_ycore_pixel_size yetty_ydraw_scrolling_canvas_cell_get_pixel_size(
    struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_pixel_size){0, 0};
    }
    return canvas->base->cell_size;
}

static struct yetty_ycore_grid_size yetty_ydraw_scrolling_canvas_get_grid_size(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_grid_size){0, 0};
    }
    return canvas->base->grid_size;
}

//=============================================================================
// Cursor
//=============================================================================

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_cursor_pos(
    struct yetty_ydraw_scrolling_canvas *canvas, struct yetty_ycore_grid_cursor_pos pos)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->cursor_col = pos.cols;
    canvas->cursor_row = pos.rows;
    return YETTY_OK_VOID();
}

//=============================================================================
// Rolling offset
//=============================================================================

/* Effective viewport top: returns the override during scrollback view,
 * otherwise the live rolling_row_0. Both rebuild_grid and the shader
 * uniform must read through this so the GPU and the cell layout stay in
 * sync (the shader's y_offset = (prim.rolling_row - row0) needs row0 to
 * match the canvas-line that gpu_y=0 was filled from). */
static uint32_t canvas_effective_view_top(const struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (canvas->view_top_override_active) {
        return canvas->view_top_override;
    }
    return canvas->rolling_row_0;
}

static uint32_t yetty_ydraw_scrolling_canvas_rolling_row_0(struct yetty_ydraw_scrolling_canvas *canvas)
{
    return canvas ? canvas_effective_view_top(canvas) : 0;
}

static uint32_t yetty_ydraw_scrolling_canvas_live_rolling_row_0(struct yetty_ydraw_scrolling_canvas *canvas)
{
    return canvas ? canvas->rolling_row_0 : 0;
}

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_view_top(struct yetty_ydraw_scrolling_canvas *canvas,
                                                                bool active, uint32_t view_top)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->view_top_override_active = active;
    canvas->view_top_override = view_top;
    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

//=============================================================================
// Primitive management
//=============================================================================

// Add a single primitive (internal)
// Returns the grid_line (bottom row of AABB) for this primitive
static struct uint32_result add_primitive_internal(
    struct yetty_ydraw_scrolling_canvas *canvas,
    const struct yetty_ydraw_core_prim_flyweight *fw)
{
    if (!canvas) {
        return YETTY_ERR(uint32, "canvas is NULL");
    }
    if (!fw || !fw->data || !fw->ops) {
        return YETTY_ERR(uint32, "invalid flyweight");
    }
    if (canvas->base->cell_size.height <= 0.0f) {
        return YETTY_ERR(uint32, "cell_height <= 0");
    }
    if (canvas->base->cell_size.width <= 0.0f) {
        return YETTY_ERR(uint32, "cell_width <= 0");
    }

    if (!fw->ops->aabb || !fw->ops->size) {
        return YETTY_ERR(uint32, "handler missing ops");
    }

    uint32_t prim_type = fw->data[0];
    ydebug("add_primitive_internal: START type=0x%08x", prim_type);

    struct rectangle_result aabb_res = fw->ops->aabb(fw->data);
    if (YETTY_IS_ERR(aabb_res)) {
        yerror("add_primitive_internal: aabb failed: %s", aabb_res.error.msg);
        return YETTY_ERR(uint32, aabb_res.error.msg);
    }
    struct yetty_ycore_rectangle aabb = aabb_res.value;

    struct yetty_ycore_size_result size_res = fw->ops->size(fw->data);
    if (YETTY_IS_ERR(size_res)) {
        yerror("add_primitive_internal: size failed: %s", size_res.error.msg);
        return YETTY_ERR(uint32, size_res.error.msg);
    }
    uint32_t word_count = size_res.value / sizeof(uint32_t);

    ydebug("add_primitive_internal: type=0x%08x aabb=[%.1f,%.1f,%.1f,%.1f] words=%u", prim_type,
           aabb.min.x, aabb.min.y, aabb.max.x, aabb.max.y, word_count);

    if (aabb.min.y > aabb.max.y) {
        yerror("BUG: inverted AABB! min.y=%.1f > max.y=%.1f", aabb.min.y, aabb.max.y);
        float tmp = aabb.min.y;
        aabb.min.y = aabb.max.y;
        aabb.max.y = tmp;
    }

    /* cursor_row is a screen-row index relative to the viewport top
   * (rolling_row_0). The cursor's absolute canvas-line is the sum. */
    uint32_t cursor_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;

    /* Cap the storage AABB so that canvas-line indices never go below 0.
     * A bezier (or any prim) whose AABB extends above the absolute canvas
     * origin would produce a row index < 0, which cast to uint32_t wraps to
     * ~4 billion and hangs canvas_ensure_lines.  The primitive's actual
     * coordinate data is untouched; only the indexing bounding box is clamped. */
    float min_valid_y = -(float)cursor_canvas_line * canvas->base->cell_size.height;
    if (aabb.max.y < min_valid_y)
        aabb.max.y = min_valid_y;
    if (aabb.min.y < min_valid_y)
        aabb.min.y = min_valid_y;

    /* Half-open: row R covers y in [R*cell_h, (R+1)*cell_h). A primitive
     * whose max_y sits exactly on a row boundary K*cell_h ends at the top
     * edge of row K and paints zero pixels of it — last touched row is
     * K-1. ceil-minus-one gives the correct count for both boundary and
     * mid-row cases. */
    float max_rows = aabb.max.y / canvas->base->cell_size.height;
    uint32_t primitive_max_in_rows = (max_rows > 0.0f)
                                         ? ((uint32_t)ceilf(max_rows) - 1u)
                                         : 0u;

    uint32_t primitive_grid_line = cursor_canvas_line + primitive_max_in_rows;
    uint32_t primitive_rolling_row = cursor_canvas_line;

    canvas_ensure_lines(canvas, primitive_grid_line + 1);

    struct ydraw_canvas_grid_line *base_line =
        ydraw_canvas_line_buffer_get(&canvas->lines, primitive_grid_line);
    if (!base_line) {
        return YETTY_ERR(uint32, "line_buffer_get returned NULL");
    }
    canvas_dirty_line(canvas, primitive_grid_line);

    uint32_t prim_index = ydraw_canvas_grid_line_push_prim(base_line, primitive_rolling_row,
                                              (const float *)fw->data, word_count);
    if (prim_index == UINT32_MAX) {
        return YETTY_ERR(uint32, "grid_line_push_prim failed");
    }

    uint32_t prim_col_min = (uint32_t)(aabb.min.x / canvas->base->cell_size.width);
    uint32_t prim_col_max = (uint32_t)(aabb.max.x / canvas->base->cell_size.width);

    int32_t row_min_rel = (int32_t)floorf(aabb.min.y / canvas->base->cell_size.height);
    /* Same half-open convention as primitive_max_in_rows above: an AABB
     * ending exactly on row K's top edge does not touch row K. */
    int32_t row_max_rel = (int32_t)ceilf(aabb.max.y / canvas->base->cell_size.height) - 1;
    if (row_min_rel < 0) {
        row_min_rel = 0;
    }
    if (row_max_rel < 0) {
        row_max_rel = 0;
    }
    if (row_max_rel < row_min_rel) {
        row_max_rel = row_min_rel;
    }

    uint32_t prim_row_min = cursor_canvas_line + (uint32_t)row_min_rel;
    uint32_t prim_row_max = cursor_canvas_line + (uint32_t)row_max_rel;

    if (prim_row_min > prim_row_max) {
        return YETTY_ERR(uint32, "AABB row min > max after clamp");
    }
    if (prim_col_min > prim_col_max) {
        return YETTY_ERR(uint32, "AABB col min > max");
    }

    if (canvas->base->grid_size.cols == 0) {
        return YETTY_ERR(uint32, "grid_size.cols is 0");
    }
    if (prim_col_max >= canvas->base->grid_size.cols) {
        prim_col_max = canvas->base->grid_size.cols - 1;
    }

    for (uint32_t row = prim_row_min; row <= prim_row_max; row++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, row);
        ydraw_canvas_grid_line_ensure_cells(line, prim_col_max + 1);
        canvas_dirty_line(canvas, row);

        uint16_t lines_ahead = (uint16_t)(primitive_grid_line - row);

        for (uint32_t col = prim_col_min; col <= prim_col_max; col++) {
            struct ydraw_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_index};
            ydraw_canvas_prim_ref_array_push(&line->cells[col].refs, ref);
        }
    }

    ydebug("add_primitive_internal: aabb_y=[%.1f,%.1f] cell_height=%.1f "
           "cursor_row=%u",
           aabb.min.y, aabb.max.y, canvas->base->cell_size.height, canvas->cursor_row);
    ydebug("add_primitive_internal: prim_min_row=%u prim_max_row=%u lines.count=%u", prim_row_min,
           prim_row_max, canvas->lines.count);

    // Track complex prims for resource set collection
    if (yetty_ydraw_core_is_complex_type(prim_type)) {
        /* Create factory instance for complex prim */
        struct yetty_ydraw_core_complex_prim_instance_ptr_result inst_res =
            yetty_ydraw_core_complex_prim_factory_create_instance(
                canvas->base->complex_prim_factory, fw->data, word_count * sizeof(uint32_t),
                primitive_rolling_row);
        if (YETTY_IS_ERR(inst_res)) {
            return YETTY_ERR(uint32, inst_res.error.msg);
        }

        /* Ensure capacity for instance pointer array */
        if (base_line->complex_prim_count >= base_line->complex_prim_capacity) {
            uint32_t new_cap =
                base_line->complex_prim_capacity == 0 ? 4 : base_line->complex_prim_capacity * 2;
            base_line->complex_prims =
                realloc(base_line->complex_prims,
                        new_cap * sizeof(struct yetty_ydraw_core_complex_prim_instance *));
            if (!base_line->complex_prims) {
                yetty_ydraw_core_complex_prim_instance_destroy(inst_res.value);
                return YETTY_ERR(uint32, "realloc complex_prims failed");
            }
            base_line->complex_prim_capacity = new_cap;
        }

        base_line->complex_prims[base_line->complex_prim_count++] = inst_res.value;

        ydebug("add_primitive_internal: added complex prim type=0x%08x to line %u", prim_type,
               primitive_grid_line);
    }

    canvas->base->dirty = true;
    return YETTY_OK(uint32, primitive_grid_line);
}

//=============================================================================
// Buffer management (public API)
//=============================================================================

/* Expand a TEXT_SPAN view into per-glyph SDF primitives at the canvas's
 * current cursor. Returns the highest grid row touched (0 if no glyphs
 * placed). `font_handle` is the cache handle the resulting glyphs encode
 * into the shader's per-glyph slot dispatcher. */
static struct uint32_result expand_text_span_to_glyphs(
    struct yetty_ydraw_scrolling_canvas *canvas, const struct yetty_ydraw_core_text_span_prim_view *ts,
    struct yetty_ydraw_font *font, yetty_yfont_cache_handle font_handle)
{
    static uint32_t glyph_z_order = 0;
    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0) ? ts->font_size / base_size : 1.0f;
    float cursor_x = ts->x;
    uint32_t glyph_max_row = 0;

    const uint8_t *ptr = (const uint8_t *)ts->text;
    const uint8_t *end = ptr + ts->text_len;

    while (ptr < end) {
        /* UTF-8 decode */
        uint32_t cp = 0;
        if ((*ptr & 0x80) == 0) {
            cp = *ptr++;
        } else if ((*ptr & 0xE0) == 0xC0) {
            cp = (*ptr++ & 0x1F) << 6;
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F);
            }
        } else if ((*ptr & 0xF0) == 0xE0) {
            cp = (*ptr++ & 0x0F) << 12;
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F) << 6;
            }
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F);
            }
        } else if ((*ptr & 0xF8) == 0xF0) {
            cp = (*ptr++ & 0x07) << 18;
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F) << 12;
            }
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F) << 6;
            }
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F);
            }
        } else {
            ptr++;
            continue;
        }

        struct uint32_result gi_res = font->ops->get_glyph_index(font, cp);
        if (YETTY_IS_ERR(gi_res)) {
            cursor_x += ts->font_size * 0.5f;
            continue;
        }
        uint32_t glyph_index = gi_res.value;

        struct yetty_yrender_gpu_resource_set_result rs_res = font->ops->get_gpu_resource_set(font);
        if (YETTY_IS_ERR(rs_res)) {
            continue;
        }
        const struct yetty_ydraw_core_gpu_resource_set *rs = rs_res.value;
        if (rs->buffer_count == 0 || !rs->buffers[0].data) {
            continue;
        }

        /* Per-glyph metadata: 6 floats [size_x, size_y, bearing_x, bearing_y,
     * advance, _pad]. */
        const float *meta = (const float *)rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(rs->buffers[0].size / (6 * sizeof(float)));
        if (glyph_index >= meta_count) {
            cursor_x += ts->font_size * 0.5f;
            continue;
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

        /* Glyph SDF prim (7 words): type, z_order, x, y, font_size, packed, color
         *
         * `packed` carries (glyph_index in the low 16 bits, slot+1 in the
         * high 16 bits). `slot` is the cache handle — the same order the
         * binder attaches cache slots as resource-set children, so the
         * shader's font dispatcher uses it directly. 0 in the high bits
         * means "use slot 0" (default font); +1 lets producers encode
         * "no font" as 0 in pre-existing prims. */
        uint32_t slot = (font_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) ? font_handle : 0u;
        float glyph_data[YDRAW_GLYPH_WORDS];
        uint32_t tmp;
        tmp = YETTY_YSDF_GLYPH;
        memcpy(&glyph_data[0], &tmp, sizeof(float));
        tmp = glyph_z_order++;
        memcpy(&glyph_data[1], &tmp, sizeof(float));
        glyph_data[2] = gx;
        glyph_data[3] = gy;
        glyph_data[4] = ts->font_size;
        uint32_t packed_gf = (glyph_index & 0xFFFF) | (((uint32_t)(slot + 1) & 0xFFFF) << 16);
        memcpy(&glyph_data[5], &packed_gf, sizeof(float));
        memcpy(&glyph_data[6], &ts->color, sizeof(float));

        /* cursor_row is a screen-row offset from the viewport top
     * (rolling_row_0); convert to absolute canvas-line space. */
        uint32_t cursor_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
        float abs_y = gy + (float)cursor_canvas_line * canvas->base->cell_size.height;
        float abs_y_max = abs_y + gh;
        /* Half-open: a glyph whose bottom edge sits exactly on row K's top
         * paints zero pixels of row K. ceil-minus-one matches the same
         * convention used in add_primitive_internal. */
        float gymax_rows = abs_y_max / canvas->base->cell_size.height;
        uint32_t glyph_row_max = (gymax_rows > 0.0f)
                                     ? ((uint32_t)ceilf(gymax_rows) - 1u)
                                     : 0u;

        canvas_ensure_lines(canvas, glyph_row_max + 1);

        uint32_t rolling_row = cursor_canvas_line;

        struct ydraw_canvas_grid_line *base_line =
            ydraw_canvas_line_buffer_get(&canvas->lines, glyph_row_max);
        if (!base_line) {
            cursor_x += advance * scale;
            continue;
        }
        canvas_dirty_line(canvas, glyph_row_max);

        uint32_t prim_idx =
            ydraw_canvas_grid_line_push_prim(base_line, rolling_row, glyph_data, YDRAW_GLYPH_WORDS);
        if (prim_idx == UINT32_MAX) {
            cursor_x += advance * scale;
            continue;
        }

        uint32_t col_min =
            (canvas->base->cell_size.width > 0) ? (uint32_t)(gx / canvas->base->cell_size.width) : 0;
        uint32_t col_max =
            (canvas->base->cell_size.width > 0) ? (uint32_t)((gx + gw) / canvas->base->cell_size.width) : 0;
        uint32_t row_min = (uint32_t)(abs_y / canvas->base->cell_size.height);

        if (col_max >= canvas->base->grid_size.cols && canvas->base->grid_size.cols > 0) {
            col_max = canvas->base->grid_size.cols - 1;
        }

        for (uint32_t row = row_min; row <= glyph_row_max; row++) {
            struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, row);
            ydraw_canvas_grid_line_ensure_cells(line, col_max + 1);
            canvas_dirty_line(canvas, row);
            uint16_t lines_ahead = (uint16_t)(glyph_row_max - row);
            for (uint32_t col = col_min; col <= col_max; col++) {
                struct ydraw_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_idx};
                ydraw_canvas_prim_ref_array_push(&line->cells[col].refs, ref);
            }
        }

        if (glyph_row_max > glyph_max_row) {
            glyph_max_row = glyph_row_max;
        }

        /* Per-glyph displacement: font advance + PDF text-state Tc, plus
         * Tw for ASCII space. ts->char_spacing/word_spacing are already
         * in display pixels (ypdf does the unit conversion); add them
         * straight to the cursor. The values are 0 for any producer that
         * doesn't fill them in (default font in YAML, etc.), so this is
         * a no-op for non-PDF text. */
        cursor_x += advance * scale + ts->char_spacing;
        if (cp == 0x20) {
            cursor_x += ts->word_spacing;
        }
    }

    return YETTY_OK(uint32, glyph_max_row);
}

/* Attach a cache handle to the grid line at `glyph_max_row`.
 *
 * If the handle is already on some other line, MIGRATE the entry to the
 * target line — no refcount change, the line just moves which row "owns"
 * the binder attachment. If it's not on any line yet, push a fresh entry
 * and bump the cache refcount (the line now owns one ref, released at
 * grid_line_free).
 *
 * Skip when the handle is invalid, identifies the canvas default font (we
 * don't track the default per-line — slot 0 is always attached), or when
 * glyph_max_row is 0. */
static void attach_handle_to_line(struct yetty_ydraw_scrolling_canvas *canvas,
                                  yetty_yfont_cache_handle handle, uint32_t glyph_max_row)
{
    if (handle == YETTY_YFONT_CACHE_HANDLE_INVALID || handle == canvas->base->default_handle ||
        glyph_max_row == 0) {
        return;
    }
    struct ydraw_canvas_grid_line *target = ydraw_canvas_line_buffer_get(&canvas->lines, glyph_max_row);
    if (!target) {
        return;
    }

    /* Look for an existing attachment to migrate (still O(L*F) per call,
     * but called once per unique font per buffer rather than per text-span). */
    for (uint32_t li = 0; li < canvas->lines.count; li++) {
        struct ydraw_canvas_grid_line *l = &canvas->lines.lines[li];
        for (uint32_t fi = 0; fi < l->font_count; fi++) {
            if (l->fonts[fi].handle == handle) {
                if (li == glyph_max_row) {
                    return; /* already in the right place */
                }
                if (target->font_count >= target->font_capacity) {
                    uint32_t new_cap =
                        target->font_capacity == 0 ? 4 : target->font_capacity * 2;
                    target->fonts = realloc(
                        target->fonts, new_cap * sizeof(struct ydraw_canvas_font_entry));
                    target->font_capacity = new_cap;
                }
                /* Move (preserves the single line-held cache ref). */
                target->fonts[target->font_count++] = l->fonts[fi];
                l->fonts[fi] = l->fonts[--l->font_count];
                return;
            }
        }
    }

    /* New attachment — line takes a fresh cache ref. */
    if (target->font_count >= target->font_capacity) {
        uint32_t new_cap = target->font_capacity == 0 ? 4 : target->font_capacity * 2;
        target->fonts =
            realloc(target->fonts, new_cap * sizeof(struct ydraw_canvas_font_entry));
        target->font_capacity = new_cap;
    }
    yetty_yfont_cache_retain(canvas->base->font_cache, handle);
    target->fonts[target->font_count++].handle = handle;
}

/* ===========================================================================
 * Scrollbuffer eviction
 *
 * When a line scrolls below `rolling_row_0` it's no longer visible. We
 * encode it to canvas->scrollbuffer (compact form, no individual
 * allocations), record the byte offset in sb_offsets[i], then free the
 * line's expanded prims/arena/cells/fonts so the per-line malloc
 * footprint goes away. The grid_line struct itself stays in
 * canvas->lines.lines[i] but ends up empty after grid_line_free; we
 * preserve the slot so absolute canvas-line indices keep their
 * meaning. Deserialise-on-scrollback isn't here yet.
 * ========================================================================= */

/* About to mutate line `idx`. If it was previously evicted to the
 * scrollbuffer, its expanded form is empty and the only copy of the
 * content lives at sb_offsets[idx]. We MUST:
 *   1. restore that content into the line's expanded form first, so
 *      the new prim can stack on top of it,
 *   2. clear sb_offsets[idx] so the next eviction re-encodes the merged
 *      content (the orphaned old record stays in the scrollbuffer but
 *      no offset points to it — bounded waste).
 *
 * Without step 1, clearing sb_offsets[idx] would silently abandon the
 * old content on the next evict pass (re-encode of merely the new prim,
 * the old bytes orphaned but unreachable). PDFs, multi-buffer browsers,
 * and any "ydraw #2 lands on lines previously occupied by ydraw #1"
 * scenario would lose history.
 *
 * Restore failure is logged and we proceed — the new prim still saves,
 * the old content is gone. Mirror of the best-effort policy used in
 * canvas_restore_range. */
static void canvas_dirty_line(struct yetty_ydraw_scrolling_canvas *canvas, uint32_t idx)
{
    if (idx >= canvas->sb_offsets_count || canvas->sb_offsets[idx] == SB_OFFSET_UNSET) {
        return;
    }
    struct yetty_ycore_void_result r = canvas_restore_line(canvas, idx);
    if (YETTY_IS_ERR(r)) {
        yerror("canvas_dirty_line: restore line %u failed: %s — old content will be dropped",
               idx, r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
    canvas->sb_offsets[idx] = SB_OFFSET_UNSET;
}

static struct yetty_ycore_void_result sb_offsets_ensure(struct yetty_ydraw_scrolling_canvas *canvas,
                                                        uint32_t min_count)
{
    if (min_count <= canvas->sb_offsets_count) {
        return YETTY_OK_VOID();
    }
    if (min_count > canvas->sb_offsets_capacity) {
        uint32_t new_cap = canvas->sb_offsets_capacity ? canvas->sb_offsets_capacity : 64u;
        while (new_cap < min_count) {
            new_cap *= 2;
        }
        uint32_t *grown = realloc(canvas->sb_offsets, new_cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "sb_offsets: realloc failed");
        }
        canvas->sb_offsets = grown;
        canvas->sb_offsets_capacity = new_cap;
    }
    for (uint32_t i = canvas->sb_offsets_count; i < min_count; i++) {
        canvas->sb_offsets[i] = SB_OFFSET_UNSET;
    }
    canvas->sb_offsets_count = min_count;
    return YETTY_OK_VOID();
}

/* Serialise one line at index `idx` into the scrollbuffer and free its
 * expanded form. No-op if the line is already serialised or doesn't
 * exist. Returns OK even if the line was empty; an empty line still
 * gets a 12-byte header record so sb_offsets[idx] is always meaningful
 * after this call. */
static struct yetty_ycore_void_result canvas_evict_line(struct yetty_ydraw_scrolling_canvas *canvas,
                                                        uint32_t idx)
{
    if (idx >= canvas->lines.count) {
        return YETTY_OK_VOID();
    }
    /* Already evicted? skip. */
    struct yetty_ycore_void_result er = sb_offsets_ensure(canvas, idx + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "evict: ensure offsets");
    if (canvas->sb_offsets[idx] != SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }

    struct ydraw_canvas_grid_line *line = &canvas->lines.lines[idx];

    /* Build the cell view: walk cells in ascending col order, emit
     * one entry per cell that has at least one ref. The on-stack
     * buffer is sized by line->cell_count which is bounded by
     * grid_size.cols + any oversize a wide line grew to. */
    struct yetty_ydraw_scrollbuffer_cell *cells = NULL;
    uint32_t n_cells = 0;
    if (line->cell_count > 0) {
        cells = malloc(line->cell_count * sizeof(*cells));
        if (!cells) {
            return YETTY_ERR(yetty_ycore_void, "evict: cell view alloc failed");
        }
        for (uint32_t c = 0; c < line->cell_count; c++) {
            if (line->cells[c].refs.count == 0) {
                continue;
            }
            cells[n_cells].col = (uint16_t)c;
            cells[n_cells].ref_count = (uint16_t)line->cells[c].refs.count;
            /* The codec's ref view shape matches our internal
             * prim_ref byte-for-byte, so we just hand over the
             * dynamic array's data pointer. */
            cells[n_cells].refs =
                (const struct yetty_ydraw_scrollbuffer_ref *)line->cells[c].refs.data;
            n_cells++;
        }
    }

    /* Build the prim view: per prim, point payload at the arena slice
     * (arena_offset .. arena_offset + word_count). */
    struct yetty_ydraw_scrollbuffer_prim *prims = NULL;
    uint32_t n_prims = line->prims.count;
    if (n_prims > 0) {
        prims = malloc(n_prims * sizeof(*prims));
        if (!prims) {
            free(cells);
            return YETTY_ERR(yetty_ycore_void, "evict: prim view alloc failed");
        }
        for (uint32_t p = 0; p < n_prims; p++) {
            struct ydraw_canvas_prim_data *pd = &line->prims.data[p];
            prims[p].rolling_row = pd->rolling_row;
            prims[p].word_count = pd->word_count;
            prims[p].payload = line->arena + pd->arena_offset;
        }
    }

    struct yetty_ydraw_scrollbuffer_offset_result enc =
        yetty_ydraw_scrollbuffer_encode_line(&canvas->scrollbuffer, idx,
                                              canvas->base->grid_size.cols, cells, n_cells, prims,
                                              n_prims);
    free(cells);
    free(prims);
    if (YETTY_IS_ERR(enc)) {
        return YETTY_ERR(yetty_ycore_void, "evict: encode failed", enc);
    }
    canvas->sb_offsets[idx] = (uint32_t)enc.value;

    /* Free the expanded form. The line struct itself stays in
     * canvas->lines.lines[idx]; grid_line_free zeroes its internal
     * pointers/counts so it's effectively a placeholder afterwards.
     *
     * Font cache refs the line held are dropped here too — that's
     * intentional: if the only line referencing a given font is being
     * evicted, the cache slot would normally die. With the lazy-
     * resolve commit (69c4dd5), TEXT_SPANs hitting the same font
     * later re-resolve through the cache; the on-disk CDB still
     * caches the MSDF generation, so the cost is one cache miss +
     * atlas load on first reuse.
     *
     * Complex prims (yimage / yplot / yvideo) are GPU-backed
     * instances the scrollbuffer codec can't round-trip — we'd lose
     * them on scroll-back. Detach them before grid_line_free, then
     * re-attach after grid_line_init so they keep living on the
     * (otherwise empty) line and re-render the moment the user
     * scrolls the line back into the visible window.
     *
     * Font attachments get the same treatment for a different reason:
     * each line holds a cache ref per attached font, and the GLYPH
     * payloads we just encoded carry the font's slot index. If we let
     * grid_line_free release these refs, the cache may evict the font
     * — and the ydraw layer's WGSL dispatcher (sized off
     * canvas_font_count) keeps referencing that slot's namespace at
     * render time, causing the shader to fail to compile with
     * "struct member <ns>_base_size not found". Detach + restore keeps
     * the cache ref alive across the line's empty-form phase, so the
     * font slot remains addressable until the line is re-restored. */
    struct yetty_ydraw_core_complex_prim_instance **saved_cp = line->complex_prims;
    uint32_t saved_cp_count = line->complex_prim_count;
    uint32_t saved_cp_cap = line->complex_prim_capacity;
    line->complex_prims = NULL;
    line->complex_prim_count = 0;
    line->complex_prim_capacity = 0;

    struct ydraw_canvas_font_entry *saved_fonts = line->fonts;
    uint32_t saved_font_count = line->font_count;
    uint32_t saved_font_capacity = line->font_capacity;
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;

    struct yetty_ycore_void_result fr =
        ydraw_canvas_grid_line_free(line, canvas->base->font_cache);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "evict: grid_line_free");
    /* Re-init so future writes (e.g. an explicit out-of-order place)
     * don't see dangling pointers. */
    ydraw_canvas_grid_line_init(line);

    line->complex_prims = saved_cp;
    line->complex_prim_count = saved_cp_count;
    line->complex_prim_capacity = saved_cp_cap;
    line->fonts = saved_fonts;
    line->font_count = saved_font_count;
    line->font_capacity = saved_font_capacity;
    return YETTY_OK_VOID();
}

/* Walk every line strictly below rolling_row_0 and evict any that
 * still hold expanded content. Called at the end of add_buffer once
 * the new content has been placed and the viewport has scrolled.
 *
 * A line that was previously evicted but restored for a scrollback
 * render (sb_offsets[i] set, but grid_line currently populated) is
 * NOT re-encoded — its bytes haven't changed — but its expanded
 * form is freed again. */
static void canvas_evict_scrollback(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (canvas->rolling_row_0 == 0) {
        return;
    }
    uint32_t end = canvas->rolling_row_0;
    if (end > canvas->lines.count) {
        end = canvas->lines.count;
    }
    for (uint32_t i = 0; i < end; i++) {
        if (i < canvas->sb_offsets_count && canvas->sb_offsets[i] != SB_OFFSET_UNSET) {
            /* Already serialised. If it was restored, free the
             * expanded form again. Complex prims AND font attachments
             * must survive the re-free for the same reason as in
             * canvas_evict_line — detach before grid_line_free,
             * re-attach after grid_line_init. */
            struct ydraw_canvas_grid_line *line = &canvas->lines.lines[i];
            if (line->prims.count > 0 || line->cell_count > 0 || line->font_count > 0) {
                struct yetty_ydraw_core_complex_prim_instance **saved_cp = line->complex_prims;
                uint32_t saved_cp_count = line->complex_prim_count;
                uint32_t saved_cp_cap = line->complex_prim_capacity;
                line->complex_prims = NULL;
                line->complex_prim_count = 0;
                line->complex_prim_capacity = 0;

                struct ydraw_canvas_font_entry *saved_fonts = line->fonts;
                uint32_t saved_font_count = line->font_count;
                uint32_t saved_font_capacity = line->font_capacity;
                line->fonts = NULL;
                line->font_count = 0;
                line->font_capacity = 0;

                struct yetty_ycore_void_result fr =
                    ydraw_canvas_grid_line_free(line, canvas->base->font_cache);
                if (YETTY_IS_ERR(fr)) {
                    yerror("canvas_evict_scrollback: re-free line %u failed: %s",
                           i, fr.error.msg);
                    yetty_ycore_error_destroy(fr.error);
                }
                ydraw_canvas_grid_line_init(line);

                line->complex_prims = saved_cp;
                line->complex_prim_count = saved_cp_count;
                line->complex_prim_capacity = saved_cp_cap;
                line->fonts = saved_fonts;
                line->font_count = saved_font_count;
                line->font_capacity = saved_font_capacity;
            }
            continue;
        }
        struct yetty_ycore_void_result r = canvas_evict_line(canvas, i);
        if (YETTY_IS_ERR(r)) {
            yerror("canvas_evict_scrollback: evict line %u failed: %s", i, r.error.msg);
            yetty_ycore_error_destroy(r.error);
            /* Keep going — best-effort batch eviction; later lines may
             * still succeed. */
        }
    }
}

/* ===========================================================================
 * Scrollbuffer restore (decode evicted lines back into canvas->lines)
 *
 * Called by rebuild_grid for every visible-window row whose grid_line
 * is empty but sb_offsets[i] is set. After restore, the grid_line
 * has its prims/arena/cells populated and rendering proceeds as if
 * the line had never been evicted. canvas_evict_scrollback frees
 * these restored lines on the next add_buffer.
 * ========================================================================= */

/* Word-count lookup for the scrollbuffer decoder. The codec stores
 * non-default prims as <type, payload-bytes> with NO explicit length —
 * the decoder must know how many words each type occupies.
 *
 * Two source ranges live in canvas->lines and therefore in the
 * scrollbuffer:
 *   - YETTY_YSDF_GLYPH (= 200), the per-character flyweight expanded
 *     from TEXT_SPAN prims. Standalone constant — not in the SDF
 *     types.gen enum range.
 *   - Real SDF prims [0x10000000, 0x1FFFFFFF] from generators that
 *     emit BOX/CIRCLE/SEGMENT/ELLIPSE/… (SVG, PDF rect/line, browser).
 *
 * Returning 0 for an unrecognised type lets the decoder fail cleanly
 * rather than read garbage. */
static uint32_t canvas_sb_word_count_fn(uint32_t type_word, void *ctx)
{
    (void)ctx;
    if (type_word == YETTY_YSDF_GLYPH) {
        return YDRAW_GLYPH_WORDS;
    }
    if (type_word >= 0x10000000u && type_word <= 0x1FFFFFFFu) {
        uint32_t wc = yetty_ysdf_word_count((enum yetty_ysdf_type)type_word);
        if (wc > 0) return wc;
    }
    return 0;
}

struct sb_restore_ctx {
    struct ydraw_canvas_grid_line *line;
};

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_header(void *ctx,
                                                           uint32_t line_rolling_row,
                                                           uint32_t prim_count)
{
    /* No-op: the line was freed before this restore was triggered,
     * so push_prim handles growing the arena/prims as needed. The
     * header values are only used for sanity checks here. */
    (void)ctx;
    (void)line_rolling_row;
    (void)prim_count;
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_cell(
    void *ctx, uint32_t col, const struct yetty_ydraw_scrollbuffer_ref *refs, uint32_t ref_count)
{
    struct sb_restore_ctx *r = ctx;
    struct yetty_ycore_void_result er = ydraw_canvas_grid_line_ensure_cells(r->line, col + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "restore: ensure_cells");
    for (uint32_t i = 0; i < ref_count; i++) {
        struct ydraw_canvas_prim_ref pr = {
            .lines_ahead = refs[i].lines_ahead,
            .prim_index  = refs[i].prim_idx,
        };
        ydraw_canvas_prim_ref_array_push(&r->line->cells[col].refs, pr);
    }
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_prim(void *ctx, uint32_t rolling_row,
                                                         const uint32_t *payload,
                                                         uint32_t word_count)
{
    struct sb_restore_ctx *r = ctx;
    uint32_t idx = ydraw_canvas_grid_line_push_prim(r->line, rolling_row, (const float *)payload, word_count);
    if (idx == UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "restore: grid_line_push_prim failed");
    }
    return YETTY_OK_VOID();
}

/* Decode line `idx` from the scrollbuffer back into
 * canvas->lines.lines[idx]. Idempotent: if the line is not in
 * scrollbuffer (sb_offsets[idx] == UNSET) or already has expanded
 * content, it's a no-op. */
static struct yetty_ycore_void_result canvas_restore_line(struct yetty_ydraw_scrolling_canvas *canvas,
                                                          uint32_t idx)
{
    if (idx >= canvas->sb_offsets_count || canvas->sb_offsets[idx] == SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }
    if (idx >= canvas->lines.count) {
        return YETTY_OK_VOID();
    }
    struct ydraw_canvas_grid_line *line = &canvas->lines.lines[idx];
    if (line->prims.count > 0 || line->cell_count > 0) {
        /* Already expanded — could be a previously-restored line still
         * holding content from the previous render, or content that
         * was placed on top of a serialised line (shouldn't happen
         * for ycat workloads, but harmless to handle). */
        return YETTY_OK_VOID();
    }

    struct sb_restore_ctx rctx = {.line = line};
    struct yetty_ydraw_scrollbuffer_decode_sinks sinks = {
        .ctx = &rctx,
        .on_header = sb_restore_on_header,
        .on_cell   = sb_restore_on_cell,
        .on_prim   = sb_restore_on_prim,
    };
    struct yetty_ydraw_scrollbuffer_offset_result dec = yetty_ydraw_scrollbuffer_decode_line(
        &canvas->scrollbuffer, canvas->sb_offsets[idx], canvas_sb_word_count_fn, NULL, &sinks);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dec, "canvas_restore_line: decode failed");
    return YETTY_OK_VOID();
}

/* Restore every evicted line in [first, last] (inclusive). Used by
 * rebuild_grid to ensure the visible-window rows are populated. */
static void canvas_restore_range(struct yetty_ydraw_scrolling_canvas *canvas, uint32_t first,
                                 uint32_t last)
{
    for (uint32_t i = first; i <= last; i++) {
        struct yetty_ycore_void_result r = canvas_restore_line(canvas, i);
        if (YETTY_IS_ERR(r)) {
            yerror("canvas_restore_range: restore line %u failed: %s", i, r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }
}

//=============================================================================
// Scrolling
//=============================================================================

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_scroll_lines(struct yetty_ydraw_scrolling_canvas *canvas,
                                                                uint16_t num_lines)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (num_lines == 0) {
        return YETTY_OK_VOID();
    }

    /* Non-destructive scroll: lines stay in canvas->lines as scrollback.
   * rolling_row_0 advances to the canvas-line index of the new viewport
   * top; cursor_row is a screen-row, so it shifts up by num_lines. */
    canvas->rolling_row_0 += num_lines;
    if (canvas->cursor_row >= num_lines) {
        canvas->cursor_row -= num_lines;
    } else {
        canvas->cursor_row = 0;
    }

    ydebug("yetty_ydraw_scrolling_canvas_scroll_lines: num_lines=%u lines.count=%u "
           "rolling_row_0=%u cursor_row=%u",
           num_lines, canvas->lines.count, canvas->rolling_row_0, canvas->cursor_row);

    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_scroll_callback(
    struct yetty_ydraw_scrolling_canvas *canvas,
    yetty_ydraw_canvas_scroll_callback callback, void *user_data)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->scroll_callback = callback;
    canvas->scroll_callback_user_data = user_data;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_set_cursor_callback(
    struct yetty_ydraw_scrolling_canvas *canvas,
    yetty_ydraw_canvas_cursor_callback callback, void *user_data)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->cursor_set_callback = callback;
    canvas->cursor_set_callback_user_data = user_data;
    return YETTY_OK_VOID();
}

//=============================================================================
// Packed GPU format
//=============================================================================

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_rebuild_grid(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->base->dirty && canvas->base->grid_staging_count > 0) {
        return YETTY_OK_VOID();
    }

    /* If the visible window dips into scrollback (set_view_top), the
     * grid_lines for that range may have been evicted to the
     * scrollbuffer and emptied. Decode them back into expanded form
     * before the prefix-sum/cell-walk below runs. Lines that aren't
     * evicted, or are already restored, are no-ops. */
    {
        uint32_t window_top = canvas_effective_view_top(canvas);
        uint32_t window_last = window_top + canvas->base->grid_size.rows;
        if (window_last > canvas->lines.count) {
            window_last = canvas->lines.count;
        }
        if (window_last > window_top) {
            canvas_restore_range(canvas, window_top, window_last - 1u);
        }
    }

    /* Prefix-sum of prim counts across ALL canvas lines. The GPU prim buffer
   * holds every prim (so off-screen scrollback prims can still be referenced
   * by visible cells via lines_ahead), and ref->prim_index is local to the
   * line that was appended to. */
    uint32_t total_prims = 0;
    uint32_t *line_base_prim_idx = NULL;
    if (canvas->lines.count > 0) {
        line_base_prim_idx = malloc(canvas->lines.count * sizeof(uint32_t));
        for (uint32_t i = 0; i < canvas->lines.count; i++) {
            line_base_prim_idx[i] = total_prims;
            struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);
            total_prims += line->prims.count;
        }
    }

    /* Build a fixed-size GPU grid for the visible viewport only. The viewport
   * spans canvas-line indices [view_top .. view_top + grid_rows). In live
   * mode that's rolling_row_0; in scrollback view it's the override. */
    uint32_t grid_w = canvas->base->grid_size.cols;
    uint32_t grid_h = canvas->base->grid_size.rows;
    uint32_t window_top = canvas_effective_view_top(canvas);

    /* Cells beyond grid_size.cols can exist on lines that grew past the
   * default width; widen grid_w to accommodate the visible window's
   * widest line. Off-screen lines don't influence grid_w because the
   * shader never indexes those columns. Capped at YDRAW_GRID_COLS_MAX
   * so a buggy/malicious producer can't blow up grid_staging — anything
   * beyond is clipped on the right edge, like an unwrapped text line. */
    const uint32_t YDRAW_GRID_COLS_MAX = 4096u;
    for (uint32_t gpu_y = 0; gpu_y < grid_h; gpu_y++) {
        uint32_t canvas_y = window_top + gpu_y;
        if (canvas_y >= canvas->lines.count) {
            break;
        }
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, canvas_y);
        if (line->cell_count > grid_w) {
            grid_w = line->cell_count;
        }
    }
    if (grid_w > YDRAW_GRID_COLS_MAX) {
        grid_w = YDRAW_GRID_COLS_MAX;
    }

    if (grid_w == 0 || grid_h == 0) {
        canvas->base->grid_staging_count = 0;
        canvas->base->dirty = false;
        free(line_base_prim_idx);
        return YETTY_OK_VOID();
    }

    uint32_t num_cells = grid_w * grid_h;

    ydraw_canvas_ensure_grid_staging(canvas->base, num_cells * 4);
    canvas->base->grid_staging_count = num_cells;

    uint32_t cells_with_prims = 0;
    uint32_t total_refs_in_window = 0;
    uint32_t max_refs_in_one_cell = 0;
    uint32_t lines_with_prims_in_window = 0;
    /* Per-row detail kept on the stack — grid_h is small (≤ ~256). */
    uint32_t row_ref_counts[256] = {0};
    uint32_t row_line_prims[256] = {0};

    for (uint32_t gpu_y = 0; gpu_y < grid_h; gpu_y++) {
        uint32_t canvas_y = window_top + gpu_y;
        bool has_line = canvas_y < canvas->lines.count;
        struct ydraw_canvas_grid_line *line =
            has_line ? ydraw_canvas_line_buffer_get(&canvas->lines, canvas_y) : NULL;
        uint32_t line_cell_count = line ? line->cell_count : 0;
        uint32_t row_refs = 0;
        if (line && gpu_y < 256) {
            row_line_prims[gpu_y] = line->prims.count;
        }

        for (uint32_t x = 0; x < grid_w; x++) {
            uint32_t cell_idx = gpu_y * grid_w + x;

            ydraw_canvas_ensure_grid_staging(canvas->base, canvas->base->grid_staging_count + 2);
            canvas->base->grid_staging[cell_idx] = canvas->base->grid_staging_count;

            uint32_t count_pos = canvas->base->grid_staging_count++;
            ydraw_canvas_ensure_grid_staging(canvas->base, canvas->base->grid_staging_count + 1);
            canvas->base->grid_staging[count_pos] = 0;
            uint32_t count = 0;

            if (has_line && x < line_cell_count) {
                struct ydraw_canvas_grid_cell *cell = &line->cells[x];
                for (uint32_t ri = 0; ri < cell->refs.count; ri++) {
                    struct ydraw_canvas_prim_ref *ref = &cell->refs.data[ri];
                    /* lines_ahead is in canvas-line space, so bl is the canvas-line
           * of the prim's anchor — not a GPU row index. */
                    uint32_t bl = canvas_y + ref->lines_ahead;
                    if (bl < canvas->lines.count && line_base_prim_idx) {
                        ydraw_canvas_ensure_grid_staging(canvas->base, canvas->base->grid_staging_count + 1);
                        canvas->base->grid_staging[canvas->base->grid_staging_count++] =
                            line_base_prim_idx[bl] + ref->prim_index;
                        count++;
                    }
                }
            }

            canvas->base->grid_staging[count_pos] = count;
            if (count > 0) {
                cells_with_prims++;
            }
            if (count > max_refs_in_one_cell) {
                max_refs_in_one_cell = count;
            }
            row_refs += count;
        }

        total_refs_in_window += row_refs;
        if (row_refs > 0) {
            lines_with_prims_in_window++;
        }
        if (gpu_y < 256) {
            row_ref_counts[gpu_y] = row_refs;
        }
    }

    ydebug("rebuild_grid: window=[%u..%u] grid=%ux%u cells_with_prims=%u/%u "
           "lines_with_prims=%u total_refs=%u max_refs/cell=%u",
           window_top, window_top + grid_h - 1, grid_w, grid_h, cells_with_prims, grid_w * grid_h,
           lines_with_prims_in_window, total_refs_in_window, max_refs_in_one_cell);
    /* Per-row breakdown so we can see which canvas-lines have prims and
   * which rows of the screen end up blank. */
    for (uint32_t gpu_y = 0; gpu_y < grid_h && gpu_y < 256; gpu_y++) {
        if (row_ref_counts[gpu_y] > 0 || row_line_prims[gpu_y] > 0) {
            ydebug("rebuild_grid:   gpu_y=%2u canvas_y=%u line.prims=%u refs=%u", gpu_y,
                   window_top + gpu_y, row_line_prims[gpu_y], row_ref_counts[gpu_y]);
        }
    }

    free(line_base_prim_idx);
    canvas->base->dirty = false;
    return YETTY_OK_VOID();
}

//=============================================================================
// Primitive staging
//=============================================================================

static struct yetty_ydraw_prim_staging_result yetty_ydraw_scrolling_canvas_build_prim_staging(
    struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_prim_staging,
                         "yetty_ydraw_scrolling_canvas_build_prim_staging: NULL canvas");
    }

    // Count primitives and total words (+1 per prim for rolling_row)
    uint32_t prim_count = 0;
    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);
        for (uint32_t p = 0; p < line->prims.count; p++) {
            prim_count++;
            total_words += line->prims.data[p].word_count + 1; // +1 for rolling_row
        }
    }

    if (prim_count == 0) {
        canvas->base->prim_staging_count = 0;
        struct yetty_ydraw_prim_staging empty = {.data = NULL, .word_count = 0};
        return YETTY_OK(yetty_ydraw_prim_staging, empty);
    }

    // Layout: [prim0_offset, prim1_offset, ...][rolling_row0,
    // prim0_data...][rolling_row1, prim1_data...]
    uint32_t total_size = prim_count + total_words;
    struct yetty_ycore_void_result eps = ydraw_canvas_ensure_prim_staging(canvas->base, total_size);
    YETTY_RETURN_IF_ERR(yetty_ydraw_prim_staging, eps,
                        "yetty_ydraw_scrolling_canvas_build_prim_staging: ensure_prim_staging failed");

    uint32_t data_offset = 0;
    uint32_t prim_idx = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);

        for (uint32_t p = 0; p < line->prims.count; p++) {
            struct ydraw_canvas_prim_data *prim = &line->prims.data[p];
            canvas->base->prim_staging[prim_idx] = data_offset;

            // Prepend rolling_row at insertion (for shader y_offset calculation)
            canvas->base->prim_staging[prim_count + data_offset] = prim->rolling_row;

            // Copy primitive payload from the line's arena.
            const uint32_t *payload = line->arena + prim->arena_offset;
            memcpy(&canvas->base->prim_staging[prim_count + data_offset + 1], payload,
                   prim->word_count * sizeof(uint32_t));

            data_offset += prim->word_count + 1; // +1 for rolling_row
            prim_idx++;
        }
    }

    canvas->base->prim_staging_count = total_size;
    struct yetty_ydraw_prim_staging out = {.data = canvas->base->prim_staging, .word_count = total_size};
    return YETTY_OK(yetty_ydraw_prim_staging, out);
}

static uint32_t yetty_ydraw_scrolling_canvas_prim_gpu_size(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);
        for (uint32_t p = 0; p < line->prims.count; p++) {
            total_words += line->prims.data[p].word_count + 1; // +1 for rolling_row
        }
    }
    return total_words * sizeof(float);
}

//=============================================================================
// State management
//=============================================================================

static struct yetty_ycore_void_result yetty_ydraw_scrolling_canvas_clear(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    struct yetty_ycore_void_result res =
        ydraw_canvas_line_buffer_free(&canvas->lines, canvas->base->font_cache);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    ydraw_canvas_line_buffer_init(&canvas->lines);

    /* Reset scrollbuffer too — clear wipes scrollback. */
    yetty_ydraw_scrollbuffer_free(&canvas->scrollbuffer);
    yetty_ydraw_scrollbuffer_init(&canvas->scrollbuffer);
    free(canvas->sb_offsets);
    canvas->sb_offsets = NULL;
    canvas->sb_offsets_count = 0;
    canvas->sb_offsets_capacity = 0;

    canvas->base->grid_staging_count = 0;
    canvas->base->prim_staging_count = 0;
    canvas->cursor_col = 0;
    canvas->cursor_row = 0;
    canvas->rolling_row_0 = 0;
    canvas->view_top_override_active = false;
    canvas->view_top_override = 0;
    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

static uint32_t yetty_ydraw_scrolling_canvas_primitive_count(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);
        count += line->prims.count;
    }
    return count;
}

//=============================================================================
// Complex primitive access (for atlas rendering)
//=============================================================================

/* Visible window in canvas-line space:
 *   [rolling_row_0 .. rolling_row_0 + grid_size.rows). */
static void canvas_visible_window(const struct yetty_ydraw_scrolling_canvas *canvas, uint32_t *out_top,
                                  uint32_t *out_end)
{
    uint32_t top = canvas_effective_view_top(canvas);
    uint32_t end = top + canvas->base->grid_size.rows;
    if (end > canvas->lines.count) {
        end = canvas->lines.count;
    }
    if (top > end) {
        top = end;
    }
    *out_top = top;
    *out_end = end;
}

static uint32_t yetty_ydraw_scrolling_canvas_complex_prim_count(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t top, end;
    canvas_visible_window(canvas, &top, &end);

    uint32_t count = 0;
    for (uint32_t i = top; i < end; i++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);
        count += line->complex_prim_count;
    }
    return count;
}

static struct yetty_ydraw_core_complex_prim_instance *yetty_ydraw_scrolling_canvas_get_complex_prim(
    struct yetty_ydraw_scrolling_canvas *canvas, uint32_t index)
{
    if (!canvas) {
        return NULL;
    }

    uint32_t top, end;
    canvas_visible_window(canvas, &top, &end);

    uint32_t current = 0;
    for (uint32_t i = top; i < end; i++) {
        struct ydraw_canvas_grid_line *line = ydraw_canvas_line_buffer_get(&canvas->lines, i);
        if (index < current + line->complex_prim_count) {
            uint32_t local_idx = index - current;
            return line->complex_prims[local_idx];
        }
        current += line->complex_prim_count;
    }
    return NULL;
}


/*=============================================================================
 * Glyph iteration
 *===========================================================================*/

static void yetty_ydraw_scrolling_canvas_for_each_glyph(struct yetty_ydraw_scrolling_canvas *canvas,
                                        yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    if (!canvas || !visitor) {
        return;
    }
    float cell_h = canvas->base->cell_size.height;
    /* Glyph prim layout (see expand_text_span_to_glyphs):
     *   word[0]: u32 type            (== YETTY_YSDF_GLYPH)
     *   word[1]: u32 z_order
     *   word[2]: f32 x               (canvas-pixel)
     *   word[3]: f32 y               (RELATIVE — see below)
     *   word[4]: f32 font_size
     *   word[5]: u32 packed          (low 16 = glyph_idx, high 16 = slot+1)
     *   word[6]: u32 color
     *
     * The stored y is RELATIVE to the cursor-line at insertion time, not
     * absolute canvas y. The canvas reconstructs the absolute position
     * via `gy + pd->rolling_row * cell_h` (see the abs_y computation in
     * expand_text_span_to_glyphs). We do the same here so visitors get
     * absolute canvas pixel coordinates and can filter by viewport y
     * directly. Words carry mixed types — memcpy each one out at decode
     * time to stay endian/alignment-agnostic. */
    for (uint32_t li = 0; li < canvas->lines.count; li++) {
        const struct ydraw_canvas_grid_line *line = &canvas->lines.lines[li];
        for (uint32_t pi = 0; pi < line->prims.count; pi++) {
            const struct ydraw_canvas_prim_data *pd = &line->prims.data[pi];
            if (pd->word_count < YDRAW_GLYPH_WORDS) {
                continue;
            }
            const uint32_t *words = line->arena + pd->arena_offset;
            uint32_t type_word;
            memcpy(&type_word, &words[0], sizeof(type_word));
            if (type_word != YETTY_YSDF_GLYPH) {
                continue;
            }
            float gx, gy_rel;
            uint32_t packed;
            memcpy(&gx, &words[2], sizeof(gx));
            memcpy(&gy_rel, &words[3], sizeof(gy_rel));
            memcpy(&packed, &words[5], sizeof(packed));

            struct yetty_ydraw_glyph_view view;
            view.x = gx;
            view.y = gy_rel + (float)pd->rolling_row * cell_h;
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

static struct yetty_ycore_void_result scrolling_set_grid_size_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_size size)
{
    return yetty_ydraw_scrolling_canvas_set_grid_size(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, size);
}

/* Reset per-envelope streaming state on the variant. Called on the SM
 * `at_end` finaliser path and on errors. Idempotent.
 *
 * Font map refs are NOT released here — scrolling_env_finalize releases
 * them before scroll/evict so the cache refcount has settled by the time
 * eviction runs. Reset just frees the map storage. */
static void scrolling_env_reset(struct yetty_ydraw_scrolling_canvas *canvas)
{
    if (!canvas->env_active) {
        return;
    }
    yetty_ydraw_core_prim_iter_destroy(&canvas->env_iter);
    ydraw_canvas_buffer_attach_free(&canvas->env_attach_list);
    /* Error path: if finalize never ran, refs are still held — drop them. */
    ydraw_canvas_font_map_release_all(&canvas->env_fonts_map, canvas->base->font_cache);
    free(canvas->env_fonts_map.entries);
    canvas->env_fonts_map.entries = NULL;
    canvas->env_fonts_map.capacity = 0;
    canvas->env_active = false;
}

/* End-of-envelope finalisation: attach fonts, advance the cursor to the
 * deepest row the queue painted on, scroll the viewport only as much as
 * needed to keep that row visible, then evict scrollback.
 *
 * Contract — same as a plain text write that does NOT end with `\n`:
 * cursor lands on the last touched row, no phantom row reserved for a
 * follow-on prompt. Producers that want a fresh row below their painting
 * (ycat, ypdf, ...) emit `\n` via the PTY text path themselves, exactly
 * like `cat foo.txt` does.
 *
 * ORDERING MATTERS: font_map_release_all runs BEFORE scroll/evict.
 * The dispatcher in ydraw-layer keys its rebuild on font_count alone, so
 * eviction must observe the final refcount state — otherwise a font
 * dropped by eviction can leave a stale dispatcher entry referencing
 * a struct that's no longer in the merged shader. */
static struct yetty_ycore_void_result scrolling_env_finalize(
    struct yetty_ydraw_scrolling_canvas *canvas)
{
    /* Attach pass — one cache ref per unique font, parked on its highest row. */
    for (uint32_t i = 0; i < canvas->env_attach_list.count; i++) {
        attach_handle_to_line(canvas, canvas->env_attach_list.entries[i].handle,
                              canvas->env_attach_list.entries[i].max_row);
    }

    /* Release buffer-scoped font refs BEFORE scroll/evict. Lines that took
     * their own ref via attach_handle_to_line keep the font alive; fonts
     * only referenced by evicted lines drop together with eviction. */
    ydraw_canvas_font_map_release_all(&canvas->env_fonts_map, canvas->base->font_cache);
    free(canvas->env_fonts_map.entries);
    canvas->env_fonts_map.entries = NULL;
    canvas->env_fonts_map.capacity = 0;

    /* Target: the deepest row the queue actually painted on. Equivalent
     * to writing N rows of text without a trailing newline — cursor sits
     * on the last touched row, viewport scrolls just enough to bring it
     * into view. The producer is responsible for any follow-on `\n`. */
    uint32_t target_cursor_canvas_line = canvas->env_max_row_seen;
    uint32_t viewport_bottom = canvas->rolling_row_0 + canvas->base->grid_size.rows - 1;

    if (target_cursor_canvas_line > viewport_bottom) {
        uint32_t lines_to_scroll = target_cursor_canvas_line - viewport_bottom;
        if (canvas->scroll_callback) {
            struct yetty_ycore_void_result scroll_res = canvas->scroll_callback(
                canvas->scroll_callback_user_data, (uint16_t)lines_to_scroll);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res,
                                "process_input: scroll_callback failed");
        }
        struct yetty_ycore_void_result sr =
            yetty_ydraw_scrolling_canvas_scroll_lines(canvas, (uint16_t)lines_to_scroll);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "process_input: scroll_lines failed");
    }

    uint32_t cursor_screen_row = (target_cursor_canvas_line >= canvas->rolling_row_0)
                                     ? (target_cursor_canvas_line - canvas->rolling_row_0)
                                     : 0;
    canvas->cursor_row = (uint16_t)cursor_screen_row;
    if (canvas->cursor_set_callback) {
        canvas->cursor_set_callback(canvas->cursor_set_callback_user_data, canvas->cursor_row);
    }

    canvas_evict_scrollback(canvas);
    canvas->base->dirty = true;
    return YETTY_OK_VOID();
}

/* Dispatch one prim from the streaming flyweight view onto the canvas.
 * Mirrors the per-prim block of add_buffer; no need to bail on per-prim
 * errors — they're logged and the next prim still attempts to land. */
static void scrolling_dispatch_one(
    struct yetty_ydraw_scrolling_canvas *canvas,
    const struct yetty_ydraw_core_prim_flyweight *fw)
{
    uint32_t prim_type = fw->data[0];

    if (prim_type <= YETTY_YDRAW_CMD_END) {
        if (prim_type == YETTY_YDRAW_CMD_ZERO) {
            ydebug("process_input: CMD_ZERO — clearing canvas + cursor (0,0)");
            yetty_ydraw_scrolling_canvas_clear(canvas);
            struct yetty_ycore_grid_cursor_pos pos = {.cols = 0, .rows = 0};
            struct yetty_ycore_void_result cr =
                yetty_ydraw_scrolling_canvas_set_cursor_pos(canvas, pos);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
            canvas->env_initial_canvas_line =
                canvas->rolling_row_0 + canvas->cursor_row;
            canvas->env_max_row_seen = canvas->env_initial_canvas_line;
        }
    } else if (prim_type == YETTY_YDRAW_TYPE_FONT) {
        struct yetty_ydraw_core_font_prim_view fv;
        if (yetty_ydraw_core_font_prim_parse(fw->data, &fv) == 0 && fv.font_id >= 0) {
            char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
            size_t hl = fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
            memcpy(hint, fv.name, hl);
            hint[hl] = '\0';
            char hex[17];
            struct yetty_ycore_void_result er = ydraw_canvas_ensure_blob_font_cdb(
                canvas->base, fv.ttf, fv.ttf_len, hint, hex);
            if (YETTY_IS_ERR(er)) {
                ywarn("process_input: font CDB ensure failed (font_id=%d hint='%s'): %s",
                      fv.font_id, hint, er.error.msg);
                yetty_ycore_error_destroy(er.error);
            } else {
                ydraw_canvas_font_map_grow(&canvas->env_fonts_map,
                                            (uint32_t)fv.font_id + 1);
                memcpy(canvas->env_fonts_map.entries[fv.font_id].hex, hex, 17);
                canvas->env_fonts_map.entries[fv.font_id].declared = true;
            }
        }
    } else if (prim_type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        struct yetty_ydraw_core_text_span_prim_view tv;
        if (yetty_ydraw_core_text_span_prim_parse(fw->data, &tv) == 0) {
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
                    if (YETTY_IS_OK(rr)) {
                        e->font = rr.value.font;
                        e->handle = rr.value.handle;
                        e->resolved = true;
                        font = e->font;
                        handle = e->handle;
                    } else {
                        ywarn("process_input: font resolve failed (font_id=%d): %s",
                              tv.font_id, rr.error.msg);
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
                struct uint32_result gmr = expand_text_span_to_glyphs(canvas, &tv, font, handle);
                if (YETTY_IS_OK(gmr)) {
                    if (gmr.value > canvas->env_max_row_seen) {
                        canvas->env_max_row_seen = gmr.value;
                    }
                    ydraw_canvas_buffer_attach_note(&canvas->env_attach_list, handle, gmr.value);
                } else {
                    yetty_ycore_error_destroy(gmr.error);
                }
            }
        }
    } else {
        struct uint32_result prim_res = add_primitive_internal(canvas, fw);
        if (YETTY_IS_ERR(prim_res)) {
            yerror("process_input: add_primitive_internal failed (continuing): %s",
                   prim_res.error.msg);
            yetty_ycore_error_destroy(prim_res.error);
        } else if (prim_res.value > canvas->env_max_row_seen) {
            canvas->env_max_row_seen = prim_res.value;
        }
    }
}

static struct yetty_ycore_void_result scrolling_process_input_impl(
    struct yetty_ydraw_canvas *base, struct yetty_yterm_osc_statemachine *sm)
{
    struct yetty_ydraw_scrolling_canvas *canvas = base->impl;

    /* First entry into this envelope — set up per-envelope state on the
     * variant struct (survives mid-envelope WOULD_BLOCK returns). */
    if (!canvas->env_active) {
        struct yetty_ycore_void_result ir = yetty_ydraw_core_prim_iter_init(
            &canvas->env_iter, sm, canvas->base->flyweight_registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "process_input: prim_iter init");
        ydraw_canvas_font_map_init(&canvas->env_fonts_map);
        ydraw_canvas_buffer_attach_init(&canvas->env_attach_list);
        canvas->env_initial_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
        canvas->env_max_row_seen = canvas->env_initial_canvas_line;
        canvas->env_active = true;
    }

    /* Pull-and-dispatch loop. */
    for (;;) {
        struct yetty_ydraw_core_prim_iter_status_result sr =
            yetty_ydraw_core_prim_iter_next(&canvas->env_iter);
        if (YETTY_IS_ERR(sr)) {
            yerror("process_input: prim_iter error — resetting envelope state: %s",
                   sr.error.msg ? sr.error.msg : "(no msg)");
            yetty_ycore_error_destroy(sr.error);
            scrolling_env_reset(canvas);
            return YETTY_ERR(yetty_ycore_void, "process_input: prim_iter failed");
        }
        if (sr.value == YETTY_PRIM_ITER_WOULD_BLOCK) {
            /* SM yielded mid-envelope — keep state, return so the event
             * loop can do something else; SM re-dispatches us later. */
            return YETTY_OK_VOID();
        }
        if (sr.value == YETTY_PRIM_ITER_DONE) {
            break;
        }
        /* OK — fw is valid until the next _next call. */
        scrolling_dispatch_one(canvas, &canvas->env_iter.fw);
    }

    /* Envelope drained cleanly — run finalisation, reset, return. */
    struct yetty_ycore_void_result fr = scrolling_env_finalize(canvas);
    scrolling_env_reset(canvas);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "process_input: finalize failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scrolling_clear_impl(
    struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_clear(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static uint32_t scrolling_primitive_count_impl(const struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_primitive_count(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static struct yetty_ycore_void_result scrolling_rebuild_grid_impl(
    struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_rebuild_grid(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static struct yetty_ydraw_prim_staging_result scrolling_build_prim_staging_impl(
    struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_build_prim_staging(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static uint32_t scrolling_prim_gpu_size_impl(const struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_prim_gpu_size(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static uint32_t scrolling_complex_prim_count_impl(const struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_complex_prim_count(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static struct yetty_ydraw_core_complex_prim_instance *scrolling_get_complex_prim_impl(
    const struct yetty_ydraw_canvas *base, uint32_t index)
{
    return yetty_ydraw_scrolling_canvas_get_complex_prim(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, index);
}

static void scrolling_for_each_glyph_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    yetty_ydraw_scrolling_canvas_for_each_glyph(
        (struct yetty_ydraw_scrolling_canvas *)base->impl,
        visitor, user);
}

static struct yetty_ycore_void_result scrolling_set_cursor_pos_impl(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_cursor_pos pos)
{
    return yetty_ydraw_scrolling_canvas_set_cursor_pos(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, pos);
}

static struct yetty_ycore_void_result scrolling_scroll_lines_impl(
    struct yetty_ydraw_canvas *base, uint16_t num_lines)
{
    return yetty_ydraw_scrolling_canvas_scroll_lines(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, num_lines);
}

static struct yetty_ycore_void_result scrolling_set_view_top_impl(
    struct yetty_ydraw_canvas *base, bool active, uint32_t view_top)
{
    return yetty_ydraw_scrolling_canvas_set_view_top(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, active, view_top);
}

static uint32_t scrolling_rolling_row_0_impl(struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_rolling_row_0(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static uint32_t scrolling_live_rolling_row_0_impl(struct yetty_ydraw_canvas *base)
{
    return yetty_ydraw_scrolling_canvas_live_rolling_row_0(
        (struct yetty_ydraw_scrolling_canvas *)base->impl);
}

static struct yetty_ycore_void_result scrolling_set_scroll_callback_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_scroll_callback callback, void *userdata)
{
    return yetty_ydraw_scrolling_canvas_set_scroll_callback(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, callback, userdata);
}

static struct yetty_ycore_void_result scrolling_set_cursor_callback_impl(
    struct yetty_ydraw_canvas *base,
    yetty_ydraw_canvas_cursor_callback callback, void *userdata)
{
    return yetty_ydraw_scrolling_canvas_set_cursor_callback(
        (struct yetty_ydraw_scrolling_canvas *)base->impl, callback, userdata);
}

static const struct yetty_ydraw_canvas_ops scrolling_canvas_ops = {
    .name               = "scrolling",
    .destroy            = scrolling_destroy_impl,
    .set_grid_size      = scrolling_set_grid_size_impl,
    .process_input      = scrolling_process_input_impl,
    .clear              = scrolling_clear_impl,
    .primitive_count    = scrolling_primitive_count_impl,
    .rebuild_grid       = scrolling_rebuild_grid_impl,
    .build_prim_staging = scrolling_build_prim_staging_impl,
    .prim_gpu_size      = scrolling_prim_gpu_size_impl,
    .complex_prim_count = scrolling_complex_prim_count_impl,
    .get_complex_prim   = scrolling_get_complex_prim_impl,
    .for_each_glyph     = scrolling_for_each_glyph_impl,
    .set_cursor_pos     = scrolling_set_cursor_pos_impl,
    .scroll_lines       = scrolling_scroll_lines_impl,
    .set_view_top       = scrolling_set_view_top_impl,
    .rolling_row_0      = scrolling_rolling_row_0_impl,
    .live_rolling_row_0 = scrolling_live_rolling_row_0_impl,
    .set_scroll_callback = scrolling_set_scroll_callback_impl,
    .set_cursor_callback = scrolling_set_cursor_callback_impl,
};
