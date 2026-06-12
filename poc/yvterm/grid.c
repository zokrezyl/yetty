/*
 * poc/yvterm/grid.c — see grid.h.
 *
 * One contiguous cell ring driven from VTermState. Cells are packed into the
 * exact 16-byte layout text-layer.wgsl reads, so the production MSDF/cdb
 * shader renders this probe unchanged.
 */
#include "grid.h"

#include <stdlib.h>
#include <string.h>

#include <vterm.h>

#include <yetty/ycore/util.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>

/* Uniform slots — same set and names text-layer.wgsl expects (namespace
 * "text_grid" is prepended by the binder). */
enum {
    UNIFORM_GRID_SIZE = 0,
    UNIFORM_CELL_SIZE,
    UNIFORM_CURSOR_POS,
    UNIFORM_CURSOR_VISIBLE,
    UNIFORM_CURSOR_SHAPE,
    UNIFORM_SCALE,
    UNIFORM_DEFAULT_FG,
    UNIFORM_DEFAULT_BG,
    UNIFORM_FONT_TYPE,
    UNIFORM_VISUAL_ZOOM_SCALE,
    UNIFORM_VISUAL_ZOOM_OFF,
    UNIFORM_ROOT_ROW,
    UNIFORM_SEL_ACTIVE,
    UNIFORM_SEL_ANCHOR,
    UNIFORM_SEL_HEAD,
    UNIFORM_COUNT,
};

/* Four u32 words per cell — matches sizeof(VTermScreenCell) in this tree and
 * the stride text-layer.wgsl assumes. */
enum { WORDS_PER_CELL = 4 };

struct poc_yvterm_grid {
    struct yetty_yrender_terminal_layer base;

    VTerm *vterm;
    VTermState *state;
    struct yetty_yfont_ms_font *font; /* borrowed */

    uint32_t cols;
    uint32_t rows;
    uint32_t buffer_rows; /* 2 * rows — the double-buffer height */

    /* The line collection: one contiguous (buffer_rows * cols) array of cells,
     * each WORDS_PER_CELL u32. line i of the visible screen lives at buffer row
     * (root_row + i). The GPU storage buffer points straight at this. */
    uint32_t *cells;
    uint32_t root_row; /* visible top within the buffer, in [0, rows] */

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

    /* Dirty span in visible rows, plus the coarse layer dirty bit. */
    uint32_t dirty_min;
    uint32_t dirty_max;
    int has_dirty;

    struct yetty_ycore_buffer shader_code;
    struct yetty_yrender_gpu_resource_set rs;

    /* First error raised inside a state callback (callbacks can't return a
     * Result). Surfaced by poc_yvterm_grid_feed. */
    struct yetty_ycore_void_result pending_error;

    /* Perf counters. */
    uint64_t scroll_fastpath;
    uint64_t scroll_memmove;
    uint64_t cells_uploaded;
    uint32_t dirty_rows_last;
};

/*===========================================================================
 * Cell helpers
 *=========================================================================*/

static inline uint32_t *cell_words(struct poc_yvterm_grid *grid, uint32_t buffer_row, uint32_t col)
{
    return &grid->cells[((size_t)buffer_row * grid->cols + col) * WORDS_PER_CELL];
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

static void blank_buffer_row(struct poc_yvterm_grid *grid, uint32_t buffer_row)
{
    for (uint32_t col = 0; col < grid->cols; col++) {
        pack_cell(cell_words(grid, buffer_row, col), 0u, grid->default_fg, grid->default_bg, 0u);
    }
}

static void mark_dirty_row(struct poc_yvterm_grid *grid, uint32_t visible_row)
{
    if (visible_row >= grid->rows) {
        return;
    }
    if (!grid->has_dirty) {
        grid->dirty_min = visible_row;
        grid->dirty_max = visible_row;
        grid->has_dirty = 1;
    } else {
        if (visible_row < grid->dirty_min) {
            grid->dirty_min = visible_row;
        }
        if (visible_row > grid->dirty_max) {
            grid->dirty_max = visible_row;
        }
    }
    grid->base.dirty = 1;
}

static void mark_dirty_all(struct poc_yvterm_grid *grid)
{
    grid->dirty_min = 0;
    grid->dirty_max = grid->rows ? grid->rows - 1 : 0;
    grid->has_dirty = 1;
    grid->base.dirty = 1;
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
    if (pos.row < 0 || pos.col < 0 || (uint32_t)pos.row >= grid->rows ||
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

    uint32_t buffer_row = grid->root_row + (uint32_t)pos.row;
    pack_cell(cell_words(grid, buffer_row, (uint32_t)pos.col), glyph_index, fg, bg, grid->pen_attrs);

    /* A double-width glyph owns the next cell; blank it so a stale glyph from a
     * previous frame doesn't bleed through. */
    if (info->width == 2 && (uint32_t)pos.col + 1 < grid->cols) {
        pack_cell(cell_words(grid, buffer_row, (uint32_t)pos.col + 1), 0u, fg, bg, grid->pen_attrs);
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

    /* Copy row by row. Direction matters when src/dest overlap vertically. */
    if (dest.start_row <= src.start_row) {
        for (int row = 0; row < row_count; row++) {
            uint32_t *to = cell_words(grid, grid->root_row + (uint32_t)(dest.start_row + row),
                                      (uint32_t)dest.start_col);
            uint32_t *from = cell_words(grid, grid->root_row + (uint32_t)(src.start_row + row),
                                        (uint32_t)src.start_col);
            memmove(to, from, (size_t)col_count * WORDS_PER_CELL * sizeof(uint32_t));
            mark_dirty_row(grid, (uint32_t)(dest.start_row + row));
        }
    } else {
        for (int row = row_count - 1; row >= 0; row--) {
            uint32_t *to = cell_words(grid, grid->root_row + (uint32_t)(dest.start_row + row),
                                      (uint32_t)dest.start_col);
            uint32_t *from = cell_words(grid, grid->root_row + (uint32_t)(src.start_row + row),
                                        (uint32_t)src.start_col);
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
        if (row < 0 || (uint32_t)row >= grid->rows) {
            continue;
        }
        uint32_t buffer_row = grid->root_row + (uint32_t)row;
        for (int col = rect.start_col; col < rect.end_col; col++) {
            if (col < 0 || (uint32_t)col >= grid->cols) {
                continue;
            }
            pack_cell(cell_words(grid, buffer_row, (uint32_t)col), 0u, grid->default_fg,
                      grid->default_bg, 0u);
        }
        mark_dirty_row(grid, (uint32_t)row);
    }
    return 1;
}

/* Whole-screen scroll up by `lines` via the root_row slide — the O(1) path.
 * Only the newly exposed bottom lines are blanked; existing content never
 * moves until the double-buffer wraps. */
static void scroll_whole_up(struct poc_yvterm_grid *grid, uint32_t lines)
{
    if (lines == 0) {
        return;
    }
    if (lines > grid->rows) {
        lines = grid->rows;
    }

    if (grid->root_row + lines + grid->rows <= grid->buffer_rows) {
        grid->root_row += lines;
        for (uint32_t row = 0; row < lines; row++) {
            blank_buffer_row(grid, grid->root_row + grid->rows - lines + row);
        }
    } else {
        /* Wrap: keep the surviving (rows - lines) lines, slide them to the top,
         * reset root_row. One memmove every `rows` scrolls — the amortized cost
         * of the double-buffer scheme. */
        uint32_t survivors = grid->rows - lines;
        memmove(cell_words(grid, 0, 0), cell_words(grid, grid->root_row + lines, 0),
                (size_t)survivors * grid->cols * WORDS_PER_CELL * sizeof(uint32_t));
        grid->root_row = 0;
        for (uint32_t row = 0; row < lines; row++) {
            blank_buffer_row(grid, survivors + row);
        }
    }
    grid->scroll_fastpath++;
    mark_dirty_all(grid);
}

YETTY_EXTERNAL_CALLBACK
static int cb_scrollrect(VTermRect rect, int downward, int rightward, void *user)
{
    struct poc_yvterm_grid *grid = user;

    int whole_width = (rect.start_col == 0 && (uint32_t)rect.end_col == grid->cols);
    int whole_height = (rect.start_row == 0 && (uint32_t)rect.end_row == grid->rows);
    if (rightward == 0 && downward > 0 && whole_width && whole_height) {
        scroll_whole_up(grid, (uint32_t)downward);
        return 1;
    }

    /* Scroll regions, reverse index, horizontal scroll — decompose into the
     * moverect/erase primitives, which memmove content within the buffer. */
    grid->scroll_memmove++;
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

static const VTermStateCallbacks state_callbacks = {
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

/*===========================================================================
 * Resource set — mirrors text-layer.c so the binder generates the bindings
 * text-layer.wgsl references.
 *=========================================================================*/

static void init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
{
    rs->uniform_count = UNIFORM_COUNT;
    rs->uniforms[UNIFORM_GRID_SIZE] =
        (struct yetty_yrender_uniform){"grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[UNIFORM_CELL_SIZE] =
        (struct yetty_yrender_uniform){"cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[UNIFORM_CURSOR_POS] =
        (struct yetty_yrender_uniform){"cursor_pos", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[UNIFORM_CURSOR_VISIBLE] =
        (struct yetty_yrender_uniform){"cursor_visible", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[UNIFORM_CURSOR_SHAPE] =
        (struct yetty_yrender_uniform){"cursor_shape", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[UNIFORM_SCALE] = (struct yetty_yrender_uniform){"scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[UNIFORM_DEFAULT_FG] =
        (struct yetty_yrender_uniform){"default_fg", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[UNIFORM_DEFAULT_BG] =
        (struct yetty_yrender_uniform){"default_bg", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[UNIFORM_FONT_TYPE] =
        (struct yetty_yrender_uniform){"font_type", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[UNIFORM_VISUAL_ZOOM_SCALE] =
        (struct yetty_yrender_uniform){"visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[UNIFORM_VISUAL_ZOOM_OFF] =
        (struct yetty_yrender_uniform){"visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[UNIFORM_ROOT_ROW] =
        (struct yetty_yrender_uniform){"root_row", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[UNIFORM_SEL_ACTIVE] =
        (struct yetty_yrender_uniform){"sel_active", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[UNIFORM_SEL_ANCHOR] =
        (struct yetty_yrender_uniform){"sel_anchor", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[UNIFORM_SEL_HEAD] =
        (struct yetty_yrender_uniform){"sel_head", YETTY_YRENDER_UNIFORM_VEC2};

    rs->uniforms[UNIFORM_SCALE].f32 = 1.0f;
    rs->uniforms[UNIFORM_CURSOR_SHAPE].f32 = 1.0f;
    rs->uniforms[UNIFORM_DEFAULT_FG].u32 = 0x00FFFFFFu;
    rs->uniforms[UNIFORM_DEFAULT_BG].u32 = 0x00000000u;
    rs->uniforms[UNIFORM_VISUAL_ZOOM_SCALE].f32 = 1.0f;
}

/*===========================================================================
 * Layer ops
 *=========================================================================*/

static struct yetty_yrender_gpu_resource_set_result grid_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    struct poc_yvterm_grid *grid =
        container_of((struct yetty_yrender_terminal_layer *)self, struct poc_yvterm_grid, base);

    if (grid->base.dirty) {
        grid->rs.buffers[0].data = (uint8_t *)grid->cells;
        grid->rs.buffers[0].size =
            (size_t)grid->buffer_rows * grid->cols * WORDS_PER_CELL * sizeof(uint32_t);
        grid->rs.buffers[0].dirty = 1;

        grid->rs.uniforms[UNIFORM_GRID_SIZE].vec2[0] = (float)grid->cols;
        grid->rs.uniforms[UNIFORM_GRID_SIZE].vec2[1] = (float)grid->rows;
        grid->rs.uniforms[UNIFORM_CELL_SIZE].vec2[0] = grid->base.cell_size.width;
        grid->rs.uniforms[UNIFORM_CELL_SIZE].vec2[1] = grid->base.cell_size.height;
        grid->rs.uniforms[UNIFORM_ROOT_ROW].u32 = grid->root_row;
        grid->rs.uniforms[UNIFORM_CURSOR_POS].vec2[0] = (float)grid->cursor_col;
        grid->rs.uniforms[UNIFORM_CURSOR_POS].vec2[1] = (float)grid->cursor_row;
        grid->rs.uniforms[UNIFORM_CURSOR_VISIBLE].f32 = grid->cursor_visible ? 1.0f : 0.0f;

        uint32_t span = grid->has_dirty ? (grid->dirty_max - grid->dirty_min + 1u) : 0u;
        grid->dirty_rows_last = span;
        grid->cells_uploaded += (uint64_t)span * grid->cols;

        grid->base.dirty = 0;
        grid->has_dirty = 0;
    }

    /* Attach the font's resource set as the child the binder merges (provides
     * font_sample + the MSDF atlas bindings). */
    struct yetty_yrender_gpu_resource_set_result font_rs =
        grid->font->ops->get_gpu_resource_set(grid->font);
    if (YETTY_IS_ERR(font_rs)) {
        return YETTY_ERR(yetty_yrender_gpu_resource_set, "grid_get_gpu_resource_set: font rs",
                         font_rs);
    }
    grid->rs.children[0] = (struct yetty_yrender_gpu_resource_set *)font_rs.value;
    grid->rs.children_count = 1;

    return YETTY_OK(yetty_yrender_gpu_resource_set, &grid->rs);
}

static struct yetty_ycore_int_result grid_render(struct yetty_yrender_terminal_layer *self,
                                                 struct yetty_ydraw_target *target, int force)
{
    (void)force;
    struct yetty_ycore_void_result rr = target->ops->render_layer(target, self);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rr, "grid_render: render_layer");
    return YETTY_OK(yetty_ycore_int, 1);
}

static int grid_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    const struct poc_yvterm_grid *grid =
        container_of((struct yetty_yrender_terminal_layer *)self, struct poc_yvterm_grid, base);
    if (grid->base.dirty) {
        return 1;
    }
    return grid->font->ops->is_dirty(grid->font);
}

static int grid_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    return 0;
}

static struct yetty_ycore_void_result grid_destroy_op(struct yetty_yrender_terminal_layer *self)
{
    struct poc_yvterm_grid *grid = container_of(self, struct poc_yvterm_grid, base);
    return poc_yvterm_grid_destroy(grid);
}

static const struct yetty_yterminal_layer_ops grid_ops = {
    .destroy = grid_destroy_op,
    .get_gpu_resource_set = grid_get_gpu_resource_set,
    .render = grid_render,
    .is_dirty = grid_is_dirty,
    .is_empty = grid_is_empty,
};

/*===========================================================================
 * Public API
 *=========================================================================*/

struct poc_yvterm_grid_ptr_result poc_yvterm_grid_create(uint32_t cols, uint32_t rows,
                                                         struct yetty_yfont_ms_font *font,
                                                         const char *text_shader_path)
{
    if (cols == 0 || rows == 0 || !font || !text_shader_path) {
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: invalid args");
    }

    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(text_shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: read text-layer.wgsl",
                         shader_res);
    }

    struct poc_yvterm_grid *grid = calloc(1, sizeof(struct poc_yvterm_grid));
    if (!grid) {
        free(shader_res.value.data);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: alloc");
    }
    grid->shader_code = shader_res.value;
    grid->font = font;
    grid->cols = cols;
    grid->rows = rows;
    grid->buffer_rows = rows * 2u;
    grid->pending_error = YETTY_OK_VOID();

    grid->cells = calloc((size_t)grid->buffer_rows * cols * WORDS_PER_CELL, sizeof(uint32_t));
    if (!grid->cells) {
        free(grid->shader_code.data);
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: cells alloc");
    }

    struct pixel_size_result cell = font->ops->get_cell_size(font);
    if (YETTY_IS_ERR(cell)) {
        free(grid->cells);
        free(grid->shader_code.data);
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: font cell size", cell);
    }

    grid->base.ops = &grid_ops;
    grid->base.grid_size.cols = cols;
    grid->base.grid_size.rows = rows;
    grid->base.cell_size.width = cell.value.width;
    grid->base.cell_size.height = cell.value.height;

    /* Preload Basic Latin so the common path doesn't pay a load per glyph. */
    struct yetty_ycore_void_result latin = font->ops->load_basic_latin(font);
    if (YETTY_IS_ERR(latin)) {
        free(grid->cells);
        free(grid->shader_code.data);
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: load_basic_latin", latin);
    }

    grid->vterm = vterm_new((int)rows, (int)cols);
    if (!grid->vterm) {
        free(grid->cells);
        free(grid->shader_code.data);
        free(grid);
        return YETTY_ERR(poc_yvterm_grid_ptr, "poc_yvterm_grid_create: vterm_new");
    }
    vterm_set_utf8(grid->vterm, 1);
    grid->state = vterm_obtain_state(grid->vterm);
    vterm_state_set_callbacks(grid->state, &state_callbacks, grid);
    vterm_state_get_default_colors(grid->state, &grid->default_fg, &grid->default_bg);
    vterm_state_reset(grid->state, 1);
    grid->pen_fg = grid->default_fg;
    grid->pen_bg = grid->default_bg;
    grid->cursor_visible = 1u;

    /* Resource set: same namespace + buffer name + uniform set as text-layer so
     * the binder produces the symbols text-layer.wgsl references. */
    strncpy(grid->rs.namespace, "text_grid", YETTY_YRENDER_NAME_MAX - 1);
    grid->rs.buffer_count = 1;
    strncpy(grid->rs.buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(grid->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    grid->rs.buffers[0].readonly = 1;
    init_uniforms(&grid->rs);
    grid->rs.uniforms[UNIFORM_GRID_SIZE].vec2[0] = (float)cols;
    grid->rs.uniforms[UNIFORM_GRID_SIZE].vec2[1] = (float)rows;
    grid->rs.uniforms[UNIFORM_CELL_SIZE].vec2[0] = grid->base.cell_size.width;
    grid->rs.uniforms[UNIFORM_CELL_SIZE].vec2[1] = grid->base.cell_size.height;
    grid->rs.pixel_size.width = (float)cols * grid->base.cell_size.width;
    grid->rs.pixel_size.height = (float)rows * grid->base.cell_size.height;
    yetty_yrender_shader_code_set(&grid->rs.shader, (const char *)grid->shader_code.data,
                                  grid->shader_code.size);

    /* Start every visible line blank and dirty so the first frame uploads a
     * clean grid. */
    for (uint32_t row = 0; row < rows; row++) {
        blank_buffer_row(grid, grid->root_row + row);
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
    free(grid->cells);
    free(grid->shader_code.data);
    free(grid);
    return YETTY_OK_VOID();
}

struct yetty_yrender_terminal_layer *poc_yvterm_grid_as_layer(struct poc_yvterm_grid *grid)
{
    return grid ? &grid->base : NULL;
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

int poc_yvterm_grid_is_dirty(const struct poc_yvterm_grid *grid)
{
    return grid ? grid_is_dirty(&grid->base) : 0;
}

void poc_yvterm_grid_force_full_dirty(struct poc_yvterm_grid *grid)
{
    if (grid) {
        mark_dirty_all(grid);
    }
}

struct poc_yvterm_grid_stats poc_yvterm_grid_stats_take(struct poc_yvterm_grid *grid)
{
    struct poc_yvterm_grid_stats out = {0};
    if (!grid) {
        return out;
    }
    out.scroll_fastpath = grid->scroll_fastpath;
    out.scroll_memmove = grid->scroll_memmove;
    out.cells_uploaded = grid->cells_uploaded;
    out.dirty_rows_last = grid->dirty_rows_last;
    grid->cells_uploaded = 0;
    return out;
}
