/*
 * grid.c — yvterm_new:grid, the unified terminal data model.
 *
 * A collection of lines driven from the libvterm STATE layer. Each line owns
 * its own row of text cells (mirroring the old VTermScreen cell content) plus a
 * vector of references to primitives anchored on that line — simple SDF/MSDF
 * shapes and complex composite figures alike. The grid owns the pools those
 * references point into. The yvterm_new:view figure owns one grid instance and
 * reads it to build the GPU buffers.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vterm.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yvterm-new/grid-api.h>

/* ===========================================================================
 * grid — the yclass data model (the line / cell / primitive structs live in
 * the hand-written grid-api.h).
 * ========================================================================= */

struct [[clang::annotate("class@yvterm_new:grid")]] yetty_yvterm_new_grid {
    /* The line ring. lines[0 .. line_count) back the grid; visible row i maps
     * to lines[(base + i) % visible_rows]. Whole-screen scroll advances base
     * and blanks the rolled-off line — no per-line content move. */
    struct yetty_yvterm_new_line *lines;
    uint32_t line_count;   /* total backing lines (>= visible_rows)           */
    uint32_t visible_rows; /* vterm height                                    */
    uint32_t cols;
    uint32_t base; /* rolling-ring offset                             */

    /* libvterm STATE driver — composes the cells from raw PTY bytes. */
    VTerm *vterm;
    VTermState *state;

    /* Current pen, rebuilt from setpenattr. */
    uint32_t pen_fg, pen_bg;
    uint32_t default_fg, default_bg;
    uint16_t pen_attrs;
    int pen_reverse;

    /* Cursor. */
    uint32_t cursor_row, cursor_col, cursor_visible;

    int has_dirty;
};

/* ===========================================================================
 * Cell / line helpers
 * ========================================================================= */

/* libvterm in this tree always hands back RGB colors (the indexed→rgb
 * conversion was patched out), so packing is a straight 0xRRGGBBAA. */
static inline uint32_t pack_color(VTermColor color)
{
    return (uint32_t)color.red | ((uint32_t)color.green << 8) | ((uint32_t)color.blue << 16) |
           (0xFFu << 24);
}

/* Map a visible row to its ring slot. The callbacks address the grid in
 * visible-row coordinates; the ring mapping lives here so a whole-screen scroll
 * only has to move `base`. base < visible_rows and row < visible_rows, so one
 * conditional subtract replaces an integer divide on the hot path. */
static inline uint32_t ring_slot(const struct yetty_yvterm_new_grid *grid, uint32_t row)
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

static inline struct yetty_yvterm_new_text_cell *cell_at(struct yetty_yvterm_new_grid *grid,
                                                         uint32_t row, uint32_t col)
{
    return &grid->lines[ring_slot(grid, row)].text_cells[col];
}

static void blank_cell(struct yetty_yvterm_new_text_cell *cell, uint32_t fg, uint32_t bg)
{
    cell->glyph_index = 0;
    cell->codepoint = 0;
    cell->fg = fg;
    cell->bg = bg;
    cell->attrs = 0;
    cell->width = 1;
    cell->flags = 0;
}

static void blank_line(struct yetty_yvterm_new_grid *grid, uint32_t row)
{
    for (uint32_t col = 0; col < grid->cols; col++) {
        blank_cell(cell_at(grid, row, col), grid->default_fg, grid->default_bg);
    }
}

static void mark_dirty_row(struct yetty_yvterm_new_grid *grid, uint32_t row)
{
    if (row >= grid->visible_rows) {
        return;
    }
    grid->lines[ring_slot(grid, row)].dirty = 1;
    grid->has_dirty = 1;
}

static void mark_dirty_all(struct yetty_yvterm_new_grid *grid)
{
    for (uint32_t line = 0; line < grid->line_count; line++) {
        grid->lines[line].dirty = 1;
    }
    grid->has_dirty = 1;
}

/* Scroll the screen up by `count` rows the rolling way: the top `count` rows
 * roll off and become the new blank bottom rows. No content moves — blank the
 * rolled-off slots, advance base, mark only those slots dirty. */
static void roll_up(struct yetty_yvterm_new_grid *grid, uint32_t count)
{
    if (grid->visible_rows == 0) {
        return;
    }
    if (count > grid->visible_rows) {
        count = grid->visible_rows;
    }
    for (uint32_t step = 0; step < count; step++) {
        uint32_t slot = grid->base + step;
        if (slot >= grid->visible_rows) {
            slot -= grid->visible_rows;
        }
        struct yetty_yvterm_new_text_cell *cells = grid->lines[slot].text_cells;
        for (uint32_t col = 0; col < grid->cols; col++) {
            blank_cell(&cells[col], grid->default_fg, grid->default_bg);
        }
        grid->lines[slot].dirty = 1;
    }
    grid->base += count;
    if (grid->base >= grid->visible_rows) {
        grid->base -= grid->visible_rows;
    }
    grid->has_dirty = 1;
}

/* ===========================================================================
 * VTermState callbacks — compose the text cells from the state machine. Glyph
 * resolution is deferred to the view: the grid stores the codepoint and leaves
 * glyph_index 0 until the renderer resolves it against a font.
 * ========================================================================= */

YETTY_EXTERNAL_CALLBACK
static int cb_putglyph(VTermGlyphInfo *info, VTermPos pos, void *user)
{
    struct yetty_yvterm_new_grid *grid = user;
    if (pos.row < 0 || pos.col < 0 || (uint32_t)pos.row >= grid->visible_rows ||
        (uint32_t)pos.col >= grid->cols) {
        return 1;
    }

    uint32_t codepoint = (info->chars && info->chars[0]) ? info->chars[0] : 0u;
    uint32_t fg = grid->pen_reverse ? grid->pen_bg : grid->pen_fg;
    uint32_t bg = grid->pen_reverse ? grid->pen_fg : grid->pen_bg;

    struct yetty_yvterm_new_text_cell *cell = cell_at(grid, (uint32_t)pos.row, (uint32_t)pos.col);
    cell->glyph_index = 0; /* resolved by the view */
    cell->codepoint = codepoint;
    cell->fg = fg;
    cell->bg = bg;
    cell->attrs = grid->pen_attrs;
    cell->width = (uint8_t)(info->width >= 2 ? 2 : 1);
    cell->flags = 0;

    /* A double-width glyph owns the next cell; blank it as spillover (width 0)
     * so a stale glyph from a previous frame can't bleed through. */
    if (info->width == 2 && (uint32_t)pos.col + 1 < grid->cols) {
        struct yetty_yvterm_new_text_cell *next =
            cell_at(grid, (uint32_t)pos.row, (uint32_t)pos.col + 1);
        next->glyph_index = 0;
        next->codepoint = 0;
        next->fg = fg;
        next->bg = bg;
        next->attrs = grid->pen_attrs;
        next->width = 0;
        next->flags = 0;
    }

    mark_dirty_row(grid, (uint32_t)pos.row);
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    struct yetty_yvterm_new_grid *grid = user;
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
    struct yetty_yvterm_new_grid *grid = user;
    int row_count = dest.end_row - dest.start_row;
    int col_count = dest.end_col - dest.start_col;
    if (row_count <= 0 || col_count <= 0) {
        return 1;
    }
    size_t span = (size_t)col_count * sizeof(struct yetty_yvterm_new_text_cell);

    /* Row-by-row copy between line buffers; direction matters on vertical
     * overlap. Only the text cells move here — moving the per-cell prim /
     * composite refs is part of the rich-content path, still to come. */
    if (dest.start_row <= src.start_row) {
        for (int row = 0; row < row_count; row++) {
            memmove(cell_at(grid, (uint32_t)(dest.start_row + row), (uint32_t)dest.start_col),
                    cell_at(grid, (uint32_t)(src.start_row + row), (uint32_t)src.start_col), span);
            mark_dirty_row(grid, (uint32_t)(dest.start_row + row));
        }
    } else {
        for (int row = row_count - 1; row >= 0; row--) {
            memmove(cell_at(grid, (uint32_t)(dest.start_row + row), (uint32_t)dest.start_col),
                    cell_at(grid, (uint32_t)(src.start_row + row), (uint32_t)src.start_col), span);
            mark_dirty_row(grid, (uint32_t)(dest.start_row + row));
        }
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_erase(VTermRect rect, int selective, void *user)
{
    struct yetty_yvterm_new_grid *grid = user;
    (void)selective;
    for (int row = rect.start_row; row < rect.end_row; row++) {
        if (row < 0 || (uint32_t)row >= grid->visible_rows) {
            continue;
        }
        for (int col = rect.start_col; col < rect.end_col; col++) {
            if (col < 0 || (uint32_t)col >= grid->cols) {
                continue;
            }
            blank_cell(cell_at(grid, (uint32_t)row, (uint32_t)col), grid->default_fg,
                       grid->default_bg);
        }
        mark_dirty_row(grid, (uint32_t)row);
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_scrollrect(VTermRect rect, int downward, int rightward, void *user)
{
    struct yetty_yvterm_new_grid *grid = user;

    /* The common case — a whole-screen scroll up (newline at the bottom) — rolls
     * the ring in O(1). Scroll regions, reverse index and horizontal scroll fall
     * back to the moverect/erase decomposition. */
    int whole_width = (rect.start_col == 0 && (uint32_t)rect.end_col == grid->cols);
    int whole_height = (rect.start_row == 0 && (uint32_t)rect.end_row == grid->visible_rows);
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
    struct yetty_yvterm_new_grid *grid = user;
    grid->pen_fg = grid->default_fg;
    grid->pen_bg = grid->default_bg;
    grid->pen_attrs = 0;
    grid->pen_reverse = 0;
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_setpenattr(VTermAttr attr, VTermValue *val, void *user)
{
    struct yetty_yvterm_new_grid *grid = user;
    switch (attr) {
    case VTERM_ATTR_BOLD:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_NEW_ATTR_BOLD)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_NEW_ATTR_BOLD);
        break;
    case VTERM_ATTR_UNDERLINE: {
        uint16_t mask = YETTY_YVTERM_NEW_ATTR_UNDERLINE | YETTY_YVTERM_NEW_ATTR_UNDERLINE2;
        grid->pen_attrs =
            (uint16_t)((grid->pen_attrs & ~mask) | (((uint16_t)(val->number & 0x3)) << 1));
        break;
    }
    case VTERM_ATTR_ITALIC:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_NEW_ATTR_ITALIC)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_NEW_ATTR_ITALIC);
        break;
    case VTERM_ATTR_BLINK:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_NEW_ATTR_BLINK)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_NEW_ATTR_BLINK);
        break;
    case VTERM_ATTR_STRIKE:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_NEW_ATTR_STRIKE)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_NEW_ATTR_STRIKE);
        break;
    case VTERM_ATTR_CONCEAL:
        grid->pen_attrs = val->boolean ? (grid->pen_attrs | YETTY_YVTERM_NEW_ATTR_CONCEAL)
                                       : (grid->pen_attrs & ~(uint16_t)YETTY_YVTERM_NEW_ATTR_CONCEAL);
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
    struct yetty_yvterm_new_grid *grid = user;
    if (prop == VTERM_PROP_CURSORVISIBLE) {
        grid->cursor_visible = val->boolean ? 1u : 0u;
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
    /* The grid is reflowed through an explicit resize entry point, not by
     * libvterm here. Honour the signature and clamp nothing. */
    (void)rows;
    (void)cols;
    (void)fields;
    (void)user;
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

/* ===========================================================================
 * Lifecycle / public API
 * ========================================================================= */

static void free_lines(struct yetty_yvterm_new_grid *grid)
{
    if (!grid->lines) {
        return;
    }
    for (uint32_t line = 0; line < grid->line_count; line++) {
        free(grid->lines[line].text_cells);
        free(grid->lines[line].drawable_cells_refs);
        free(grid->lines[line].prims);
        free(grid->lines[line].arena);
    }
    free(grid->lines);
    grid->lines = NULL;
}

struct yetty_yvterm_new_grid_ptr_result yetty_yvterm_new_grid_create(uint32_t cols, uint32_t rows)
{
    if (cols == 0 || rows == 0) {
        return YETTY_ERR(yetty_yvterm_new_grid_ptr, "yetty_yvterm_new_grid_create: invalid size");
    }

    struct yetty_yvterm_new_grid *grid = calloc(1, sizeof(struct yetty_yvterm_new_grid));
    if (!grid) {
        return YETTY_ERR(yetty_yvterm_new_grid_ptr, "yetty_yvterm_new_grid_create: alloc");
    }
    grid->cols = cols;
    grid->visible_rows = rows;
    grid->line_count = rows;

    grid->lines = calloc(rows, sizeof(struct yetty_yvterm_new_line));
    if (!grid->lines) {
        free(grid);
        return YETTY_ERR(yetty_yvterm_new_grid_ptr, "yetty_yvterm_new_grid_create: lines alloc");
    }
    for (uint32_t line = 0; line < rows; line++) {
        grid->lines[line].text_cells = calloc(cols, sizeof(struct yetty_yvterm_new_text_cell));
        grid->lines[line].drawable_cells_refs =
            calloc(cols, sizeof(struct yetty_yvterm_new_drawable_refs));
        if (!grid->lines[line].text_cells || !grid->lines[line].drawable_cells_refs) {
            free_lines(grid);
            free(grid);
            return YETTY_ERR(yetty_yvterm_new_grid_ptr, "yetty_yvterm_new_grid_create: line alloc");
        }
    }

    grid->vterm = vterm_new((int)rows, (int)cols);
    if (!grid->vterm) {
        free_lines(grid);
        free(grid);
        return YETTY_ERR(yetty_yvterm_new_grid_ptr, "yetty_yvterm_new_grid_create: vterm_new");
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
    grid->pen_attrs = 0;
    grid->pen_reverse = 0;
    grid->cursor_visible = 1u;

    for (uint32_t row = 0; row < rows; row++) {
        blank_line(grid, row);
    }
    mark_dirty_all(grid);

    return YETTY_OK(yetty_yvterm_new_grid_ptr, grid);
}

struct yetty_ycore_void_result yetty_yvterm_new_grid_destroy(struct yetty_yvterm_new_grid *grid)
{
    if (!grid) {
        return YETTY_OK_VOID();
    }
    if (grid->vterm) {
        vterm_free(grid->vterm);
    }
    free_lines(grid);
    free(grid);
    return YETTY_OK_VOID();
}

/* Feed raw PTY bytes through the state machine; updates the cells and marks the
 * touched lines dirty. */
struct yetty_ycore_void_result yetty_yvterm_new_grid_feed(struct yetty_yvterm_new_grid *grid,
                                                          const char *bytes, size_t len)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yvterm_new_grid_feed: NULL grid");
    }
    vterm_input_write(grid->vterm, bytes, len);
    return YETTY_OK_VOID();
}

/* Accessors for the renderer. */
uint32_t yetty_yvterm_new_grid_cols(const struct yetty_yvterm_new_grid *grid)
{
    return grid ? grid->cols : 0u;
}

uint32_t yetty_yvterm_new_grid_visible_rows(const struct yetty_yvterm_new_grid *grid)
{
    return grid ? grid->visible_rows : 0u;
}

/* Rolling-ring offset — feed into the shader's root_row uniform so it maps
 * visible row → ring slot the same way the grid does. */
uint32_t yetty_yvterm_new_grid_base(const struct yetty_yvterm_new_grid *grid)
{
    return grid ? grid->base : 0u;
}

struct yetty_yvterm_new_line *yetty_yvterm_new_grid_lines(struct yetty_yvterm_new_grid *grid)
{
    return grid ? grid->lines : NULL;
}

int yetty_yvterm_new_grid_is_dirty(const struct yetty_yvterm_new_grid *grid)
{
    return grid ? grid->has_dirty : 0;
}

/* The renderer calls this once it has uploaded every dirty line, so the grid
 * reports clean again until the next feed/cursor move marks a row. */
void yetty_yvterm_new_grid_clear_dirty(struct yetty_yvterm_new_grid *grid)
{
    if (grid) {
        grid->has_dirty = 0;
    }
}

void yetty_yvterm_new_grid_force_full_dirty(struct yetty_yvterm_new_grid *grid)
{
    if (grid) {
        mark_dirty_all(grid);
    }
}

void yetty_yvterm_new_grid_cursor(const struct yetty_yvterm_new_grid *grid, uint32_t *out_row,
                                  uint32_t *out_col, uint32_t *out_visible)
{
    uint32_t row = grid ? grid->cursor_row : 0u;
    uint32_t col = grid ? grid->cursor_col : 0u;
    uint32_t visible = grid ? grid->cursor_visible : 0u;
    if (out_row) {
        *out_row = row;
    }
    if (out_col) {
        *out_col = col;
    }
    if (out_visible) {
        *out_visible = visible;
    }
}

/* Reflow to a new viewport: reallocate the line ring and resize the vterm
 * state grid. The caller repaints the child via SIGWINCH. */
struct yetty_ycore_void_result yetty_yvterm_new_grid_resize(struct yetty_yvterm_new_grid *grid,
                                                            uint32_t cols, uint32_t rows)
{
    if (!grid || cols == 0 || rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yvterm_new_grid_resize: invalid dimensions");
    }

    /* Build the new ring first so a failure leaves the old grid intact. */
    struct yetty_yvterm_new_line *new_lines = calloc(rows, sizeof(struct yetty_yvterm_new_line));
    if (!new_lines) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yvterm_new_grid_resize: lines alloc");
    }
    for (uint32_t line = 0; line < rows; line++) {
        new_lines[line].text_cells = calloc(cols, sizeof(struct yetty_yvterm_new_text_cell));
        new_lines[line].drawable_cells_refs =
            calloc(cols, sizeof(struct yetty_yvterm_new_drawable_refs));
        if (!new_lines[line].text_cells || !new_lines[line].drawable_cells_refs) {
            for (uint32_t freed = 0; freed <= line; freed++) {
                free(new_lines[freed].text_cells);
                free(new_lines[freed].drawable_cells_refs);
            }
            free(new_lines);
            return YETTY_ERR(yetty_ycore_void, "yetty_yvterm_new_grid_resize: line alloc");
        }
    }

    free_lines(grid); /* frees the old ring using the old line_count */
    grid->lines = new_lines;
    grid->cols = cols;
    grid->visible_rows = rows;
    grid->line_count = rows;
    grid->base = 0;

    for (uint32_t row = 0; row < rows; row++) {
        blank_line(grid, row);
    }
    vterm_set_size(grid->vterm, (int)rows, (int)cols);
    mark_dirty_all(grid);
    return YETTY_OK_VOID();
}
