/*
 * poc/yvterm/grid.c — see grid.h.
 *
 * A collection of lines driven from VTermState. Each line owns its own row of
 * cells, packed into the exact 16-byte (4 u32) layout text.wgsl reads. The
 * renderer in main.c uploads dirty lines one at a time into a single pinned
 * GPU buffer, so the storage here is deliberately per-line rather than one
 * contiguous array.
 */
#include "grid.h"

#include <stdlib.h>
#include <string.h>

#include <vterm.h>

#define WORDS_PER_CELL POC_YVTERM_WORDS_PER_CELL

struct poc_yvterm_grid {
    VTerm *vterm;
    VTermState *state;
    struct yetty_yfont_ms_font *font; /* borrowed */

    uint32_t cols;
    uint32_t visible_rows; /* the vterm height — rows the state layer writes */
    uint32_t total_rows;   /* number of line objects backing the GPU buffer */

    /* The line collection. Lines [0, visible_rows) carry the terminal screen;
     * any extra lines up to total_rows exist only so --stress / --rows N can
     * inflate the per-frame upload count. Each line owns its own row. */
    struct poc_line *lines;

    /* Current pen, rebuilt from setpenattr. */
    VTermColor pen_fg;
    VTermColor pen_bg;
    VTermColor default_fg;
    VTermColor default_bg;
    uint32_t pen_attrs; /* packed as the shader reads: bit0 bold, bits1-2 underline, bit6 strike */
    int pen_reverse;

    uint32_t cursor_row;
    uint32_t cursor_col;
    uint32_t cursor_visible;

    int has_dirty;

    /* First error raised inside a state callback (callbacks can't return a
     * Result). Surfaced by poc_yvterm_grid_feed. */
    struct yetty_ycore_void_result pending_error;
};

/*===========================================================================
 * Cell helpers
 *=========================================================================*/

static inline uint32_t *cell_words(struct poc_yvterm_grid *grid, uint32_t row, uint32_t col)
{
    return &grid->lines[row].cells[(size_t)col * WORDS_PER_CELL];
}

static inline void pack_cell(uint32_t *words, uint32_t glyph_index, VTermColor fg, VTermColor bg,
                             uint32_t attrs)
{
    words[0] = glyph_index;
    words[1] = (uint32_t)fg.red | ((uint32_t)fg.green << 8) | ((uint32_t)fg.blue << 16) |
               ((uint32_t)bg.red << 24);
    words[2] = (uint32_t)bg.green | ((uint32_t)bg.blue << 8) | (attrs << 16);
    words[3] = 0u; /* rich_handle — unused in the text-only probe */
}

static void blank_line(struct poc_yvterm_grid *grid, uint32_t row)
{
    for (uint32_t col = 0; col < grid->cols; col++) {
        pack_cell(cell_words(grid, row, col), 0u, grid->default_fg, grid->default_bg, 0u);
    }
}

static void mark_dirty_row(struct poc_yvterm_grid *grid, uint32_t row)
{
    if (row >= grid->visible_rows) {
        return;
    }
    grid->lines[row].dirty = 1;
    grid->has_dirty = 1;
}

static void mark_dirty_all(struct poc_yvterm_grid *grid)
{
    for (uint32_t row = 0; row < grid->total_rows; row++) {
        grid->lines[row].dirty = 1;
    }
    grid->has_dirty = 1;
}

static void stash_callback_error(struct poc_yvterm_grid *grid, struct yetty_ycore_void_result res)
{
    if (YETTY_IS_OK(res)) {
        return;
    }
    if (YETTY_IS_OK(grid->pending_error)) {
        grid->pending_error = res;
    } else {
        yetty_ycore_error_destroy(res.error);
    }
}

/*===========================================================================
 * VTermState callbacks
 *=========================================================================*/

YETTY_EXTERNAL_CALLBACK
static int cb_putglyph(VTermGlyphInfo *info, VTermPos pos, void *user)
{
    struct poc_yvterm_grid *grid = user;
    if (pos.row < 0 || pos.col < 0 || (uint32_t)pos.row >= grid->visible_rows ||
        (uint32_t)pos.col >= grid->cols) {
        return 1;
    }

    uint32_t codepoint = (info->chars && info->chars[0]) ? info->chars[0] : 0u;
    uint32_t glyph_index = 0u;
    if (codepoint) {
        struct uint32_result gi = grid->font->ops->get_glyph_index(grid->font, codepoint);
        if (YETTY_IS_ERR(gi)) {
            yetty_ycore_error_destroy(gi.error);
            /* Not cached yet — load on demand, then retry. */
            struct yetty_ycore_void_result lr =
                grid->font->ops->load_glyphs(grid->font, &codepoint, 1);
            if (YETTY_IS_ERR(lr)) {
                stash_callback_error(grid, lr);
            } else {
                gi = grid->font->ops->get_glyph_index(grid->font, codepoint);
                if (YETTY_IS_OK(gi)) {
                    glyph_index = gi.value;
                } else {
                    yetty_ycore_error_destroy(gi.error);
                }
            }
        } else {
            glyph_index = gi.value;
        }
    }

    VTermColor fg = grid->pen_reverse ? grid->pen_bg : grid->pen_fg;
    VTermColor bg = grid->pen_reverse ? grid->pen_fg : grid->pen_bg;

    pack_cell(cell_words(grid, (uint32_t)pos.row, (uint32_t)pos.col), glyph_index, fg, bg,
              grid->pen_attrs);

    /* A double-width glyph owns the next cell; blank it so a stale glyph from a
     * previous frame doesn't bleed through. */
    if (info->width == 2 && (uint32_t)pos.col + 1 < grid->cols) {
        pack_cell(cell_words(grid, (uint32_t)pos.row, (uint32_t)pos.col + 1), 0u, fg, bg,
                  grid->pen_attrs);
    }

    mark_dirty_row(grid, (uint32_t)pos.row);
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    struct poc_yvterm_grid *grid = user;
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
    struct poc_yvterm_grid *grid = user;
    int row_count = dest.end_row - dest.start_row;
    int col_count = dest.end_col - dest.start_col;
    if (row_count <= 0 || col_count <= 0) {
        return 1;
    }

    /* Copy row by row between line buffers. Direction matters when src/dest
     * overlap vertically. */
    if (dest.start_row <= src.start_row) {
        for (int row = 0; row < row_count; row++) {
            uint32_t *to =
                cell_words(grid, (uint32_t)(dest.start_row + row), (uint32_t)dest.start_col);
            uint32_t *from =
                cell_words(grid, (uint32_t)(src.start_row + row), (uint32_t)src.start_col);
            memmove(to, from, (size_t)col_count * WORDS_PER_CELL * sizeof(uint32_t));
            mark_dirty_row(grid, (uint32_t)(dest.start_row + row));
        }
    } else {
        for (int row = row_count - 1; row >= 0; row--) {
            uint32_t *to =
                cell_words(grid, (uint32_t)(dest.start_row + row), (uint32_t)dest.start_col);
            uint32_t *from =
                cell_words(grid, (uint32_t)(src.start_row + row), (uint32_t)src.start_col);
            memmove(to, from, (size_t)col_count * WORDS_PER_CELL * sizeof(uint32_t));
            mark_dirty_row(grid, (uint32_t)(dest.start_row + row));
        }
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_erase(VTermRect rect, int selective, void *user)
{
    struct poc_yvterm_grid *grid = user;
    (void)selective;
    for (int row = rect.start_row; row < rect.end_row; row++) {
        if (row < 0 || (uint32_t)row >= grid->visible_rows) {
            continue;
        }
        for (int col = rect.start_col; col < rect.end_col; col++) {
            if (col < 0 || (uint32_t)col >= grid->cols) {
                continue;
            }
            pack_cell(cell_words(grid, (uint32_t)row, (uint32_t)col), 0u, grid->default_fg,
                      grid->default_bg, 0u);
        }
        mark_dirty_row(grid, (uint32_t)row);
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_scrollrect(VTermRect rect, int downward, int rightward, void *user)
{
    struct poc_yvterm_grid *grid = user;
    /* Decompose every scroll into the moverect/erase primitives, which move
     * content between line buffers. With per-line storage there is no O(1)
     * root_row slide; root_row stays 0. */
    vterm_scroll_rect(rect, downward, rightward, cb_moverect, cb_erase, grid);
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_initpen(void *user)
{
    struct poc_yvterm_grid *grid = user;
    grid->pen_fg = grid->default_fg;
    grid->pen_bg = grid->default_bg;
    grid->pen_attrs = 0u;
    grid->pen_reverse = 0;
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_setpenattr(VTermAttr attr, VTermValue *val, void *user)
{
    struct poc_yvterm_grid *grid = user;
    switch (attr) {
    case VTERM_ATTR_BOLD:
        if (val->boolean) {
            grid->pen_attrs |= 0x1u;
        } else {
            grid->pen_attrs &= ~0x1u;
        }
        break;
    case VTERM_ATTR_UNDERLINE:
        grid->pen_attrs &= ~(0x3u << 1);
        grid->pen_attrs |= ((uint32_t)(val->number & 0x3) << 1);
        break;
    case VTERM_ATTR_STRIKE:
        if (val->boolean) {
            grid->pen_attrs |= (0x1u << 6);
        } else {
            grid->pen_attrs &= ~(0x1u << 6);
        }
        break;
    case VTERM_ATTR_REVERSE:
        grid->pen_reverse = val->boolean ? 1 : 0;
        break;
    case VTERM_ATTR_FOREGROUND:
        grid->pen_fg = val->color;
        break;
    case VTERM_ATTR_BACKGROUND:
        grid->pen_bg = val->color;
        break;
    default:
        break;
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int cb_settermprop(VTermProp prop, VTermValue *val, void *user)
{
    (void)prop;
    (void)val;
    (void)user;
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
    /* The probe creates the grid at a fixed size and does not drive PTY
     * resize through libvterm, so this is a no-op beyond honoring the cursor
     * clamp libvterm expects. */
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

/*===========================================================================
 * Public API
 *=========================================================================*/

static void free_lines(struct poc_yvterm_grid *grid)
{
    if (!grid->lines) {
        return;
    }
    for (uint32_t row = 0; row < grid->total_rows; row++) {
        free(grid->lines[row].cells);
    }
    free(grid->lines);
    grid->lines = NULL;
}

struct poc_yvterm_grid_ptr_result poc_yvterm_grid_create(uint32_t cols, uint32_t visible_rows,
                                                         uint32_t total_rows,
                                                         struct yetty_yfont_ms_font *font)
{
    if (cols == 0 || visible_rows == 0 || total_rows < visible_rows || !font) {
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: invalid args");
    }

    struct poc_yvterm_grid *grid = calloc(1, sizeof(struct poc_yvterm_grid));
    if (!grid) {
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: alloc");
    }
    grid->font = font;
    grid->cols = cols;
    grid->visible_rows = visible_rows;
    grid->total_rows = total_rows;
    grid->pending_error = YETTY_OK_VOID();

    grid->lines = calloc(total_rows, sizeof(struct poc_line));
    if (!grid->lines) {
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: lines alloc");
    }
    for (uint32_t row = 0; row < total_rows; row++) {
        grid->lines[row].cells = calloc((size_t)cols * WORDS_PER_CELL, sizeof(uint32_t));
        if (!grid->lines[row].cells) {
            free_lines(grid);
            free(grid);
            return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: line cells alloc");
        }
    }

    /* Preload Basic Latin so the common path doesn't pay a load per glyph. */
    struct yetty_ycore_void_result latin = font->ops->load_basic_latin(font);
    if (YETTY_IS_ERR(latin)) {
        free_lines(grid);
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: load_basic_latin", latin);
    }

    grid->vterm = vterm_new((int)visible_rows, (int)cols);
    if (!grid->vterm) {
        free_lines(grid);
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: vterm_new");
    }
    vterm_set_utf8(grid->vterm, 1);
    grid->state = vterm_obtain_state(grid->vterm);
    vterm_state_set_callbacks(grid->state, grid_state_callbacks(), grid);
    vterm_state_get_default_colors(grid->state, &grid->default_fg, &grid->default_bg);
    vterm_state_reset(grid->state, 1);
    grid->pen_fg = grid->default_fg;
    grid->pen_bg = grid->default_bg;
    grid->cursor_visible = 1u;

    /* Start every line blank and dirty so the first frame uploads a clean
     * grid. */
    for (uint32_t row = 0; row < total_rows; row++) {
        blank_line(grid, row);
    }
    mark_dirty_all(grid);

    return YETTY_OK(poc_yvterm_grid_ptr, grid);
}

struct yetty_ycore_void_result poc_yvterm_grid_destroy(struct poc_yvterm_grid *grid)
{
    if (!grid) {
        return YETTY_OK_VOID();
    }
    if (YETTY_IS_ERR(grid->pending_error)) {
        yetty_ycore_error_destroy(grid->pending_error.error);
    }
    if (grid->vterm) {
        vterm_free(grid->vterm);
    }
    free_lines(grid);
    free(grid);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result poc_yvterm_grid_feed(struct poc_yvterm_grid *grid, const char *bytes,
                                                    size_t len)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "poc_yvterm_grid_feed: NULL grid");
    }
    vterm_input_write(grid->vterm, bytes, len);
    if (YETTY_IS_ERR(grid->pending_error)) {
        struct yetty_ycore_void_result err = grid->pending_error;
        grid->pending_error = YETTY_OK_VOID();
        return err;
    }
    return YETTY_OK_VOID();
}

uint32_t poc_yvterm_grid_cols(const struct poc_yvterm_grid *grid)
{
    return grid ? grid->cols : 0u;
}

uint32_t poc_yvterm_grid_visible_rows(const struct poc_yvterm_grid *grid)
{
    return grid ? grid->visible_rows : 0u;
}

uint32_t poc_yvterm_grid_total_rows(const struct poc_yvterm_grid *grid)
{
    return grid ? grid->total_rows : 0u;
}

struct poc_line *poc_yvterm_grid_lines(struct poc_yvterm_grid *grid)
{
    return grid ? grid->lines : NULL;
}

int poc_yvterm_grid_is_dirty(const struct poc_yvterm_grid *grid)
{
    if (!grid) {
        return 0;
    }
    if (grid->has_dirty) {
        return 1;
    }
    return grid->font->ops->is_dirty(grid->font);
}

void poc_yvterm_grid_force_full_dirty(struct poc_yvterm_grid *grid)
{
    if (grid) {
        mark_dirty_all(grid);
    }
}
