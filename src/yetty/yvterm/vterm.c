/*
 * vterm.c — the terminal content as a yfigure: class@yvterm:vterm.
 *
 * This is the main class. It is a yclass figure (parent yfigure:figure) that
 * owns the unified grid model whose datatypes live in grid.c. Raw PTY bytes
 * enter libvterm; the libvterm State callbacks below transform the ONE model
 * (text cells + per-cell rich refs + anchored primitive/composite storage on a
 * scrolling line ring). The figure render slot reads that model — it is not a
 * second source of truth and there is no text-layer/ydraw-layer split.
 *
 * grid.c carries only the data layout (no methods, no callbacks); it is
 * included here so this implementation TU sees the struct definitions. The
 * generated public header (vterm.h) is a downstream artifact for other modules
 * and is deliberately NOT included here — the foundational types are pulled in
 * directly and this TU declares its own yetty_yvterm_vterm_ptr_result, the same
 * one vterm.h publishes for consumers.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vterm.h>
#include <vterm_keycodes.h>

#include <webgpu/webgpu.h>

#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

/* GPU cell layout the text shader reads: 4 u32 words per cell. */
enum { YVTERM_WORDS_PER_CELL = 4 };

/* Must match the Uniforms struct in the text shader (text_wgsl below). */
struct vterm_uniforms {
    float grid_size[2];
    float cell_size[2];
    float scale;
    float baseline_y;
    float glyph_left;
    float pixel_range;
    uint32_t root_row;
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t cursor_visible;
};

/* `struct yetty_ydraw_composite` is forward-declared in grid.c and kept opaque
 * here on purpose: it is defined in BOTH ydraw-core/figure.h and
 * ydraw-factory/composite-factory.h (a pre-existing duplicate), so pulling
 * either defining header would clash in any consumer that includes the other.
 * The factory teardown is declared by hand so this TU only ever uses the type
 * by pointer — which also lets codegen forward-declare it in the generated
 * header rather than including a (conflicting) defining header. */
struct yetty_ydraw_composite;
void yetty_ydraw_composite_destroy(struct yetty_ydraw_composite *instance);
/* Render an anchored composite at a pixel origin — hand-declared free function
 * (defined in ydraw-factory) so this TU can draw composites without including
 * the conflicting defining header. */
struct yetty_ycore_void_result yetty_ydraw_composite_render(struct yetty_ydraw_composite *instance,
                                                            struct yetty_ydraw_target *target,
                                                            float x, float y);

/* The render slot takes the render target only by pointer and never derefs it
 * here, so a forward decl suffices — avoids pulling the webgpu-heavy
 * render-target.h into the model TU (matches figure.c's own forward decls). */
struct yetty_ydraw_target;

/* The unified model datatypes (data layout only). */
#include "grid.c"

/* Absolute lowest stacking order: the container sorts children by (z, seq) and
 * renders back-to-front, so the most-negative z renders first (the floor). The
 * terminal content must sit below every other figure. */
#define YETTY_YVTERM_VTERM_Z (-2000000000)

/*===========================================================================
 * Model helpers — operate on the unified grid datatypes from grid.c.
 *=========================================================================*/

static inline uint32_t pack_color(VTermColor color)
{
    return (uint32_t)color.red | ((uint32_t)color.green << 8) | ((uint32_t)color.blue << 16) |
           (0xFFu << 24);
}

static inline uint32_t ring_slot(const struct yetty_yvterm_grid *grid, uint32_t row)
{
    if (!grid->visible_rows) {
        return row;
    }
    uint32_t slot = grid->base + row;
    if (slot >= grid->visible_rows) {
        slot -= grid->visible_rows;
    }
    return slot;
}

static inline struct yetty_yvterm_line *line_at(struct yetty_yvterm_grid *grid, uint32_t row)
{
    return &grid->lines[ring_slot(grid, row)];
}

static inline struct yetty_yvterm_text_cell *cell_at(struct yetty_yvterm_grid *grid, uint32_t row,
                                                     uint32_t col)
{
    return &line_at(grid, row)->text_cells[col];
}

static inline struct yetty_yvterm_drawable_refs *refs_at(struct yetty_yvterm_grid *grid,
                                                         uint32_t row, uint32_t col)
{
    return &line_at(grid, row)->drawable_refs[col];
}

static void destroy_composite(struct yetty_ydraw_composite *composite)
{
    if (composite) {
        yetty_ydraw_composite_destroy(composite);
    }
}

/* Growable backing arrays — each grows capacity (doubling, clamped to need). */

static struct yetty_ycore_void_result ensure_primitive_refs(
    struct yetty_yvterm_drawable_refs *refs, uint16_t need)
{
    if (need <= refs->primitive_ref_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = refs->primitive_ref_capacity ? refs->primitive_ref_capacity : 2u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    if (new_cap > UINT16_MAX) {
        new_cap = UINT16_MAX;
    }
    struct yetty_yvterm_primitive_ref *grown =
        realloc(refs->primitive_refs, (size_t)new_cap * sizeof(*grown));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yvterm: primitive_refs grow failed");
    }
    refs->primitive_refs = grown;
    refs->primitive_ref_capacity = (uint16_t)new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_composite_refs(
    struct yetty_yvterm_drawable_refs *refs, uint16_t need)
{
    if (need <= refs->composite_ref_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = refs->composite_ref_capacity ? refs->composite_ref_capacity : 2u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    if (new_cap > UINT16_MAX) {
        new_cap = UINT16_MAX;
    }
    struct yetty_yvterm_composite_ref *grown =
        realloc(refs->composite_refs, (size_t)new_cap * sizeof(*grown));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yvterm: composite_refs grow failed");
    }
    refs->composite_refs = grown;
    refs->composite_ref_capacity = (uint16_t)new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_arena(struct yetty_yvterm_line *line, uint32_t need)
{
    if (need <= line->arena_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = line->arena_capacity ? line->arena_capacity : 16u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    uint32_t *grown = realloc(line->arena, (size_t)new_cap * sizeof(*grown));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yvterm: arena grow failed");
    }
    line->arena = grown;
    line->arena_capacity = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_primitives(struct yetty_yvterm_line *line,
                                                        uint32_t need)
{
    if (need <= line->primitive_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = line->primitive_capacity ? line->primitive_capacity : 4u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    struct yetty_yvterm_primitive *grown =
        realloc(line->primitives, (size_t)new_cap * sizeof(*grown));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yvterm: primitives grow failed");
    }
    line->primitives = grown;
    line->primitive_capacity = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_composites(struct yetty_yvterm_line *line,
                                                        uint32_t need)
{
    if (need <= line->composite_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = line->composite_capacity ? line->composite_capacity : 4u;
    while (new_cap < need) {
        new_cap *= 2u;
    }
    struct yetty_ydraw_composite **grown =
        realloc(line->composites, (size_t)new_cap * sizeof(struct yetty_ydraw_composite *));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yvterm: composites grow failed");
    }
    line->composites = grown;
    line->composite_capacity = new_cap;
    return YETTY_OK_VOID();
}

static void free_refs(struct yetty_yvterm_drawable_refs *refs)
{
    free(refs->primitive_refs);
    refs->primitive_refs = NULL;
    refs->primitive_ref_count = 0;
    refs->primitive_ref_capacity = 0;

    free(refs->composite_refs);
    refs->composite_refs = NULL;
    refs->composite_ref_count = 0;
    refs->composite_ref_capacity = 0;
}

/* Per-cell borrow drop: forget which rich content covers this cell. Never
 * destroys the anchor line's owned content. */
static void clear_refs(struct yetty_yvterm_drawable_refs *refs)
{
    refs->primitive_ref_count = 0;
    refs->composite_ref_count = 0;
}

/* Full line ownership clear: drop every cell's borrows AND release the line's
 * owned rich content (arena/primitives reset, composites destroyed). */
static void clear_line_rich(struct yetty_yvterm_line *line, uint32_t cols)
{
    if (line->drawable_refs) {
        for (uint32_t col = 0; col < cols; ++col) {
            clear_refs(&line->drawable_refs[col]);
        }
    }
    line->primitive_count = 0;
    line->arena_count = 0;
    for (uint32_t i = 0; i < line->composite_count; ++i) {
        destroy_composite(line->composites[i]);
        line->composites[i] = NULL;
    }
    line->composite_count = 0;
}

/* Remove every borrowed ref (on any visible line) that points to anchor_row, so
 * clearing that anchor line's owned storage cannot leave a dangling ref behind.
 * The anchor convention is downward (anchor = covered_row + rel_line), so only
 * rows at or above anchor_row can reference it. */
static void drop_refs_to_anchor(struct yetty_yvterm_grid *grid, uint32_t anchor_row)
{
    if (anchor_row >= grid->visible_rows) {
        return;
    }
    for (uint32_t row = 0; row <= anchor_row; ++row) {
        struct yetty_yvterm_line *line = line_at(grid, row);
        if (!line->drawable_refs) {
            continue;
        }
        int touched = 0;
        for (uint32_t col = 0; col < grid->cols; ++col) {
            struct yetty_yvterm_drawable_refs *refs = &line->drawable_refs[col];
            uint16_t kept = 0;
            for (uint16_t i = 0; i < refs->primitive_ref_count; ++i) {
                if ((uint32_t)row + refs->primitive_refs[i].rel_line != anchor_row) {
                    refs->primitive_refs[kept++] = refs->primitive_refs[i];
                }
            }
            if (kept != refs->primitive_ref_count) {
                refs->primitive_ref_count = kept;
                touched = 1;
            }
            kept = 0;
            for (uint16_t i = 0; i < refs->composite_ref_count; ++i) {
                if ((uint32_t)row + refs->composite_refs[i].rel_line != anchor_row) {
                    refs->composite_refs[kept++] = refs->composite_refs[i];
                }
            }
            if (kept != refs->composite_ref_count) {
                refs->composite_ref_count = kept;
                touched = 1;
            }
        }
        if (touched) {
            line->dirty = 1;
            grid->has_dirty = 1;
        }
    }
}

static void blank_cell(struct yetty_yvterm_text_cell *cell, uint32_t fg, uint32_t bg)
{
    cell->glyph_index = 0;
    cell->codepoint = 0;
    cell->fg = fg;
    cell->bg = bg;
    cell->attrs = 0;
    cell->width = 1;
    cell->flags = 0;
}

static void reset_line(struct yetty_yvterm_grid *grid, struct yetty_yvterm_line *line)
{
    for (uint32_t col = 0; col < grid->cols; ++col) {
        blank_cell(&line->text_cells[col], grid->default_fg, grid->default_bg);
    }
    clear_line_rich(line, grid->cols);
}

static void blank_line(struct yetty_yvterm_grid *grid, uint32_t row)
{
    reset_line(grid, line_at(grid, row));
}

static void mark_dirty_row(struct yetty_yvterm_grid *grid, uint32_t row)
{
    if (row >= grid->visible_rows) {
        return;
    }
    line_at(grid, row)->dirty = 1;
    grid->has_dirty = 1;
}

static void mark_dirty_all(struct yetty_yvterm_grid *grid)
{
    for (uint32_t i = 0; i < grid->line_count; ++i) {
        grid->lines[i].dirty = 1;
    }
    grid->has_dirty = 1;
}

/* O(1) whole-screen scroll-up: blank rolled-off slots (dropping rich
 * ownership) and advance the ring base so everything below moves together. */
static void roll_up(struct yetty_yvterm_grid *grid, uint32_t count)
{
    if (grid->visible_rows == 0) {
        return;
    }
    if (count > grid->visible_rows) {
        count = grid->visible_rows;
    }
    for (uint32_t step = 0; step < count; ++step) {
        uint32_t slot = grid->base + step;
        if (slot >= grid->visible_rows) {
            slot -= grid->visible_rows;
        }
        reset_line(grid, &grid->lines[slot]);
        grid->lines[slot].dirty = 1;
    }
    grid->base += count;
    while (grid->base >= grid->visible_rows) {
        grid->base -= grid->visible_rows;
    }
    grid->has_dirty = 1;
}

/* Move one cell's full state — text plus borrowed primitive/composite refs.
 *
 * Borrowed refs name their anchor relative to the *covered* row
 * (anchor = covered_row + rel_line). Moving a cell to a different row must keep
 * the absolute anchor fixed, so rel_line is rebased by (src_row - dst_row). A
 * ref whose rebased rel_line would point above the cell (negative — violating
 * the downward-anchor convention) or past the visible grid is dropped rather
 * than left dangling. Line-owned primitive/composite *storage* is not touched
 * here: it lives on its anchor line and is addressed by index, so a cell move
 * only needs to keep the references consistent. Best-effort on allocation: a
 * libvterm callback cannot propagate an error, so it keeps what fits. */
static void move_cell(struct yetty_yvterm_grid *grid, uint32_t dst_row, uint32_t dst_col,
                      uint32_t src_row, uint32_t src_col)
{
    *cell_at(grid, dst_row, dst_col) = *cell_at(grid, src_row, src_col);

    struct yetty_yvterm_drawable_refs *dst = refs_at(grid, dst_row, dst_col);
    struct yetty_yvterm_drawable_refs *src = refs_at(grid, src_row, src_col);
    int rel_delta = (int)src_row - (int)dst_row;
    dst->primitive_ref_count = 0;
    dst->composite_ref_count = 0;

    struct yetty_ycore_void_result pr = ensure_primitive_refs(dst, src->primitive_ref_count);
    if (YETTY_IS_OK(pr)) {
        for (uint16_t i = 0; i < src->primitive_ref_count; ++i) {
            int new_rel = (int)src->primitive_refs[i].rel_line + rel_delta;
            if (new_rel < 0 || new_rel > UINT16_MAX ||
                dst_row + (uint32_t)new_rel >= grid->visible_rows) {
                continue;
            }
            dst->primitive_refs[dst->primitive_ref_count].rel_line = (uint16_t)new_rel;
            dst->primitive_refs[dst->primitive_ref_count].index_in_list =
                src->primitive_refs[i].index_in_list;
            dst->primitive_ref_count++;
        }
    } else {
        yetty_ycore_error_destroy(pr.error);
    }

    struct yetty_ycore_void_result cr = ensure_composite_refs(dst, src->composite_ref_count);
    if (YETTY_IS_OK(cr)) {
        for (uint16_t i = 0; i < src->composite_ref_count; ++i) {
            int new_rel = (int)src->composite_refs[i].rel_line + rel_delta;
            if (new_rel < 0 || new_rel > UINT16_MAX ||
                dst_row + (uint32_t)new_rel >= grid->visible_rows) {
                continue;
            }
            dst->composite_refs[dst->composite_ref_count].rel_line = (uint16_t)new_rel;
            dst->composite_refs[dst->composite_ref_count].index_in_list =
                src->composite_refs[i].index_in_list;
            dst->composite_ref_count++;
        }
    } else {
        yetty_ycore_error_destroy(cr.error);
    }
}

/*===========================================================================
 * libvterm State callbacks — transform the one model. user = grid pointer.
 *=========================================================================*/

YETTY_EXTERNAL_CALLBACK
static int cb_putglyph(VTermGlyphInfo *info, VTermPos pos, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    if (pos.row < 0 || pos.col < 0 || (uint32_t)pos.row >= grid->visible_rows ||
        (uint32_t)pos.col >= grid->cols) {
        return 1;
    }

    uint32_t fg = grid->pen_reverse ? grid->pen_bg : grid->pen_fg;
    uint32_t bg = grid->pen_reverse ? grid->pen_fg : grid->pen_bg;
    struct yetty_yvterm_text_cell *cell = cell_at(grid, (uint32_t)pos.row, (uint32_t)pos.col);
    cell->glyph_index = 0;
    cell->codepoint = (info->chars && info->chars[0]) ? info->chars[0] : 0u;
    cell->fg = fg;
    cell->bg = bg;
    cell->attrs = grid->pen_attrs;
    cell->width = (uint8_t)(info->width >= 2 ? 2 : 1);
    cell->flags = 0;

    /* A text write overwrites the cell, so its rich-content borrows go stale. */
    clear_refs(refs_at(grid, (uint32_t)pos.row, (uint32_t)pos.col));

    if (info->width == 2 && (uint32_t)pos.col + 1u < grid->cols) {
        struct yetty_yvterm_text_cell *next =
            cell_at(grid, (uint32_t)pos.row, (uint32_t)pos.col + 1u);
        next->glyph_index = 0;
        next->codepoint = 0;
        next->fg = fg;
        next->bg = bg;
        next->attrs = grid->pen_attrs;
        next->width = 0;
        next->flags = 0;
        clear_refs(refs_at(grid, (uint32_t)pos.row, (uint32_t)pos.col + 1u));
    }

    /* If this row anchors owned primitives/composites, writing text over it
     * invalidates that rich content: release the owned storage and drop every
     * borrowed ref (on any row) that pointed at this anchor, so nothing dangles.
     * primitive_count/composite_count drop to 0, so later glyphs on the same
     * line skip this cheaply. */
    struct yetty_yvterm_line *line = line_at(grid, (uint32_t)pos.row);
    if (line->primitive_count || line->composite_count) {
        clear_line_rich(line, grid->cols);
        drop_refs_to_anchor(grid, (uint32_t)pos.row);
    }

    mark_dirty_row(grid, (uint32_t)pos.row);
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    if (oldpos.row >= 0) {
        mark_dirty_row(grid, (uint32_t)oldpos.row);
    }
    grid->cursor_row = pos.row >= 0 ? (uint32_t)pos.row : 0u;
    grid->cursor_col = pos.col >= 0 ? (uint32_t)pos.col : 0u;
    grid->cursor_visible = visible ? 1u : 0u;
    mark_dirty_row(grid, grid->cursor_row);
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_moverect(VTermRect dest, VTermRect src, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    int row_count = dest.end_row - dest.start_row;
    int col_count = dest.end_col - dest.start_col;
    if (row_count <= 0 || col_count <= 0) {
        return 1;
    }

    /* Respect overlap direction on BOTH axes exactly like memmove. Pick the row
     * and column traversal order independently from the sign of the shift: when
     * the destination is past the source on an axis, walk that axis backwards so
     * a source cell is always read before a later write can overwrite it. This
     * covers vertical moves, same-row horizontal moves, and diagonal moves. */
    int row_shift = dest.start_row - src.start_row;
    int col_shift = dest.start_col - src.start_col;
    for (int row_step = 0; row_step < row_count; ++row_step) {
        int row = (row_shift > 0) ? (row_count - 1 - row_step) : row_step;
        for (int col_step = 0; col_step < col_count; ++col_step) {
            int col = (col_shift > 0) ? (col_count - 1 - col_step) : col_step;
            move_cell(grid, (uint32_t)(dest.start_row + row), (uint32_t)(dest.start_col + col),
                      (uint32_t)(src.start_row + row), (uint32_t)(src.start_col + col));
        }
        mark_dirty_row(grid, (uint32_t)(dest.start_row + row));
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_erase(VTermRect rect, int selective, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    (void)selective;

    int whole_screen = rect.start_row <= 0 && rect.start_col <= 0 &&
                       (uint32_t)rect.end_row >= grid->visible_rows &&
                       (uint32_t)rect.end_col >= grid->cols;

    if (whole_screen) {
        for (uint32_t row = 0; row < grid->visible_rows; ++row) {
            reset_line(grid, line_at(grid, row));
            mark_dirty_row(grid, row);
        }
        if (grid->clear_hook_fn) {
            struct yetty_ycore_void_result r = grid->clear_hook_fn(grid->clear_hook_userdata);
            if (YETTY_IS_ERR(r)) {
                yetty_ycore_error_destroy(r.error);
            }
        }
        return 1;
    }

    for (int row = rect.start_row; row < rect.end_row; ++row) {
        if (row < 0 || (uint32_t)row >= grid->visible_rows) {
            continue;
        }
        for (int col = rect.start_col; col < rect.end_col; ++col) {
            if (col < 0 || (uint32_t)col >= grid->cols) {
                continue;
            }
            blank_cell(cell_at(grid, (uint32_t)row, (uint32_t)col), grid->default_fg,
                       grid->default_bg);
            clear_refs(refs_at(grid, (uint32_t)row, (uint32_t)col));
        }
        /* Erasing cells on a row that anchors owned rich content invalidates
         * that content too — release it and drop refs pointing here, mirroring
         * cb_putglyph's anchor-line invalidation. */
        struct yetty_yvterm_line *line = line_at(grid, (uint32_t)row);
        if (line->primitive_count || line->composite_count) {
            clear_line_rich(line, grid->cols);
            drop_refs_to_anchor(grid, (uint32_t)row);
        }
        mark_dirty_row(grid, (uint32_t)row);
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_scrollrect(VTermRect rect, int downward, int rightward, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    int whole_width = rect.start_col == 0 && (uint32_t)rect.end_col == grid->cols;
    int whole_height = rect.start_row == 0 && (uint32_t)rect.end_row == grid->visible_rows;
    /* The common whole-screen scroll-up is O(1): advance the ring. Everything
     * else decomposes through the unified move/erase callbacks. */
    if (rightward == 0 && downward > 0 && whole_width && whole_height) {
        roll_up(grid, (uint32_t)downward);
        return 1;
    }
    vterm_scroll_rect(rect, downward, rightward, cb_moverect, cb_erase, grid);
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_initpen(void *user)
{
    struct yetty_yvterm_grid *grid = user;
    grid->pen_fg = grid->default_fg;
    grid->pen_bg = grid->default_bg;
    grid->pen_attrs = 0;
    grid->pen_reverse = 0;
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_setpenattr(VTermAttr attr, VTermValue *val, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    switch (attr) {
    case VTERM_ATTR_BOLD:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_ATTR_BOLD)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_ATTR_BOLD);
        break;
    case VTERM_ATTR_UNDERLINE: {
        uint16_t mask = YETTY_YVTERM_ATTR_UNDERLINE | YETTY_YVTERM_ATTR_UNDERLINE2;
        grid->pen_attrs =
            (uint16_t)((grid->pen_attrs & ~mask) | (((uint16_t)(val->number & 0x3)) << 1));
        break;
    }
    case VTERM_ATTR_ITALIC:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_ATTR_ITALIC)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_ATTR_ITALIC);
        break;
    case VTERM_ATTR_BLINK:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_ATTR_BLINK)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_ATTR_BLINK);
        break;
    case VTERM_ATTR_STRIKE:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_ATTR_STRIKE)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_ATTR_STRIKE);
        break;
    case VTERM_ATTR_CONCEAL:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_ATTR_CONCEAL)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_ATTR_CONCEAL);
        break;
    case VTERM_ATTR_REVERSE:
        grid->pen_reverse = val->boolean ? 1 : 0;
        break;
    case VTERM_ATTR_FOREGROUND:
        grid->pen_fg = pack_color(val->color);
        break;
    case VTERM_ATTR_BACKGROUND:
        grid->pen_bg = pack_color(val->color);
        break;
    default:
        break;
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_settermprop(VTermProp prop, VTermValue *val, void *user)
{
    struct yetty_yvterm_grid *grid = user;
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        grid->cursor_visible = val->boolean ? 1u : 0u;
        mark_dirty_row(grid, grid->cursor_row);
        break;
    case VTERM_PROP_ALTSCREEN:
        grid->alt_screen = val->boolean ? 1 : 0;
        mark_dirty_all(grid);
        break;
    case VTERM_PROP_MOUSE:
        grid->mouse_mode = (int)val->number;
        break;
    default:
        break;
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_bell(void *user)
{
    (void)user;
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_resize(int rows, int cols, VTermStateFields *fields, void *user)
{
    (void)cols;
    (void)user;
    /* libvterm has already updated state->rows/state->cols and delegates the
     * lineinfo (re)allocation to this callback — its internal fallback is dead
     * code once a state resize callback is installed. Each non-NULL lineinfos[]
     * buffer must be grown/shrunk to `rows` entries here; otherwise the next
     * glyph indexes the old, shorter buffer out of bounds (heap corruption,
     * then a crash). The default vterm allocator is libc malloc/free, so plain
     * calloc/free matches what libvterm will later free. Our grid model blanks
     * every line on resize, so fresh zeroed lineinfo (no flag carry-over) is
     * the correct semantics — mirrors libvterm's own screen-layer resize. */
    int safe_rows = rows > 0 ? rows : 1;
    for (int bufidx = 0; bufidx < 2; ++bufidx) {
        if (!fields->lineinfos[bufidx]) {
            continue;
        }
        VTermLineInfo *resized = calloc((size_t)safe_rows, sizeof(VTermLineInfo));
        if (!resized) {
            /* Keep the existing buffer rather than crash; libvterm clamps the
             * cursor below, so a stale-but-valid buffer is the safer failure. */
            continue;
        }
        free(fields->lineinfos[bufidx]);
        fields->lineinfos[bufidx] = resized;
    }
    return 1;
}

static const VTermStateCallbacks *grid_state_callbacks(void)
{
    static const VTermStateCallbacks callbacks = {
        .putglyph = cb_putglyph,
        .movecursor = cb_movecursor,
        .scrollrect = cb_scrollrect,
        .moverect = cb_moverect,
        .erase = cb_erase,
        .initpen = cb_initpen,
        .setpenattr = cb_setpenattr,
        .settermprop = cb_settermprop,
        .bell = cb_bell,
        .resize = cb_resize,
        .setlineinfo = NULL,
        .sb_clear = NULL,
    };
    return &callbacks;
}

/*===========================================================================
 * Grid model lifecycle — the model lives embedded in the vterm object.
 *=========================================================================*/

static void grid_free_lines(struct yetty_yvterm_grid *grid)
{
    if (!grid->lines) {
        return;
    }
    for (uint32_t line = 0; line < grid->line_count; ++line) {
        struct yetty_yvterm_line *current = &grid->lines[line];
        if (current->drawable_refs) {
            for (uint32_t col = 0; col < grid->cols; ++col) {
                free_refs(&current->drawable_refs[col]);
            }
        }
        for (uint32_t i = 0; i < current->composite_count; ++i) {
            destroy_composite(current->composites[i]);
        }
        free(current->text_cells);
        free(current->drawable_refs);
        free(current->primitives);
        free(current->arena);
        free(current->composites);
    }
    free(grid->lines);
    grid->lines = NULL;
}

static struct yetty_ycore_void_result grid_alloc_lines(struct yetty_yvterm_grid *grid,
                                                       uint32_t rows, uint32_t cols)
{
    grid->lines = calloc(rows, sizeof(struct yetty_yvterm_line));
    if (!grid->lines) {
        return YETTY_ERR(yetty_ycore_void, "yvterm: line array allocation failed");
    }
    grid->line_count = rows;
    for (uint32_t line = 0; line < rows; ++line) {
        grid->lines[line].text_cells = calloc(cols, sizeof(struct yetty_yvterm_text_cell));
        grid->lines[line].drawable_refs = calloc(cols, sizeof(struct yetty_yvterm_drawable_refs));
        if (!grid->lines[line].text_cells || !grid->lines[line].drawable_refs) {
            grid_free_lines(grid);
            return YETTY_ERR(yetty_ycore_void, "yvterm: line allocation failed");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_model_init(struct yetty_yvterm_grid *grid, uint32_t cols,
                                                      uint32_t rows)
{
    grid->cols = cols;
    grid->visible_rows = rows;

    struct yetty_ycore_void_result lines_r = grid_alloc_lines(grid, rows, cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lines_r, "yvterm: grid_model_init alloc_lines");

    grid->vterm = vterm_new((int)rows, (int)cols);
    if (!grid->vterm) {
        grid_free_lines(grid);
        return YETTY_ERR(yetty_ycore_void, "yvterm: vterm_new failed");
    }
    vterm_set_utf8(grid->vterm, 1);
    grid->state = vterm_obtain_state(grid->vterm);
    vterm_state_set_callbacks(grid->state, grid_state_callbacks(), grid);

    VTermColor default_fg;
    VTermColor default_bg;
    vterm_state_get_default_colors(grid->state, &default_fg, &default_bg);
    grid->default_fg = pack_color(default_fg);
    grid->default_bg = pack_color(default_bg);

    vterm_state_reset(grid->state, 1);
    grid->pen_fg = grid->default_fg;
    grid->pen_bg = grid->default_bg;
    grid->cursor_visible = 1u;

    for (uint32_t row = 0; row < rows; ++row) {
        blank_line(grid, row);
    }
    mark_dirty_all(grid);
    return YETTY_OK_VOID();
}

static void grid_model_teardown(struct yetty_yvterm_grid *grid)
{
    if (grid->vterm) {
        vterm_free(grid->vterm);
        grid->vterm = NULL;
        grid->state = NULL;
    }
    grid_free_lines(grid);
}

static struct yetty_ycore_void_result grid_model_resize(struct yetty_yvterm_grid *grid,
                                                        uint32_t cols, uint32_t rows)
{
    struct yetty_yvterm_grid tmp = {0};
    tmp.cols = cols;
    tmp.visible_rows = rows;
    tmp.default_fg = grid->default_fg;
    tmp.default_bg = grid->default_bg;
    struct yetty_ycore_void_result lines_r = grid_alloc_lines(&tmp, rows, cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, lines_r, "yvterm: grid_model_resize alloc_lines");

    grid_free_lines(grid);
    grid->lines = tmp.lines;
    grid->line_count = tmp.line_count;
    grid->cols = cols;
    grid->visible_rows = rows;
    grid->base = 0;

    for (uint32_t row = 0; row < rows; ++row) {
        blank_line(grid, row);
    }
    vterm_set_size(grid->vterm, (int)rows, (int)cols);
    mark_dirty_all(grid);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * The vterm yclass class — a yfigure subclass owning the unified model.
 *=========================================================================*/

struct [[clang::annotate("class@yvterm:vterm")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_yvterm_vterm {
    /* The unified terminal model (text + rich refs on a scrolling ring). */
    struct yetty_yvterm_grid grid;

    /* Cell + grid metrics mirrored for the terminal's mouse→cell mapping, PTY
     * resize, and the figure rect. The terminal sets these via resize. */
    struct yetty_ycore_grid_size grid_size;
    struct yetty_ycore_pixel_size cell_size;

    /* Content insets in pane-local pixels — a docked status bar / HUD reserved
     * a band of the pane; the render slot narrows its viewport to the rest. */
    float content_inset_top;
    float content_inset_right;
    float content_inset_bottom;
    float content_inset_left;

    /* Terminal-owned hooks. pty_write delivers keyboard output to the child;
     * request_render asks the terminal for a frame; mouse_sub reports libvterm
     * mouse-mode changes. */
    yetty_yterminal_pty_write_fn pty_write_fn;
    void *pty_write_userdata;
    yetty_yterminal_request_render_fn request_render_fn;
    void *request_render_userdata;
    yetty_yterminal_mouse_sub_fn mouse_sub_fn;
    void *mouse_sub_userdata;

    /* Selection rectangle in grid cells (terminal-driven). */
    int selection_active;
    uint32_t selection_anchor_row;
    uint32_t selection_anchor_col;
    uint32_t selection_head_row;
    uint32_t selection_head_col;

    /* Scrollback view (not yet backed by a scrollback ring — see methods). */
    int view_top_active;
    uint32_t view_top_total_idx;

    /* Visual zoom (applied by the renderer build-out). */
    float visual_zoom_scale;
    float visual_zoom_offset_x;
    float visual_zoom_offset_y;

    /* ---- GPU text renderer (ported from poc/yvterm-new) ---- */
    /* Borrowed from the framework. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_yfont_ms_font *font;

    /* Owned wgpu resources. */
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;
    WGPUBuffer cell_buffer;
    WGPUBuffer meta_buffer;
    WGPUBuffer uniform_buffer;
    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;
    uint32_t atlas_width;
    uint32_t atlas_height;
    size_t meta_capacity;
    WGPUBindGroup bind_group;
    int bind_group_valid;
    uint32_t cell_buffer_cells; /* capacity in cells */

    struct vterm_uniforms uniforms;
    uint32_t *row_scratch; /* cols * WORDS_PER_CELL scratch reused per line */
    uint32_t row_scratch_cols;
    int gpu_ready; /* pipeline + font built */
};

/* Result wrapper for the vterm handle. Declared here (this TU does not include
 * its own generated header); vterm.h publishes the identical declaration. */
YETTY_YRESULT_DECLARE(yetty_yvterm_vterm_ptr, struct yetty_yvterm_vterm *);

/* Defined in the appended vterm.gen.c. */
struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void);
struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj);

/* Resolve obj → body, absorbing the error into NULL for void/scalar getters. */
static struct yetty_yvterm_vterm *vterm_body_or_null(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    if (YETTY_IS_ERR(vterm_res)) {
        yetty_ycore_error_destroy(vterm_res.error);
        return NULL;
    }
    return vterm_res.value;
}

/*===========================================================================
 * GPU text renderer — ported from poc/yvterm-new. Resolves each cell's glyph
 * against the active MSDF font, packs the 16-byte/cell layout the text shader
 * reads, uploads dirty lines into one pinned storage buffer, and draws a single
 * full-screen quad into the figure's render target. CPU truth stays the model.
 *=========================================================================*/

static WGPUStringView vterm_sv(const char *text)
{
    return (WGPUStringView){.data = text, .length = strlen(text)};
}

/* The text grid shader (mirror of poc/yvterm-new/text.wgsl). Local static so it
 * is program-lifetime storage without a file-scope symbol. */
static const char *vterm_text_wgsl(void)
{
    static const char src[] =
        "struct Uniforms {\n"
        "    grid_size: vec2<f32>,\n"
        "    cell_size: vec2<f32>,\n"
        "    scale: f32,\n"
        "    baseline_y: f32,\n"
        "    glyph_left: f32,\n"
        "    pixel_range: f32,\n"
        "    root_row: u32,\n"
        "    cursor_col: u32,\n"
        "    cursor_row: u32,\n"
        "    cursor_visible: u32,\n"
        "};\n"
        "@group(0) @binding(0) var<storage, read> cells: array<u32>;\n"
        "@group(0) @binding(1) var<storage, read> glyph_meta: array<u32>;\n"
        "@group(0) @binding(2) var atlas_tex: texture_2d<f32>;\n"
        "@group(0) @binding(3) var atlas_smp: sampler;\n"
        "@group(0) @binding(4) var<uniform> uni: Uniforms;\n"
        "struct VSOut {\n"
        "    @builtin(position) pos: vec4<f32>,\n"
        "    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,\n"
        "};\n"
        "@vertex\n"
        "fn vs_main(@builtin(vertex_index) vid: u32) -> VSOut {\n"
        "    var corners = array<vec2<f32>, 6>(\n"
        "        vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,-1.0), vec2<f32>(1.0,1.0),\n"
        "        vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,1.0), vec2<f32>(-1.0,1.0));\n"
        "    let ndc = corners[vid];\n"
        "    var out: VSOut;\n"
        "    out.pos = vec4<f32>(ndc, 0.0, 1.0);\n"
        "    let grid_w = uni.grid_size.x * uni.cell_size.x;\n"
        "    let grid_h = uni.grid_size.y * uni.cell_size.y;\n"
        "    out.grid_pixel = vec2<f32>((ndc.x*0.5+0.5)*grid_w, (0.5-ndc.y*0.5)*grid_h);\n"
        "    return out;\n"
        "}\n"
        "fn median3(r: f32, g: f32, b: f32) -> f32 {\n"
        "    return max(min(r,g), min(max(r,g), b));\n"
        "}\n"
        "fn sample_glyph(glyph: u32, local_px: vec2<f32>) -> f32 {\n"
        "    let base = glyph * 10u;\n"
        "    let uv_min = vec2<f32>(bitcast<f32>(glyph_meta[base+0u]), "
        "bitcast<f32>(glyph_meta[base+1u]));\n"
        "    let uv_max = vec2<f32>(bitcast<f32>(glyph_meta[base+2u]), "
        "bitcast<f32>(glyph_meta[base+3u]));\n"
        "    let gsize = vec2<f32>(bitcast<f32>(glyph_meta[base+4u]), "
        "bitcast<f32>(glyph_meta[base+5u]));\n"
        "    let bear = vec2<f32>(bitcast<f32>(glyph_meta[base+6u]), "
        "bitcast<f32>(glyph_meta[base+7u]));\n"
        "    if (gsize.x <= 0.0 || gsize.y <= 0.0) { return 0.0; }\n"
        "    let scaled_size = gsize * uni.scale;\n"
        "    let scaled_bear = bear * uni.scale;\n"
        "    let gtop = uni.baseline_y - scaled_bear.y;\n"
        "    let gleft = uni.glyph_left + scaled_bear.x;\n"
        "    let gmin = vec2<f32>(gleft, gtop);\n"
        "    let gmax = vec2<f32>(gleft + scaled_size.x, gtop + scaled_size.y);\n"
        "    if (local_px.x < gmin.x || local_px.x >= gmax.x || local_px.y < gmin.y || "
        "local_px.y >= gmax.y) { return 0.0; }\n"
        "    let gl = (local_px - gmin) / scaled_size;\n"
        "    let uv = mix(uv_min, uv_max, gl);\n"
        "    let texel = textureSampleLevel(atlas_tex, atlas_smp, uv, 0.0);\n"
        "    let sd = median3(texel.r, texel.g, texel.b);\n"
        "    let screen_px_range = uni.pixel_range * uni.scale;\n"
        "    return clamp((sd - 0.5) * screen_px_range + 0.5, 0.0, 1.0);\n"
        "}\n"
        "@fragment\n"
        "fn fs_main(in: VSOut) -> @location(0) vec4<f32> {\n"
        "    let grid_w = uni.grid_size.x * uni.cell_size.x;\n"
        "    let grid_h = uni.grid_size.y * uni.cell_size.y;\n"
        "    let px = in.grid_pixel;\n"
        "    if (px.x < 0.0 || px.y < 0.0 || px.x >= grid_w || px.y >= grid_h) {\n"
        "        return vec4<f32>(0.0,0.0,0.0,1.0);\n"
        "    }\n"
        "    let colf = floor(px.x / uni.cell_size.x);\n"
        "    let rowf = floor(px.y / uni.cell_size.y);\n"
        "    let col = u32(colf);\n"
        "    let row = u32(rowf);\n"
        "    let gcols = u32(uni.grid_size.x);\n"
        "    let slot = (row + uni.root_row) % u32(uni.grid_size.y);\n"
        "    let cell_index = slot * gcols + col;\n"
        "    var local = vec2<f32>(px.x - colf*uni.cell_size.x, px.y - rowf*uni.cell_size.y);\n"
        "    var glyph = cells[cell_index*4u + 0u];\n"
        "    let w1 = cells[cell_index*4u + 1u];\n"
        "    let w2 = cells[cell_index*4u + 2u];\n"
        "    let attrs = (w2 >> 16u) & 0xFFFFu;\n"
        "    let fg = vec3<f32>(f32(w1 & 0xFFu)/255.0, f32((w1>>8u)&0xFFu)/255.0, "
        "f32((w1>>16u)&0xFFu)/255.0);\n"
        "    let bg = vec3<f32>(f32((w1>>24u)&0xFFu)/255.0, f32(w2 & 0xFFu)/255.0, "
        "f32((w2>>8u)&0xFFu)/255.0);\n"
        /* A wide glyph occupies two cells: the head (width 2) holds the glyph,
         * the spill cell (width 0) to its right is blank. Continue sampling the
         * head glyph into the spill cell, shifted one cell to the right, so the
         * right half of CJK/double-width glyphs is drawn. */
        "    if ((cells[cell_index*4u + 3u] & 0xFFu) == 0u && col > 0u) {\n"
        "        let head = slot * gcols + (col - 1u);\n"
        "        if ((cells[head*4u + 3u] & 0xFFu) == 2u) {\n"
        "            glyph = cells[head*4u + 0u];\n"
        "            local.x = local.x + uni.cell_size.x;\n"
        "        }\n"
        "    }\n"
        "    var alpha = 0.0;\n"
        "    if (glyph != 0u) { alpha = sample_glyph(glyph, local); }\n"
        /* Underline (single 0x2 or double 0x4) sits just below the baseline;
         * strikethrough (0x40) crosses the cell middle. Both paint at full
         * coverage so they show on blank cells too. */
        "    let line_h = max(1.0, uni.cell_size.y * 0.07);\n"
        "    let ul_y = uni.baseline_y + max(1.0, uni.cell_size.y * 0.10);\n"
        "    if ((attrs & 0x6u) != 0u && local.y >= ul_y && local.y < ul_y + line_h) "
        "{ alpha = 1.0; }\n"
        "    let st_y = uni.cell_size.y * 0.5;\n"
        "    if ((attrs & 0x40u) != 0u && local.y >= st_y && local.y < st_y + line_h) "
        "{ alpha = 1.0; }\n"
        "    let is_cursor = uni.cursor_visible != 0u && col == uni.cursor_col && "
        "row == uni.cursor_row;\n"
        "    var composed = mix(bg, fg, alpha);\n"
        "    if (is_cursor) {\n"
        "        composed = mix(fg, bg, alpha);\n" /* inverted block: fg fill, glyph punched in bg */
        "    }\n"
        "    return vec4<f32>(composed, 1.0);\n"
        "}\n";
    return src;
}

/* Resolve a codepoint to an atlas glyph index for the given style. The styled
 * lookup lazy-loads the (style, codepoint) slot on demand, falling back to the
 * Regular face when a bold/italic CDB variant is absent. */
static uint32_t vterm_resolve_glyph(struct yetty_yvterm_vterm *vterm, uint32_t codepoint,
                                    enum yetty_yfont_ms_style style)
{
    if (!vterm->font || codepoint == 0) {
        return 0;
    }
    struct uint32_result gi =
        vterm->font->ops->get_glyph_index_styled(vterm->font, codepoint, style);
    if (YETTY_IS_OK(gi)) {
        return gi.value;
    }
    yetty_ycore_error_destroy(gi.error);
    return 0;
}

/* Map cell text attributes to a font style variant (bold/italic combinations). */
static enum yetty_yfont_ms_style vterm_cell_style(uint16_t attrs)
{
    int style = YETTY_YFONT_MS_STYLE_REGULAR;
    if (attrs & YETTY_YVTERM_ATTR_BOLD) {
        style |= YETTY_YFONT_MS_STYLE_BOLD;
    }
    if (attrs & YETTY_YVTERM_ATTR_ITALIC) {
        style |= YETTY_YFONT_MS_STYLE_ITALIC;
    }
    return (enum yetty_yfont_ms_style)style;
}

/* Pack one model line into out[cols*4] in the GPU layout the shader reads. */
static void vterm_pack_line(struct yetty_yvterm_vterm *vterm, struct yetty_yvterm_line *line,
                            uint32_t *out)
{
    uint32_t cols = vterm->grid.cols;
    for (uint32_t col = 0; col < cols; ++col) {
        struct yetty_yvterm_text_cell *cell = &line->text_cells[col];
        /* Concealed cells render their background only — resolve no glyph.
         * Bold/italic pick a styled atlas slot via the cell attributes. */
        uint32_t glyph = 0u;
        if (cell->codepoint && !(cell->attrs & YETTY_YVTERM_ATTR_CONCEAL)) {
            glyph = vterm_resolve_glyph(vterm, cell->codepoint, vterm_cell_style(cell->attrs));
        }
        uint32_t fg = cell->fg;
        uint32_t bg = cell->bg;
        uint32_t fr = fg & 0xFFu, fgr = (fg >> 8) & 0xFFu, fb = (fg >> 16) & 0xFFu;
        uint32_t br = bg & 0xFFu, bgr = (bg >> 8) & 0xFFu, bb = (bg >> 16) & 0xFFu;
        uint32_t *words = &out[(size_t)col * YVTERM_WORDS_PER_CELL];
        words[0] = glyph;
        words[1] = fr | (fgr << 8) | (fb << 16) | (br << 24);
        words[2] = bgr | (bb << 8) | ((uint32_t)cell->attrs << 16);
        words[3] = cell->width;
    }
}

static struct yetty_ycore_void_result vterm_ensure_row_scratch(struct yetty_yvterm_vterm *vterm)
{
    uint32_t cols = vterm->grid.cols;
    if (vterm->row_scratch && vterm->row_scratch_cols >= cols) {
        return YETTY_OK_VOID();
    }
    free(vterm->row_scratch);
    vterm->row_scratch = calloc((size_t)cols * YVTERM_WORDS_PER_CELL, sizeof(uint32_t));
    if (!vterm->row_scratch) {
        vterm->row_scratch_cols = 0;
        return YETTY_ERR(yetty_ycore_void, "vterm: row scratch alloc");
    }
    vterm->row_scratch_cols = cols;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_create_pipeline(struct yetty_yvterm_vterm *vterm)
{
    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = vterm_sv(vterm_text_wgsl());
    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = &wgsl.chain;
    shader_desc.label = vterm_sv("yvterm text shader");
    vterm->shader_module = wgpuDeviceCreateShaderModule(vterm->device, &shader_desc);
    if (!vterm->shader_module) {
        return YETTY_ERR(yetty_ycore_void, "vterm: shader module");
    }

    WGPUBindGroupLayoutEntry entries[5] = {0};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    entries[4].buffer.minBindingSize = sizeof(struct vterm_uniforms);
    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 5;
    bgl_desc.entries = entries;
    vterm->bind_group_layout = wgpuDeviceCreateBindGroupLayout(vterm->device, &bgl_desc);
    if (!vterm->bind_group_layout) {
        return YETTY_ERR(yetty_ycore_void, "vterm: bind group layout");
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &vterm->bind_group_layout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(vterm->device, &pl_desc);
    if (!layout) {
        return YETTY_ERR(yetty_ycore_void, "vterm: pipeline layout");
    }

    WGPUColorTargetState color_target = {0};
    color_target.format = vterm->target_format;
    color_target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment = {0};
    fragment.module = vterm->shader_module;
    fragment.entryPoint = vterm_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;
    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.label = vterm_sv("yvterm pipeline");
    rp_desc.layout = layout;
    rp_desc.vertex.module = vterm->shader_module;
    rp_desc.vertex.entryPoint = vterm_sv("vs_main");
    rp_desc.fragment = &fragment;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;
    vterm->pipeline = wgpuDeviceCreateRenderPipeline(vterm->device, &rp_desc);
    wgpuPipelineLayoutRelease(layout);
    if (!vterm->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "vterm: render pipeline");
    }

    WGPUSamplerDescriptor sampler_desc = {0};
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.maxAnisotropy = 1;
    vterm->sampler = wgpuDeviceCreateSampler(vterm->device, &sampler_desc);
    if (!vterm->sampler) {
        return YETTY_ERR(yetty_ycore_void, "vterm: sampler");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_create_cell_buffer(struct yetty_yvterm_vterm *vterm)
{
    uint32_t cols = vterm->grid.cols, rows = vterm->grid.visible_rows;
    if (vterm->cell_buffer) {
        wgpuBufferDestroy(vterm->cell_buffer);
        wgpuBufferRelease(vterm->cell_buffer);
        vterm->cell_buffer = NULL;
    }
    uint64_t size = (uint64_t)rows * cols * YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    WGPUBufferDescriptor desc = {0};
    desc.label = vterm_sv("yvterm cells");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    vterm->cell_buffer = wgpuDeviceCreateBuffer(vterm->device, &desc);
    if (!vterm->cell_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm: cell buffer");
    }
    vterm->cell_buffer_cells = rows * cols;
    vterm->bind_group_valid = 0;

    if (!vterm->uniform_buffer) {
        WGPUBufferDescriptor uni_desc = {0};
        uni_desc.label = vterm_sv("yvterm uniforms");
        uni_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uni_desc.size = sizeof(struct vterm_uniforms);
        vterm->uniform_buffer = wgpuDeviceCreateBuffer(vterm->device, &uni_desc);
        if (!vterm->uniform_buffer) {
            return YETTY_ERR(yetty_ycore_void, "vterm: uniform buffer");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_recreate_atlas(struct yetty_yvterm_vterm *vterm,
                                                           uint32_t width, uint32_t height)
{
    if (vterm->atlas_view) {
        wgpuTextureViewRelease(vterm->atlas_view);
        vterm->atlas_view = NULL;
    }
    if (vterm->atlas_texture) {
        wgpuTextureDestroy(vterm->atlas_texture);
        wgpuTextureRelease(vterm->atlas_texture);
        vterm->atlas_texture = NULL;
    }
    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.label = vterm_sv("yvterm atlas");
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size.width = width;
    tex_desc.size.height = height;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    vterm->atlas_texture = wgpuDeviceCreateTexture(vterm->device, &tex_desc);
    if (!vterm->atlas_texture) {
        return YETTY_ERR(yetty_ycore_void, "vterm: atlas texture");
    }
    vterm->atlas_view = wgpuTextureCreateView(vterm->atlas_texture, NULL);
    if (!vterm->atlas_view) {
        return YETTY_ERR(yetty_ycore_void, "vterm: atlas view");
    }
    vterm->atlas_width = width;
    vterm->atlas_height = height;
    vterm->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_recreate_meta(struct yetty_yvterm_vterm *vterm,
                                                          size_t size)
{
    if (vterm->meta_buffer) {
        wgpuBufferDestroy(vterm->meta_buffer);
        wgpuBufferRelease(vterm->meta_buffer);
        vterm->meta_buffer = NULL;
    }
    WGPUBufferDescriptor desc = {0};
    desc.label = vterm_sv("yvterm meta");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    vterm->meta_buffer = wgpuDeviceCreateBuffer(vterm->device, &desc);
    if (!vterm->meta_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm: meta buffer");
    }
    vterm->meta_capacity = size;
    vterm->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_upload_font(struct yetty_yvterm_vterm *vterm)
{
    struct yetty_yrender_gpu_resource_set_result rs_res =
        vterm->font->ops->get_gpu_resource_set(vterm->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "vterm_upload_font: get_gpu_resource_set");
    const struct yetty_yrender_gpu_resource_set *rs = rs_res.value;

    vterm->uniforms.pixel_range = rs->uniforms[0].f32;
    vterm->uniforms.scale = rs->uniforms[1].f32;
    vterm->uniforms.baseline_y = rs->uniforms[2].f32;
    vterm->uniforms.glyph_left = rs->uniforms[3].f32;

    const struct yetty_yrender_texture *atlas = &rs->textures[0];
    if (atlas->data && atlas->width > 0 && atlas->height > 0) {
        if (!vterm->atlas_texture || atlas->width != vterm->atlas_width ||
            atlas->height != vterm->atlas_height) {
            struct yetty_ycore_void_result re =
                vterm_recreate_atlas(vterm, atlas->width, atlas->height);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "vterm_upload_font: recreate_atlas");
        }
        WGPUTexelCopyTextureInfo dest = {0};
        dest.texture = vterm->atlas_texture;
        dest.mipLevel = 0;
        WGPUTexelCopyBufferLayout src_layout = {0};
        src_layout.bytesPerRow = atlas->width * 4u;
        src_layout.rowsPerImage = atlas->height;
        WGPUExtent3D extent = {atlas->width, atlas->height, 1};
        size_t bytes = (size_t)atlas->width * atlas->height * 4u;
        wgpuQueueWriteTexture(vterm->queue, &dest, atlas->data, bytes, &src_layout, &extent);
    }

    const struct yetty_yrender_buffer *meta = &rs->buffers[0];
    if (meta->data && meta->size > 0) {
        if (!vterm->meta_buffer || meta->size > vterm->meta_capacity) {
            struct yetty_ycore_void_result re = vterm_recreate_meta(vterm, meta->size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "vterm_upload_font: recreate_meta");
        }
        wgpuQueueWriteBuffer(vterm->queue, vterm->meta_buffer, 0, meta->data, meta->size);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_ensure_bind_group(struct yetty_yvterm_vterm *vterm)
{
    if (vterm->bind_group_valid && vterm->bind_group) {
        return YETTY_OK_VOID();
    }
    if (vterm->bind_group) {
        wgpuBindGroupRelease(vterm->bind_group);
        vterm->bind_group = NULL;
    }
    if (!vterm->cell_buffer || !vterm->meta_buffer || !vterm->atlas_view || !vterm->sampler ||
        !vterm->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm_ensure_bind_group: missing resource");
    }
    WGPUBindGroupEntry entries[5] = {0};
    entries[0].binding = 0;
    entries[0].buffer = vterm->cell_buffer;
    entries[0].size = WGPU_WHOLE_SIZE;
    entries[1].binding = 1;
    entries[1].buffer = vterm->meta_buffer;
    entries[1].size = WGPU_WHOLE_SIZE;
    entries[2].binding = 2;
    entries[2].textureView = vterm->atlas_view;
    entries[3].binding = 3;
    entries[3].sampler = vterm->sampler;
    entries[4].binding = 4;
    entries[4].buffer = vterm->uniform_buffer;
    entries[4].size = sizeof(struct vterm_uniforms);
    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = vterm->bind_group_layout;
    bg_desc.entryCount = 5;
    bg_desc.entries = entries;
    vterm->bind_group = wgpuDeviceCreateBindGroup(vterm->device, &bg_desc);
    if (!vterm->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "vterm_ensure_bind_group: create");
    }
    vterm->bind_group_valid = 1;
    return YETTY_OK_VOID();
}

/* Best-effort GPU setup. On failure leaves gpu_ready=0 so the figure still
 * exists (blank pane) rather than failing terminal creation. */
static void vterm_gpu_init(struct yetty_yvterm_vterm *vterm, const struct yetty_context *context)
{
    if (!context || !context->runtime) {
        return;
    }
    struct yetty_yframework *runtime = context->runtime;
    vterm->device = runtime->gpu.device;
    vterm->queue = runtime->gpu.queue;
    vterm->target_format = runtime->gpu.surface_format;
    if (!vterm->device || !vterm->queue) {
        return;
    }

    struct yetty_yconfig_config *config = runtime->config;
    const char *fonts_dir = config ? config->ops->get_string(config, "paths/fonts", "") : "";
    const char *shaders_dir = config ? config->ops->get_string(config, "paths/shaders", "") : "";
    float content_scale = runtime->gpu.app_gpu_context.content_scale;
    if (content_scale <= 0.0f) {
        content_scale = 1.0f;
    }
    /* Config carries the font size in logical (CSS-style) pixels; scale once
     * here to framebuffer pixels so the glyphs render at the right physical
     * size on HiDPI displays without every other pipeline stage having to know
     * about content_scale. */
    int cfg_size = config ? config->ops->get_int(config, "terminal/text-layer/font/size", 14) : 14;
    float font_size = (float)cfg_size * content_scale;

    char cdb_path[768];
    char font_shader_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb",
             fonts_dir);
    snprintf(font_shader_path, sizeof(font_shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);
    struct yetty_yfont_ms_padding padding = {0};
    struct yetty_font_ms_font_result font_res =
        yetty_yfont_ms_msdf_font_create(cdb_path, font_shader_path, font_size, padding);
    if (YETTY_IS_ERR(font_res)) {
        ywarn("vterm: font create failed (%s): %s", cdb_path, font_res.error.msg);
        yetty_ycore_error_destroy(font_res.error);
        return;
    }
    vterm->font = font_res.value;
    struct yetty_ycore_void_result latin = vterm->font->ops->load_basic_latin(vterm->font);
    if (YETTY_IS_ERR(latin)) {
        yetty_ycore_error_destroy(latin.error);
    }
    struct pixel_size_result cell = vterm->font->ops->get_cell_size(vterm->font);
    if (YETTY_IS_OK(cell)) {
        vterm->cell_size = cell.value;
    }

    struct yetty_ycore_void_result pr = vterm_create_pipeline(vterm);
    if (YETTY_IS_ERR(pr)) {
        ywarn("vterm: pipeline init failed: %s", pr.error.msg);
        yetty_ycore_error_destroy(pr.error);
        return;
    }
    struct yetty_ycore_void_result cbr = vterm_create_cell_buffer(vterm);
    if (YETTY_IS_ERR(cbr)) {
        yetty_ycore_error_destroy(cbr.error);
        return;
    }
    struct yetty_ycore_void_result ufr = vterm_upload_font(vterm);
    if (YETTY_IS_ERR(ufr)) {
        yetty_ycore_error_destroy(ufr.error);
        return;
    }
    vterm->uniforms.cell_size[0] = vterm->cell_size.width;
    vterm->uniforms.cell_size[1] = vterm->cell_size.height;
    vterm->gpu_ready = 1;
}

static void vterm_gpu_destroy(struct yetty_yvterm_vterm *vterm)
{
    if (vterm->bind_group) {
        wgpuBindGroupRelease(vterm->bind_group);
        vterm->bind_group = NULL;
    }
    if (vterm->atlas_view) {
        wgpuTextureViewRelease(vterm->atlas_view);
        vterm->atlas_view = NULL;
    }
    if (vterm->atlas_texture) {
        wgpuTextureDestroy(vterm->atlas_texture);
        wgpuTextureRelease(vterm->atlas_texture);
        vterm->atlas_texture = NULL;
    }
    if (vterm->meta_buffer) {
        wgpuBufferDestroy(vterm->meta_buffer);
        wgpuBufferRelease(vterm->meta_buffer);
        vterm->meta_buffer = NULL;
    }
    if (vterm->uniform_buffer) {
        wgpuBufferDestroy(vterm->uniform_buffer);
        wgpuBufferRelease(vterm->uniform_buffer);
        vterm->uniform_buffer = NULL;
    }
    if (vterm->cell_buffer) {
        wgpuBufferDestroy(vterm->cell_buffer);
        wgpuBufferRelease(vterm->cell_buffer);
        vterm->cell_buffer = NULL;
    }
    if (vterm->sampler) {
        wgpuSamplerRelease(vterm->sampler);
        vterm->sampler = NULL;
    }
    if (vterm->pipeline) {
        wgpuRenderPipelineRelease(vterm->pipeline);
        vterm->pipeline = NULL;
    }
    if (vterm->bind_group_layout) {
        wgpuBindGroupLayoutRelease(vterm->bind_group_layout);
        vterm->bind_group_layout = NULL;
    }
    if (vterm->shader_module) {
        wgpuShaderModuleRelease(vterm->shader_module);
        vterm->shader_module = NULL;
    }
    free(vterm->row_scratch);
    vterm->row_scratch = NULL;
    vterm->row_scratch_cols = 0;
    if (vterm->font) {
        vterm->font->ops->destroy(vterm->font);
        vterm->font = NULL;
    }
    vterm->gpu_ready = 0;
}

/* Walk dirty model lines, upload them, and draw the grid into the target. */
static struct yetty_ycore_void_result vterm_render_grid(struct yetty_yvterm_vterm *vterm,
                                                        struct yetty_ydraw_target *target,
                                                        struct yetty_ycore_rectangle rect)
{
    if (!vterm->gpu_ready || !target || !target->ops || !target->ops->get_view) {
        return YETTY_OK_VOID();
    }
    struct yetty_yvterm_grid *grid = &vterm->grid;
    if (grid->cols == 0 || grid->visible_rows == 0) {
        return YETTY_OK_VOID();
    }
    /* The model may have been resized since the cell buffer was sized. */
    if (vterm->cell_buffer_cells != grid->visible_rows * grid->cols) {
        struct yetty_ycore_void_result br = vterm_create_cell_buffer(vterm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "vterm_render: resize cell buffer");
    }
    struct yetty_ycore_void_result rsr = vterm_ensure_row_scratch(vterm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rsr, "vterm_render: row scratch");

    size_t row_bytes = (size_t)grid->cols * YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    for (uint32_t slot = 0; slot < grid->visible_rows; ++slot) {
        if (!grid->lines[slot].dirty) {
            continue;
        }
        vterm_pack_line(vterm, &grid->lines[slot], vterm->row_scratch);
        wgpuQueueWriteBuffer(vterm->queue, vterm->cell_buffer, (uint64_t)slot * row_bytes,
                             vterm->row_scratch, row_bytes);
    }
    /* Glyph resolution above may have grown the atlas/meta — re-pull the font. */
    if (vterm->font->ops->is_dirty(vterm->font)) {
        struct yetty_ycore_void_result fr = vterm_upload_font(vterm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "vterm_render: upload_font");
    }

    vterm->uniforms.grid_size[0] = (float)grid->cols;
    vterm->uniforms.grid_size[1] = (float)grid->visible_rows;
    vterm->uniforms.cell_size[0] = vterm->cell_size.width;
    vterm->uniforms.cell_size[1] = vterm->cell_size.height;
    vterm->uniforms.root_row = grid->base;
    vterm->uniforms.cursor_col = grid->cursor_col;
    vterm->uniforms.cursor_row = grid->cursor_row;
    vterm->uniforms.cursor_visible = grid->cursor_visible;
    wgpuQueueWriteBuffer(vterm->queue, vterm->uniform_buffer, 0, &vterm->uniforms,
                         sizeof(struct vterm_uniforms));

    struct yetty_ycore_void_result bg = vterm_ensure_bind_group(vterm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bg, "vterm_render: bind group");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "vterm_render: target view NULL");
    }
    /* The figure rect is pane-local (origin 0,0); the pane's framebuffer
     * position lives in the target viewport origin (e.g. y=36 below the
     * tabbar). Offset the draw viewport by it so terminal row 0 lands at the
     * pane top instead of behind the tabbar — otherwise an idle shell's single
     * prompt row is drawn at y=0 and scissored away, leaving a blank pane. */
    float vx = target->viewport.x + rect.min.x;
    float vy = target->viewport.y + rect.min.y;
    float w = rect.max.x - rect.min.x;
    float h = rect.max.y - rect.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(vterm->device, &enc_desc);
    if (!enc) {
        return YETTY_ERR(yetty_ycore_void, "vterm_render: encoder");
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
    if (!pass) {
        wgpuCommandEncoderRelease(enc);
        return YETTY_ERR(yetty_ycore_void, "vterm_render: begin pass");
    }
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, w, h, 0.0f, 1.0f);
    /* Clamp scissor to the target viewport. */
    float tx0 = target->viewport.x, ty0 = target->viewport.y;
    float tx1 = tx0 + target->viewport.w, ty1 = ty0 + target->viewport.h;
    float sx0 = vx > tx0 ? vx : tx0;
    float sy0 = vy > ty0 ? vy : ty0;
    float sx1 = (vx + w) < tx1 ? (vx + w) : tx1;
    float sy1 = (vy + h) < ty1 ? (vy + h) : ty1;
    if (sx1 > sx0 && sy1 > sy0) {
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)sx0, (uint32_t)sy0,
                                            (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0));
        wgpuRenderPassEncoderSetPipeline(pass, vterm->pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, vterm->bind_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, &cmd_desc);
    wgpuQueueSubmit(vterm->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    /* Rich-content pass: anchored composites (yplot/yimage/… wrapped as
     * ydraw composites) scroll with the text. Whatever line currently sits at
     * visible row R draws its owned composites at that row's pixel origin, on
     * top of the text (each composite runs its own LoadOp_Load pass). The
     * vertical scroll offset + visual zoom match the text mapping. This is
     * consumed in the SAME render that clears the dirty flags below, so rich
     * updates are never dropped.
     *
     * NOTE: raw SDF *primitives* stored in the per-line arena are not drawn
     * here yet — that needs the ydraw SDF grid/binder pipeline (the deleted
     * ydraw-scrolling-layer). Composites cover the common anchored-figure case. */
    float zoom = vterm->visual_zoom_scale > 0.0f ? vterm->visual_zoom_scale : 1.0f;
    for (uint32_t row = 0; row < grid->visible_rows; ++row) {
        struct yetty_yvterm_line *anchor = line_at(grid, row);
        if (anchor->composite_count == 0) {
            continue;
        }
        float comp_x = rect.min.x + vterm->visual_zoom_offset_x;
        float comp_y = rect.min.y + vterm->visual_zoom_offset_y +
                       (float)row * vterm->cell_size.height * zoom;
        for (uint32_t ci = 0; ci < anchor->composite_count; ++ci) {
            struct yetty_ydraw_composite *comp = anchor->composites[ci];
            if (!comp) {
                continue;
            }
            struct yetty_ycore_void_result cr =
                yetty_ydraw_composite_render(comp, target, comp_x, comp_y);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
    }

    /* Uploaded — clear model dirty now that the renderer has consumed both the
     * text plane and the anchored composites. */
    for (uint32_t i = 0; i < grid->line_count; ++i) {
        grid->lines[i].dirty = 0;
    }
    grid->has_dirty = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure slots.
 *=========================================================================*/

/* Render the unified model into the figure's target: walk dirty lines, resolve
 * glyphs, upload, draw the grid quad. The render narrows to the inset content
 * rect (a docked HUD reserved a band) before drawing. */
[[clang::annotate("override@yvterm:vterm:yfigure:render")]]
static struct yetty_ycore_void_result vterm_render_slot(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_target *target)
{
    (void)ctx;
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "vterm_render: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;

    struct rectangle_result rect_res = yetty_yfigure_figure_rect_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "vterm_render: rect");
    struct yetty_ycore_rectangle rect = rect_res.value;
    rect.min.x += vterm->content_inset_left;
    rect.min.y += vterm->content_inset_top;
    rect.max.x -= vterm->content_inset_right;
    rect.max.y -= vterm->content_inset_bottom;

    return vterm_render_grid(vterm, target, rect);
}

[[clang::annotate("override@yvterm:vterm:yfigure:destroy")]]
static struct yetty_ycore_void_result vterm_destroy_slot(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "vterm_destroy: from_obj");
    vterm_gpu_destroy(vterm_res.value);
    grid_model_teardown(&vterm_res.value->grid);

    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_res, "vterm_destroy: object_free");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API — object-keyed; the terminal drives the figure through these.
 *=========================================================================*/

/* Fired after a full-screen erase clears the visible lines, so the terminal can
 * also drop external figures the model does not own. Identical underlying type
 * to grid.c's field, exposed here so the public API has a header type. */
[[clang::annotate("expose")]]
typedef struct yetty_ycore_void_result (*yetty_yvterm_clear_hook_fn)(void *userdata);

/* Create the terminal-content figure for one pane. The rect is set in pixels;
 * cell_size is stored so the terminal can read it immediately after create. The
 * terminal hooks (pty write, render request, mouse subscription) are stored for
 * keyboard output and mouse-mode reporting.
 *
 * cell_size starts at a placeholder until the renderer build-out resolves exact
 * metrics from the active font; the terminal overwrites it on the first resize.
 * `context` is accepted now for that future font setup. */
[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata)
{
    if (cols == 0 || rows == 0) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: invalid size");
    }

    struct yetty_yclass_ptr_result class_res = yetty_yvterm_vterm_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "yvterm vterm_create: class");

    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "yvterm vterm_create: object_alloc");
    struct yetty_yclass_object *obj = object_res.value;

    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    if (YETTY_IS_ERR(vterm_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: from_obj", vterm_res);
    }
    struct yetty_yvterm_vterm *vterm = vterm_res.value;

    struct yetty_ycore_void_result init_res = grid_model_init(&vterm->grid, cols, rows);
    if (YETTY_IS_ERR(init_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: grid_model_init", init_res);
    }
    vterm->grid_size = (struct yetty_ycore_grid_size){.cols = cols, .rows = rows};
    vterm->cell_size = (struct yetty_ycore_pixel_size){.width = 9.0f, .height = 18.0f};
    vterm->pty_write_fn = pty_write_fn;
    vterm->pty_write_userdata = pty_write_userdata;
    vterm->request_render_fn = request_render_fn;
    vterm->request_render_userdata = request_render_userdata;
    vterm->mouse_sub_fn = mouse_sub_fn;
    vterm->mouse_sub_userdata = mouse_sub_userdata;
    vterm->visual_zoom_scale = 1.0f;

    /* Best-effort: builds the pipeline/font and overrides cell_size with the
     * font's real metrics. On failure the figure still exists (blank pane). */
    vterm_gpu_init(vterm, context);

    struct yetty_ycore_rectangle rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)cols * vterm->cell_size.width,
                .y = (float)rows * vterm->cell_size.height},
    };
    struct yetty_ycore_void_result rect_res = yetty_yfigure_figure_rect_set(obj, rect);
    struct yetty_ycore_void_result z_res = YETTY_OK_VOID();
    struct yetty_ycore_void_result dirty_res = YETTY_OK_VOID();
    if (YETTY_IS_OK(rect_res)) {
        z_res = yetty_yfigure_figure_z_set(obj, YETTY_YVTERM_VTERM_Z);
    }
    if (YETTY_IS_OK(z_res)) {
        dirty_res = yetty_yfigure_figure_dirty_set(obj, 1);
    }
    /* On any figure-setup failure, tear down the model + GPU + object so the
     * initialized resources do not leak. */
    if (YETTY_IS_ERR(rect_res) || YETTY_IS_ERR(z_res) || YETTY_IS_ERR(dirty_res)) {
        vterm_gpu_destroy(vterm);
        grid_model_teardown(&vterm->grid);
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        if (YETTY_IS_ERR(rect_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: rect_set", rect_res);
        }
        if (YETTY_IS_ERR(z_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: z_set", z_res);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: dirty_set", dirty_res);
    }

    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Upcast to the figure base (first slice in the object). */
[[clang::annotate("expose")]]
struct yetty_yfigure_figure *yetty_yvterm_vterm_as_figure(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yfigure_figure_ptr_result figure_res = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(figure_res)) {
        yetty_ycore_error_destroy(figure_res.error);
        return NULL;
    }
    return figure_res.value;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_feed(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_feed: from_obj");
    if (len && !bytes) {
        return YETTY_ERR(yetty_ycore_void, "yvterm vterm_feed: NULL bytes");
    }
    vterm_input_write(vterm_res.value->grid.vterm, bytes, len);
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_resize(struct yetty_yclass_object *obj,
                                                         struct yetty_ycore_grid_size grid_size,
                                                         struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_resize: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;

    if (grid_size.cols == 0 || grid_size.rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "yvterm vterm_resize: invalid dimensions");
    }
    struct yetty_ycore_void_result resize_res =
        grid_model_resize(&vterm->grid, grid_size.cols, grid_size.rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resize_res, "yvterm vterm_resize: grid_model_resize");
    vterm->grid_size = grid_size;
    vterm->cell_size = cell_size;

    struct yetty_ycore_rectangle rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)grid_size.cols * cell_size.width,
                .y = (float)grid_size.rows * cell_size.height},
    };
    struct yetty_ycore_void_result rect_res = yetty_yfigure_figure_rect_set(obj, rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yvterm vterm_resize: rect_set");
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_pixel_size yetty_yvterm_vterm_cell_size(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return (struct yetty_ycore_pixel_size){0};
    }
    return vterm->cell_size;
}

[[clang::annotate("expose")]]
int yetty_yvterm_vterm_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    return vterm ? vterm->grid.has_dirty : 0;
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_set_content_inset(struct yetty_yclass_object *obj, float top, float right,
                                          float bottom, float left)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return;
    }
    vterm->content_inset_top = top;
    vterm->content_inset_right = right;
    vterm->content_inset_bottom = bottom;
    vterm->content_inset_left = left;
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_get_content_inset(struct yetty_yclass_object *obj, float *out_top,
                                          float *out_right, float *out_bottom, float *out_left)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (out_top) {
        *out_top = vterm ? vterm->content_inset_top : 0.0f;
    }
    if (out_right) {
        *out_right = vterm ? vterm->content_inset_right : 0.0f;
    }
    if (out_bottom) {
        *out_bottom = vterm ? vterm->content_inset_bottom : 0.0f;
    }
    if (out_left) {
        *out_left = vterm ? vterm->content_inset_left : 0.0f;
    }
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_set_clear_hook(struct yetty_yclass_object *obj,
                                       yetty_yvterm_clear_hook_fn fn, void *userdata)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return;
    }
    vterm->grid.clear_hook_fn = fn;
    vterm->grid.clear_hook_userdata = userdata;
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_cursor(struct yetty_yclass_object *obj, uint32_t *out_row, uint32_t *out_col,
                               uint32_t *out_visible)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (out_row) {
        *out_row = vterm ? vterm->grid.cursor_row : 0u;
    }
    if (out_col) {
        *out_col = vterm ? vterm->grid.cursor_col : 0u;
    }
    if (out_visible) {
        *out_visible = vterm ? vterm->grid.cursor_visible : 0u;
    }
}

/*===========================================================================
 * Rich-content insertion — primitives/composites anchored to a line; covered
 * cells reference them with a downward rel_line + the returned index.
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, vterm_res, "yvterm append_primitive: from_obj");
    struct yetty_yvterm_grid *grid = &vterm_res.value->grid;
    if (row >= grid->visible_rows) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm append_primitive: row out of range");
    }
    if (word_count && !words) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm append_primitive: NULL words");
    }
    struct yetty_yvterm_line *line = line_at(grid, row);
    if (line->primitive_count >= UINT16_MAX) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm append_primitive: too many primitives");
    }
    struct yetty_ycore_void_result ar = ensure_arena(line, line->arena_count + word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, ar, "yvterm append_primitive: arena");
    struct yetty_ycore_void_result pr = ensure_primitives(line, line->primitive_count + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, pr, "yvterm append_primitive: primitives");

    uint32_t arena_offset = line->arena_count;
    if (word_count) {
        memcpy(line->arena + arena_offset, words, (size_t)word_count * sizeof(uint32_t));
        line->arena_count += word_count;
    }
    uint32_t index = line->primitive_count;
    line->primitives[index].arena_offset = arena_offset;
    line->primitives[index].word_count = word_count;
    line->primitive_count++;
    mark_dirty_row(grid, row);
    return YETTY_OK(yetty_ycore_uint32, index);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_add_primitive_ref(
    struct yetty_yclass_object *obj, uint32_t row, uint32_t col, uint16_t rel_line,
    uint16_t index_in_list)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm add_primitive_ref: from_obj");
    struct yetty_yvterm_grid *grid = &vterm_res.value->grid;
    if (row >= grid->visible_rows || col >= grid->cols) {
        return YETTY_ERR(yetty_ycore_void, "yvterm add_primitive_ref: out of range");
    }
    /* The anchor (row + rel_line) must be a real visible line, and index_in_list
     * must name an existing primitive on it — otherwise the ref dangles. */
    uint32_t anchor_row = (uint32_t)row + rel_line;
    if (anchor_row >= grid->visible_rows) {
        return YETTY_ERR(yetty_ycore_void, "yvterm add_primitive_ref: anchor out of range");
    }
    if (index_in_list >= line_at(grid, anchor_row)->primitive_count) {
        return YETTY_ERR(yetty_ycore_void, "yvterm add_primitive_ref: primitive index out of range");
    }
    struct yetty_yvterm_drawable_refs *refs = refs_at(grid, row, col);
    struct yetty_ycore_void_result gr =
        ensure_primitive_refs(refs, (uint16_t)(refs->primitive_ref_count + 1u));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "yvterm add_primitive_ref: grow");
    refs->primitive_refs[refs->primitive_ref_count].rel_line = rel_line;
    refs->primitive_refs[refs->primitive_ref_count].index_in_list = index_in_list;
    refs->primitive_ref_count++;
    mark_dirty_row(grid, row);
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_uint32_result yetty_yvterm_vterm_attach_composite(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, vterm_res, "yvterm attach_composite: from_obj");
    struct yetty_yvterm_grid *grid = &vterm_res.value->grid;
    if (row >= grid->visible_rows) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm attach_composite: row out of range");
    }
    if (!composite) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm attach_composite: NULL composite");
    }
    struct yetty_yvterm_line *line = line_at(grid, row);
    if (line->composite_count >= UINT16_MAX) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm attach_composite: too many composites");
    }
    struct yetty_ycore_void_result gr = ensure_composites(line, line->composite_count + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, gr, "yvterm attach_composite: grow");
    uint32_t index = line->composite_count;
    line->composites[index] = composite;
    line->composite_count++;
    mark_dirty_row(grid, row);
    return YETTY_OK(yetty_ycore_uint32, index);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_add_composite_ref(
    struct yetty_yclass_object *obj, uint32_t row, uint32_t col, uint16_t rel_line,
    uint16_t index_in_list)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm add_composite_ref: from_obj");
    struct yetty_yvterm_grid *grid = &vterm_res.value->grid;
    if (row >= grid->visible_rows || col >= grid->cols) {
        return YETTY_ERR(yetty_ycore_void, "yvterm add_composite_ref: out of range");
    }
    /* The anchor (row + rel_line) must be a real visible line, and index_in_list
     * must name an existing composite on it — otherwise the ref dangles. */
    uint32_t anchor_row = (uint32_t)row + rel_line;
    if (anchor_row >= grid->visible_rows) {
        return YETTY_ERR(yetty_ycore_void, "yvterm add_composite_ref: anchor out of range");
    }
    if (index_in_list >= line_at(grid, anchor_row)->composite_count) {
        return YETTY_ERR(yetty_ycore_void, "yvterm add_composite_ref: composite index out of range");
    }
    struct yetty_yvterm_drawable_refs *refs = refs_at(grid, row, col);
    struct yetty_ycore_void_result gr =
        ensure_composite_refs(refs, (uint16_t)(refs->composite_ref_count + 1u));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "yvterm add_composite_ref: grow");
    refs->composite_refs[refs->composite_ref_count].rel_line = rel_line;
    refs->composite_refs[refs->composite_ref_count].index_in_list = index_in_list;
    refs->composite_ref_count++;
    mark_dirty_row(grid, row);
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_line(struct yetty_yclass_object *obj,
                                                                  uint32_t row)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm clear_rich_line: from_obj");
    struct yetty_yvterm_grid *grid = &vterm_res.value->grid;
    if (row >= grid->visible_rows) {
        return YETTY_ERR(yetty_ycore_void, "yvterm clear_rich_line: row out of range");
    }
    clear_line_rich(line_at(grid, row), grid->cols);
    /* Other visible lines may hold refs anchored here; drop them so clearing the
     * owned storage cannot leave a dangling ref. */
    drop_refs_to_anchor(grid, row);
    mark_dirty_row(grid, row);
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_all(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm clear_rich_all: from_obj");
    struct yetty_yvterm_grid *grid = &vterm_res.value->grid;
    for (uint32_t row = 0; row < grid->visible_rows; ++row) {
        clear_line_rich(line_at(grid, row), grid->cols);
        mark_dirty_row(grid, row);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Input, wire, selection, scrollback, zoom — terminal-facing methods.
 *=========================================================================*/

/* Map the terminal's GLFW-style modifier bitmask to libvterm modifiers. */
static VTermModifier vterm_map_mods(int mods)
{
    int out = VTERM_MOD_NONE;
    if (mods & 0x01) { /* shift */
        out |= VTERM_MOD_SHIFT;
    }
    if (mods & 0x02) { /* control */
        out |= VTERM_MOD_CTRL;
    }
    if (mods & 0x04) { /* alt */
        out |= VTERM_MOD_ALT;
    }
    return (VTermModifier)out;
}

/* Drain libvterm's keyboard output ring to the PTY via the terminal's hook. */
static struct yetty_ycore_void_result vterm_flush_output(struct yetty_yvterm_vterm *vterm)
{
    if (!vterm->pty_write_fn) {
        return YETTY_OK_VOID();
    }
    char buf[256];
    size_t got;
    while ((got = vterm_output_read(vterm->grid.vterm, buf, sizeof(buf))) > 0) {
        struct yetty_ycore_void_result wr =
            vterm->pty_write_fn(buf, got, vterm->pty_write_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "vterm_flush_output: pty write");
    }
    return YETTY_OK_VOID();
}

/* Append one Unicode scalar to a buffer as UTF-8. */
static struct yetty_ycore_void_result append_utf8(struct yetty_ycore_buffer *out, uint32_t cp)
{
    uint8_t bytes[4];
    size_t len = 0;
    if (cp < 0x80u) {
        bytes[len++] = (uint8_t)cp;
    } else if (cp < 0x800u) {
        bytes[len++] = (uint8_t)(0xC0u | (cp >> 6));
        bytes[len++] = (uint8_t)(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        bytes[len++] = (uint8_t)(0xE0u | (cp >> 12));
        bytes[len++] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        bytes[len++] = (uint8_t)(0x80u | (cp & 0x3Fu));
    } else {
        bytes[len++] = (uint8_t)(0xF0u | (cp >> 18));
        bytes[len++] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
        bytes[len++] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        bytes[len++] = (uint8_t)(0x80u | (cp & 0x3Fu));
    }
    return yetty_ycore_buffer_write(out, bytes, len);
}

/* Default wire sink: raw terminal bytes outside any envelope feed libvterm. */
static struct yetty_ycore_void_result vterm_wire_default(void *userdata,
                                                         struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yvterm_vterm *vterm = userdata;
    uint8_t buf[4096];
    while (!yetty_ywire_wire_statemachine_at_end(sm)) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, buf, sizeof(buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "vterm_wire_default: read");
        if (rr.value == 0) {
            break;
        }
        vterm_input_write(vterm->grid.vterm, (const char *)buf, rr.value);
        /* libvterm answers terminal queries (cursor-position report, primary
         * device attributes, …) by queueing output during input_write. Flush it
         * back to the PTY right here, inside the loop. This sink runs as a
         * long-lived coroutine whose read() yields for more bytes and never
         * falls out of the loop, so a flush placed after the loop would be dead
         * code. TUIs like fzf emit a query on startup and block until the reply
         * lands; without an immediate flush they look frozen until the next
         * keystroke forces an on_char flush (Ctrl-R not opening the menu). */
        struct yetty_ycore_void_result fr = vterm_flush_output(vterm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "vterm_wire_default: flush output");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm register_wire: from_obj");
    struct yetty_ycore_void_result rr =
        yetty_ywire_wire_statemachine_set_default(sm, vterm_wire_default, vterm_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "yvterm register_wire: set_default");
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
int yetty_yvterm_vterm_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm || codepoint == 0) {
        return 0;
    }
    vterm_keyboard_unichar(vterm->grid.vterm, codepoint, vterm_map_mods(mods));
    struct yetty_ycore_void_result fr = vterm_flush_output(vterm);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
    return 1;
}

[[clang::annotate("expose")]]
int yetty_yvterm_vterm_on_key(struct yetty_yclass_object *obj, int key, int mods)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return 0;
    }
    /* GLFW special-key codes → libvterm keys. Printable keys arrive via
     * on_char and are not handled here. */
    VTermKey vk = VTERM_KEY_NONE;
    switch (key) {
    case 256: vk = VTERM_KEY_ESCAPE; break;
    case 257: vk = VTERM_KEY_ENTER; break;
    case 258: vk = VTERM_KEY_TAB; break;
    case 259: vk = VTERM_KEY_BACKSPACE; break;
    case 260: vk = VTERM_KEY_INS; break;
    case 261: vk = VTERM_KEY_DEL; break;
    case 262: vk = VTERM_KEY_RIGHT; break;
    case 263: vk = VTERM_KEY_LEFT; break;
    case 264: vk = VTERM_KEY_DOWN; break;
    case 265: vk = VTERM_KEY_UP; break;
    case 266: vk = VTERM_KEY_PAGEUP; break;
    case 267: vk = VTERM_KEY_PAGEDOWN; break;
    case 268: vk = VTERM_KEY_HOME; break;
    case 269: vk = VTERM_KEY_END; break;
    default: return 0;
    }
    vterm_keyboard_key(vterm->grid.vterm, vk, vterm_map_mods(mods));
    struct yetty_ycore_void_result fr = vterm_flush_output(vterm);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
    }
    return 1;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_set_selection(struct yetty_yclass_object *obj,
                                                                int active, uint32_t anchor_row,
                                                                uint32_t anchor_col,
                                                                uint32_t head_row, uint32_t head_col)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_selection: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->selection_active = active;
    vterm->selection_anchor_row = anchor_row;
    vterm->selection_anchor_col = anchor_col;
    vterm->selection_head_row = head_row;
    vterm->selection_head_col = head_col;
    return YETTY_OK_VOID();
}

/* Stream selection: codepoints from the anchor cell to the head cell in
 * row-major order, newline between rows. Reads the unified model directly. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_get_selection_text(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm get_selection_text: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    if (!out || !vterm->selection_active) {
        return YETTY_OK_VOID();
    }
    struct yetty_yvterm_grid *grid = &vterm->grid;
    uint32_t start_row = vterm->selection_anchor_row;
    uint32_t start_col = vterm->selection_anchor_col;
    uint32_t end_row = vterm->selection_head_row;
    uint32_t end_col = vterm->selection_head_col;
    if (start_row > end_row || (start_row == end_row && start_col > end_col)) {
        uint32_t tr = start_row, tc = start_col;
        start_row = end_row;
        start_col = end_col;
        end_row = tr;
        end_col = tc;
    }
    for (uint32_t row = start_row; row <= end_row && row < grid->visible_rows; ++row) {
        uint32_t first = (row == start_row) ? start_col : 0u;
        uint32_t last = (row == end_row) ? end_col : (grid->cols ? grid->cols - 1u : 0u);
        for (uint32_t col = first; col <= last && col < grid->cols; ++col) {
            uint32_t cp = cell_at(grid, row, col)->codepoint;
            if (cp) {
                struct yetty_ycore_void_result ar = append_utf8(out, cp);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "yvterm get_selection_text: append");
            }
        }
        if (row != end_row) {
            struct yetty_ycore_void_result nr = yetty_ycore_buffer_write(out, "\n", 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, nr, "yvterm get_selection_text: newline");
        }
    }
    return YETTY_OK_VOID();
}

/* Scrollback is not yet backed by a history ring; the live screen is all there
 * is, so the anchor and floor are both 0 (no wheel-up range). */
[[clang::annotate("expose")]]
uint32_t yetty_yvterm_vterm_get_live_anchor(struct yetty_yclass_object *obj)
{
    (void)obj;
    return 0u;
}

[[clang::annotate("expose")]]
uint32_t yetty_yvterm_vterm_get_scrollback_floor(struct yetty_yclass_object *obj)
{
    (void)obj;
    return 0u;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_set_view_top(struct yetty_yclass_object *obj,
                                                               int active,
                                                               uint32_t view_top_total_idx)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm set_view_top: bad obj");
    }
    vterm->view_top_active = active;
    vterm->view_top_total_idx = view_top_total_idx;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                  float scale, float offset_x,
                                                                  float offset_y)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm set_visual_zoom: bad obj");
    }
    vterm->visual_zoom_scale = scale;
    vterm->visual_zoom_offset_x = offset_x;
    vterm->visual_zoom_offset_y = offset_y;
    return YETTY_OK_VOID();
}

#include "vterm.gen.c"
