#include <yetty/yframework/yframework.h>
#include <yetty/yvterm/text-layer.h>
#include <yetty/yvterm/text-scrollback.h>
#include <yetty/yvterm/shader-glyph-figure.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/ms-raster-font.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yfont/shader-glyph.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ydraw/cell-ref-table.h>
#include <yetty/yfigure/methods.h>
#include <yetty/yvterm/ydraw-content.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/ytrace/ytrace.h>
#include <vterm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Uniform positions */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_CURSOR_POS 2
#define U_CURSOR_VISIBLE 3
#define U_CURSOR_SHAPE 4
#define U_SCALE 5
#define U_DEFAULT_FG 6
#define U_DEFAULT_BG 7
#define U_FONT_TYPE 8
/* Visual zoom state pushed in from yetty.c. Applied to the incoming pixel
 * position at the start of fs_main — so cell lookup, glyph sampling, and
 * MSDF/SDF math all evaluate at the *transformed* coordinate. That is the
 * only way to zoom without turning the composite into a bitmap blur. */
#define U_VZ_SCALE 9
#define U_VZ_OFF 10
/* Row offset within the libvterm 2*rows-tall cell buffer that marks the top
 * of the visible screen. Bumped by libvterm on full-screen scroll-up; the
 * shader applies it when computing cell_index so the live window appears in
 * the correct place. */
#define U_ROOT_ROW 11
/* Selection highlight — xterm-style stream selection.
 *   sel_active   — 0 disables; otherwise read the rest
 *   sel_anchor   — (row, col) cell where the user clicked
 *   sel_head     — (row, col) cell the cursor sits on now
 * The shader sorts anchor/head into start/end (reading order) and tints
 * the cells between them. col runs [0, cols]; col==cols means past the
 * EOL on that row (drag past the right edge → "to end of line"). */
#define U_SEL_ACTIVE 12
#define U_SEL_ANCHOR 13
#define U_SEL_HEAD 14
#define U_COUNT 15

/* Setters */
static inline void set_grid_size(struct yetty_yrender_gpu_resource_set *rs, float cols, float rows)
{
    rs->uniforms[U_GRID_SIZE].vec2[0] = cols;
    rs->uniforms[U_GRID_SIZE].vec2[1] = rows;
}
static inline void set_cell_size(struct yetty_yrender_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_CELL_SIZE].vec2[0] = w;
    rs->uniforms[U_CELL_SIZE].vec2[1] = h;
}
static inline void set_cursor_pos(struct yetty_yrender_gpu_resource_set *rs, float col, float row)
{
    rs->uniforms[U_CURSOR_POS].vec2[0] = col;
    rs->uniforms[U_CURSOR_POS].vec2[1] = row;
}
static inline void set_cursor_visible(struct yetty_yrender_gpu_resource_set *rs, float v)
{
    rs->uniforms[U_CURSOR_VISIBLE].f32 = v;
}
static inline void set_cursor_shape(struct yetty_yrender_gpu_resource_set *rs, float s)
{
    rs->uniforms[U_CURSOR_SHAPE].f32 = s;
}
static inline void set_scale(struct yetty_yrender_gpu_resource_set *rs, float s)
{
    rs->uniforms[U_SCALE].f32 = s;
}
static inline void set_default_fg(struct yetty_yrender_gpu_resource_set *rs, uint32_t c)
{
    rs->uniforms[U_DEFAULT_FG].u32 = c;
}
static inline void set_default_bg(struct yetty_yrender_gpu_resource_set *rs, uint32_t c)
{
    rs->uniforms[U_DEFAULT_BG].u32 = c;
}
static inline void set_visual_zoom(struct yetty_yrender_gpu_resource_set *rs, float scale,
                                   float off_x, float off_y)
{
    rs->uniforms[U_VZ_SCALE].f32 = scale;
    rs->uniforms[U_VZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_VZ_OFF].vec2[1] = off_y;
}
static inline void set_root_row(struct yetty_yrender_gpu_resource_set *rs, uint32_t r)
{
    rs->uniforms[U_ROOT_ROW].u32 = r;
}
static inline void set_selection_state(struct yetty_yrender_gpu_resource_set *rs, int active,
                                       uint32_t anchor_row, uint32_t anchor_col, uint32_t head_row,
                                       uint32_t head_col)
{
    rs->uniforms[U_SEL_ACTIVE].u32 = active ? 1u : 0u;
    rs->uniforms[U_SEL_ANCHOR].vec2[0] = (float)anchor_row;
    rs->uniforms[U_SEL_ANCHOR].vec2[1] = (float)anchor_col;
    rs->uniforms[U_SEL_HEAD].vec2[0] = (float)head_row;
    rs->uniforms[U_SEL_HEAD].vec2[1] = (float)head_col;
}

/* Init — names and types use the same constants */
static void init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;

    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CURSOR_POS] =
        (struct yetty_yrender_uniform){"cursor_pos", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CURSOR_VISIBLE] =
        (struct yetty_yrender_uniform){"cursor_visible", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_CURSOR_SHAPE] =
        (struct yetty_yrender_uniform){"cursor_shape", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_SCALE] = (struct yetty_yrender_uniform){"scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_DEFAULT_FG] =
        (struct yetty_yrender_uniform){"default_fg", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_DEFAULT_BG] =
        (struct yetty_yrender_uniform){"default_bg", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_FONT_TYPE] =
        (struct yetty_yrender_uniform){"font_type", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_ROOT_ROW] =
        (struct yetty_yrender_uniform){"root_row", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_SEL_ACTIVE] =
        (struct yetty_yrender_uniform){"sel_active", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_SEL_ANCHOR] =
        (struct yetty_yrender_uniform){"sel_anchor", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_SEL_HEAD] =
        (struct yetty_yrender_uniform){"sel_head", YETTY_YRENDER_UNIFORM_VEC2};

    set_scale(rs, 1.0f);
    set_cursor_shape(rs, 1.0f);
    set_default_fg(rs, 0x00FFFFFFu);
    set_default_bg(rs, 0x00000000u);
    set_visual_zoom(rs, 1.0f, 0.0f, 0.0f);
    set_selection_state(rs, 0, 0, 0, 0, 0);
}

/* Text layer - embeds base as first member */
struct yetty_yvterm_text_layer {
    struct yetty_yrender_terminal_layer base;
    VTerm *vterm;
    VTermScreen *screen;
    struct yetty_yfont_ms_font *font;
    uint32_t font_type; /* 0=msdf, 6=raster */
    struct yetty_ycore_buffer shader_code;
    struct yetty_yrender_gpu_resource_set rs;
    struct yetty_ycore_void_result pending_error; /* Error from vterm callbacks */
    /* DEC mode 1500/1501 — mirrored from libvterm via settermprop. The
     * terminal reads these (via base.mouse_sub_fn) to decide whether to
     * forward GLFW mouse events as OSC 777777/777778. */
    int mouse_click_subscribed;
    int mouse_move_subscribed;

    /* Scrollback buffer. Two-ring arena (cells + line descriptors) — see
     * struct yetty_yvterm_text_sb_arena above. Logical position 0 is the
     * oldest live line; pop reads the newest. */
    struct yetty_yvterm_text_sb_arena sb;

    /* Per-cell rich-handle table. Each VTermScreenCell.rich_handle indexes
     * this; the ydraw canvas reads/stamps handles through a cell_source we
     * hand it. Slots are reclaimed by a mark-sweep GC over the cell buffer. */
    struct yetty_ydraw_cell_ref_table cell_ref_table;
    /* Altscreen active? Handles only ever live on the primary buffer, so the
     * GC (which scans the active buffer) is suppressed while the altscreen is
     * shown — otherwise it would reap the primary's still-referenced handles. */
    int alt_active;

    /* Shader-glyph figure — animated procedural glyphs at PUA-B cells. It
     * reads this layer's cell buffer, so the text-layer owns its creation +
     * management (resize / visual-zoom). The terminal attaches it to the root
     * container, which renders it and owns its destroy (cascade). */
    struct yetty_yvterm_shader_glyph_figure *shader_glyph_figure;

    /* Scrollback view (tmux-style copy mode). When active, the GPU buffer
     * is built by stitching sb_lines + live screen so the user sees a
     * frozen historical viewport whose top is anchored at view_top_total_idx
     * (an absolute line index where 0..sb_count-1 are scrollback and
     * sb_count..sb_count+rows-1 are live). The cursor is hidden in this
     * mode — it's a live-screen artifact and would mislead the reader. */
    int view_active;
    uint32_t view_top_total_idx;
    /* Synthetic VTermScreenCell array used as the GPU buffer when view is
     * active. Sized cols*rows; reallocated on resize. */
    VTermScreenCell *view_staging;
    size_t view_staging_capacity;
    /* Latest cursor visibility reported by vterm. We track this separately
     * because while view_active=1 the GPU uniform is forced to 0; on exit
     * we restore from this so the cursor reappears at whatever state vterm
     * settled on while we were in scrollback view. */
    float vterm_cursor_visible;

    /* Selection — xterm-style cell-stream. Both endpoints are stored as
     * (row, col) and we sort to (start, end) in reading order at extraction
     * time. col==grid_size.cols means "past EOL" (drag past the right edge,
     * which selects to end of line). */
    int sel_active;
    uint32_t sel_anchor_row, sel_anchor_col;
    uint32_t sel_head_row, sel_head_col;
};

/* Forward declarations */
static struct yetty_yclass_object *text_layer_sgf_object(
    struct yetty_yvterm_text_layer *text_layer);
static struct yetty_ycore_void_result text_layer_destroy(struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result text_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);
static struct yetty_yrender_gpu_resource_set_result text_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static int text_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int text_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                              int mods);
static struct yetty_ycore_int_result text_layer_render(struct yetty_yrender_terminal_layer *self,
                                                       struct yetty_ydraw_target *target,
                                                       int force);
static uint32_t text_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self);
static uint32_t text_layer_get_scrollback_floor(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result text_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx);
static void text_layer_build_view(struct yetty_yvterm_text_layer *layer);
static struct yetty_ycore_void_result text_layer_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out);
static struct yetty_ycore_void_result text_layer_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col);

/* VTerm callbacks */
static int on_damage(VTermRect rect, void *user);
static int on_move_cursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
static int on_sb_pushline(int cols, const VTermScreenCell *cells, void *user);
static int on_sb_popline(int cols, VTermScreenCell *cells, void *user);
static int on_settermprop(VTermProp prop, VTermValue *val, void *user);
static int on_erase(VTermRect rect, int selective, void *user);

/* Glyph resolver — called by vterm for every codepoint. Signature
 * dictated by libvterm (VTermResolveGlyphFunc, returns VTermResolvedGlyph),
 * so errors from upcalls are logged and the error chain destroyed at this
 * boundary. */
YETTY_EXTERNAL_CALLBACK
static VTermResolvedGlyph resolve_glyph(const uint32_t *chars, int count, int bold, int italic,
                                        void *user)
{
    struct yetty_yvterm_text_layer *text_layer = user;
    VTermResolvedGlyph result = {0, 0};

    ydebug("resolve_glyph ENTER: count=%d cp=U+%04X bold=%d italic=%d", count,
           count > 0 ? chars[0] : 0u, bold, italic);

    if (count == 0) {
        ydebug("resolve_glyph EXIT early: count=0");
        return result;
    }

    /* PUA → shader-glyph route. The shader-glyph "font" lives in the top
     * half of the u32 glyph_index space and is consumed by shader-glyph
     * layer instead of the text-layer's font. No bit-pattern reservation:
     * IDs are allocated from UINT32_MAX downward.
     *
     * Two gates: codepoint must be in the configured PUA window AND a
     * shader file must actually exist for that codepoint. Nerd Fonts'
     * Material Design Icons live in U+F0001..U+F1AFE — they overlap the
     * window but should still render via the regular font atlas, not as
     * (non-existent) shader glyphs. Without the existence check, those
     * cells got the bit-31 marker, pinned the shader-glyph layer's
     * is_empty() at false, and ran the 60 Hz animation timer forever. */
    if (yetty_shader_glyph_codepoint_in_range(chars[0]) &&
        yetty_yfont_shader_glyph_codepoint_exists(chars[0])) {
        result.glyph_index = yetty_shader_glyph_id_from_codepoint(chars[0]);
        result.font_type = 0;
        ydebug("resolve_glyph SHADER: U+%04X -> glyph_index=0x%08X", chars[0], result.glyph_index);
        return result;
    }

    if (!text_layer->font || !text_layer->font->ops) {
        ydebug("resolve_glyph EXIT early: font=%p", (void *)text_layer->font);
        return result;
    }

    enum yetty_yfont_ms_style style = YETTY_YFONT_MS_STYLE_REGULAR;
    if (bold && italic) {
        style = YETTY_YFONT_MS_STYLE_BOLD_ITALIC;
    } else if (bold) {
        style = YETTY_YFONT_MS_STYLE_BOLD;
    } else if (italic) {
        style = YETTY_YFONT_MS_STYLE_ITALIC;
    }

    struct uint32_result glyph_res =
        text_layer->font->ops->get_glyph_index_styled(text_layer->font, chars[0], style);
    if (YETTY_IS_OK(glyph_res)) {
        result.glyph_index = glyph_res.value;
    } else {
        yerror("resolve_glyph: get_glyph_index_styled ERR for U+%04X: %s", chars[0],
               glyph_res.error.msg);
        yetty_ycore_error_destroy(glyph_res.error);
    }
    result.font_type = 0;

    ydebug("resolve_glyph EXIT: U+%04X -> glyph_index=%u", chars[0], result.glyph_index);
    return result;
}

/* Text layer always has content */
static int text_layer_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    return 0;
}

/* Mirrors the dirty check inside text_layer_render — single-pass over the
 * whole grid, so the base dirty bit is the only state that matters.
 *
 * This is a terminal-layer vtable slot (`.is_dirty`) with an int-returning
 * signature dictated by <yetty/yterminal/terminal.h>; it cannot return a
 * Result, so the figure-dirty query's Result is absorbed here. */
YETTY_EXTERNAL_CALLBACK
static int text_layer_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    if (self->dirty) {
        return 1;
    }
    /* The owned shader-glyph figure animates independently — surface its dirty
     * so the frame renders us (and thus it). */
    struct yetty_yclass_object *sgf_obj = text_layer_sgf_object(container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_text_layer, base));
    return sgf_obj && yetty_yfigure_figure_dirty_get(sgf_obj).value;
}

/* Append a synthetic blank scrollback line — used when ydraw asks to
 * scroll more lines than the live screen contains. Without this we'd
 * desync from ydraw's rolling_row_0 (which counts every row of unified
 * scroll history, including space the text screen never had content in). */
static void push_blank_sb_line(struct yetty_yvterm_text_layer *layer, int cols)
{
    if (cols <= 0) {
        return;
    }
    enum { YETTY_YVTERM_STACK_BLANK_CELLS = 4096 };
    if (cols <= YETTY_YVTERM_STACK_BLANK_CELLS) {
        VTermScreenCell stack_blanks[YETTY_YVTERM_STACK_BLANK_CELLS] = {0};
        yetty_yvterm_text_sb_arena_push(&layer->sb, stack_blanks, cols);
    } else {
        VTermScreenCell *blanks = calloc((size_t)cols, sizeof(VTermScreenCell));
        if (!blanks) {
            yerror("push_blank_sb_line: calloc failed (cols=%d)", cols);
            return;
        }
        yetty_yvterm_text_sb_arena_push(&layer->sb, blanks, cols);
        free(blanks);
    }
}

/* Receive scroll from other layers (e.g., ydraw).
 *
 * libvterm's vterm_scroll_rect() takes a fast path when |downward| >= rows:
 * it just erases the screen and skips the per-row moverect machinery, so
 * sb_pushline never fires. That fast path desyncs us from ydraw's
 * rolling_row_0 — every row of unified scroll history must add exactly one
 * entry to sb_lines, even when the text screen had no content for it.
 *
 * Solution: cap the chunk vterm sees at rows-1 (so it always takes the slow,
 * sb-pushing path), and once vterm has emptied the screen, append blank
 * entries directly for the remainder. */
static struct yetty_ycore_void_result text_layer_scroll(struct yetty_yrender_terminal_layer *self,
                                                        int lines)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    ydebug("text_layer_scroll ENTER: lines=%d screen=%p", lines, (void *)text_layer->screen);

    if (!text_layer->screen) {
        return YETTY_ERR(yetty_ycore_void, "screen is NULL");
    }
    if (lines <= 0) {
        return YETTY_OK_VOID();
    }

    int rows = (int)text_layer->base.grid_size.rows;
    int cols = (int)text_layer->base.grid_size.cols;
    int chunk_max = rows > 1 ? rows - 1 : 1;

    /* Two chunks through vterm are enough to push every row of original
     * screen content into scrollback (chunk_max=rows-1 then 1). After that
     * the screen is all-blank, so further vterm_screen_scroll_lines calls
     * would push blank rows — append them directly instead, much cheaper
     * than driving libvterm 200+ times for an empty screen. */
    int via_vterm = lines < 2 * rows ? lines : 2 * rows;
    int remaining = via_vterm;
    while (remaining > 0) {
        int chunk = remaining > chunk_max ? chunk_max : remaining;
        vterm_screen_scroll_lines(text_layer->screen, chunk);
        remaining -= chunk;
    }

    int blank_lines = lines - via_vterm;
    for (int i = 0; i < blank_lines; i++) {
        push_blank_sb_line(text_layer, cols);
    }

    text_layer->base.dirty = 1;

    ydebug("text_layer_scroll EXIT: lines=%d (via_vterm=%d blanks=%d) "
           "sb_count=%u",
           lines, via_vterm, blank_lines, text_layer->sb.lines_count);
    return YETTY_OK_VOID();
}

/* Receive cursor position from other layers (e.g., ydraw) */
static struct yetty_ycore_void_result text_layer_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    ydebug("text_layer_set_cursor ENTER: col=%d row=%d screen=%p", col, row,
           (void *)text_layer->screen);

    if (!text_layer->screen) {
        return YETTY_ERR(yetty_ycore_void, "text_layer_set_cursor: NULL screen");
    }

    VTermPos pos = {.row = row, .col = col};
    vterm_screen_set_cursorpos(text_layer->screen, pos);
    text_layer->base.dirty = 1;

    ydebug("text_layer_set_cursor EXIT: col=%d row=%d set", col, row);
    return YETTY_OK_VOID();
}

/* Shared inner that updates the layer's cell stride. resize_grid below
 * combines this with a grid-size update so callers can't forget either. */
static struct yetty_ycore_void_result text_layer_apply_cell_size(
    struct yetty_yvterm_text_layer *text_layer, struct yetty_ycore_pixel_size cell_size)
{
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell size");
    }

    /* Ask the font to re-rasterize (raster) or update its requested render
     * size (MSDF) FIRST, so get_cell_size() reports something useful if we
     * later want to snap to the font's natural cell. */
    if (text_layer->font && text_layer->font->ops && text_layer->font->ops->set_cell_size) {
        struct yetty_ycore_void_result r =
            text_layer->font->ops->set_cell_size(text_layer->font, cell_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "text_layer_apply_cell_size: font set_cell_size failed");
    }

    text_layer->base.cell_size = cell_size;
    /* Push to the GPU uniform the shader actually reads. */
    set_cell_size(&text_layer->rs, cell_size.width, cell_size.height);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result text_layer_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);
    set_visual_zoom(&text_layer->rs, scale, off_x, off_y);

    /* Mirror the zoom onto the owned shader-glyph figure (outside layers[]). */
    if (text_layer->shader_glyph_figure) {
        struct yetty_ycore_void_result vr = yetty_yvterm_shader_glyph_figure_set_visual_zoom(
            text_layer->shader_glyph_figure, scale, off_x, off_y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, vr,
                            "text_layer_set_visual_zoom: shader_glyph figure zoom");
    }

    self->dirty = 1;
    return YETTY_OK_VOID();
}

/* Ops */
static const struct yetty_yterminal_layer_ops text_layer_ops = {
    .destroy = text_layer_destroy,
    .resize_grid = text_layer_resize_grid,
    .set_visual_zoom = text_layer_set_visual_zoom,
    .get_gpu_resource_set = text_layer_get_gpu_resource_set,
    .render = text_layer_render,
    .is_dirty = text_layer_is_dirty,
    .is_empty = text_layer_is_empty,
    .on_key = text_layer_on_key,
    .on_char = text_layer_on_char,
    .scroll = text_layer_scroll,
    .set_cursor = text_layer_set_cursor,
    .get_live_anchor = text_layer_get_live_anchor,
    .get_scrollback_floor = text_layer_get_scrollback_floor,
    .set_view_top = text_layer_set_view_top,
    .set_selection = text_layer_set_selection,
    .get_selection_text = text_layer_get_selection_text,
};

/* VTerm screen callbacks */
static VTermScreenCallbacks screen_callbacks = {
    .damage = on_damage,
    .moverect = NULL,
    .movecursor = on_move_cursor,
    .settermprop = on_settermprop,
    .bell = NULL,
    .resize = NULL,
    .sb_pushline = on_sb_pushline,
    .sb_popline = on_sb_popline,
    .sb_clear = NULL,
    .erase = on_erase,
};

/* libvterm settermprop callback. Signature dictated by libvterm
 * (`VTermStateFallbacks.settermprop`) — int return, no out-params for
 * Result. Errors from upcalls into Result-returning typedefs are stashed
 * in `pending_error` and surfaced by the next layer op that runs. */
YETTY_EXTERNAL_CALLBACK
static int on_settermprop(VTermProp prop, VTermValue *val, void *user)
{
    struct yetty_yvterm_text_layer *layer = user;
    if (!layer || !val) {
        return 1;
    }

    int changed = 0;
    if (prop == VTERM_PROP_CARDCLICK) {
        int v = val->boolean ? 1 : 0;
        if (v != layer->mouse_click_subscribed) {
            layer->mouse_click_subscribed = v;
            changed = 1;
        }
    } else if (prop == VTERM_PROP_CARDMOVE) {
        int v = val->boolean ? 1 : 0;
        if (v != layer->mouse_move_subscribed) {
            layer->mouse_move_subscribed = v;
            changed = 1;
        }
    } else if (prop == VTERM_PROP_ALTSCREEN) {
        /* libvterm has already swapped its internal buffer pointer; refresh
         * our GPU-side pointer so the next render samples the right one,
         * and notify the terminal so the other layers can save/restore. */
        layer->alt_active = val->boolean ? 1 : 0;
        layer->rs.buffers[0].data = (uint8_t *)vterm_screen_get_buffer(layer->screen);
        layer->rs.buffers[0].size = vterm_screen_get_buffer_size(layer->screen);
        layer->rs.buffers[0].dirty = 1;
        layer->base.dirty = 1;
        if (layer->base.alt_screen_fn) {
            struct yetty_ycore_void_result r =
                layer->base.alt_screen_fn(val->boolean ? 1 : 0, layer->base.alt_screen_userdata);
            if (YETTY_IS_ERR(r)) {
                yerror("text_layer on_settermprop: alt_screen_fn failed: %s", r.error.msg);
                if (YETTY_IS_OK(layer->pending_error)) {
                    layer->pending_error = r;
                } else {
                    yetty_ycore_error_destroy(r.error);
                }
            }
        }
    } else if (prop == VTERM_PROP_CURSORVISIBLE) {
        /* DECTCEM (CSI ?25 h/l) only fires this prop — movecursor isn't
         * called when visibility toggles without a move. nvim and friends
         * routinely hide the cursor on startup and show it again when
         * idle at the prompt; without this branch the GPU uniform stays
         * stuck at the last movecursor's visibility. */
        layer->vterm_cursor_visible = val->boolean ? 1.0f : 0.0f;
        if (!layer->view_active) {
            set_cursor_visible(&layer->rs, layer->vterm_cursor_visible);
        }
        layer->base.dirty = 1;
    } else if (prop == VTERM_PROP_CURSORSHAPE) {
        /* DECSCUSR (CSI <n> SP q): vterm reports 1=block, 2=underline,
         * 3=bar. The shader's cursor branch matches these directly. */
        set_cursor_shape(&layer->rs, (float)val->number);
        layer->base.dirty = 1;
    }

    if (changed && layer->base.mouse_sub_fn) {
        struct yetty_ycore_void_result r =
            layer->base.mouse_sub_fn(layer->mouse_click_subscribed, layer->mouse_move_subscribed,
                                     layer->base.mouse_sub_userdata);
        if (YETTY_IS_ERR(r)) {
            yerror("text_layer on_settermprop: mouse_sub_fn failed: %s", r.error.msg);
            if (YETTY_IS_OK(layer->pending_error)) {
                layer->pending_error = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    return 1;
}

/* Create */

/* libvterm output callback. Signature dictated by VTermOutputCallback
 * (void return). Forwards bytes to the layer's PTY write callback;
 * errors from that Result-returning upcall are stashed in pending_error. */
YETTY_EXTERNAL_CALLBACK
static void vterm_output_callback(const char *data, size_t len, void *user)
{
    struct yetty_yvterm_text_layer *text_layer = user;
    if (!text_layer->base.pty_write_fn) {
        return;
    }
    struct yetty_ycore_void_result r =
        text_layer->base.pty_write_fn(data, len, text_layer->base.pty_write_userdata);
    if (YETTY_IS_ERR(r)) {
        yerror("text_layer vterm_output_callback: pty_write_fn failed: %s", r.error.msg);
        if (YETTY_IS_OK(text_layer->pending_error)) {
            text_layer->pending_error = r;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
}

/* Resolve one config colour key to a VTermColor. Missing / malformed keys
 * fall back to default_hex so a partial palette in config still works. */
static VTermColor color_from_config(const struct yetty_yconfig_config *config, const char *key,
                                    const char *default_hex)
{
    const char *s = NULL;
    if (config && config->ops && config->ops->get_string) {
        s = config->ops->get_string(config, key, default_hex);
    }
    if (!s || !*s) {
        s = default_hex;
    }
    uint32_t packed = 0;
    VTermColor col = {0};
    if (yetty_ycore_parse_hex_color(s, &packed) ||
        yetty_ycore_parse_hex_color(default_hex, &packed)) {
        /* parse_hex_color packs byte0=R, byte1=G, byte2=B. */
        col.red = (uint8_t)(packed & 0xFFu);
        col.green = (uint8_t)((packed >> 8) & 0xFFu);
        col.blue = (uint8_t)((packed >> 16) & 0xFFu);
    }
    return col;
}

/* Apply the configurable terminal colour palette to a freshly-created vterm.
 *
 * libvterm resolves every indexed colour (ANSI 30-37/90-97, 38;5;n for n<16)
 * through state->colors[0..15]; truecolor (38;2;r;g;b) bypasses it entirely.
 * The built-in defaults are deliberately harsh full-saturation primaries, so
 * we override them here with softer values and let config refine each one.
 * Keys (each a #RRGGBB string):
 *   terminal/colors/normal/{black,red,green,yellow,blue,magenta,cyan,white}
 *   terminal/colors/bright/{...same...}
 *   terminal/colors/{foreground,background}
 * Must run before vterm_screen_reset() so the reset copies our default
 * fg/bg into the active pen.
 */
static void apply_color_palette(const struct yetty_yconfig_config *config, VTerm *vterm)
{
    /* index, config key, soft default — defaults mirror a muted xterm-style
     * palette (the kind tmux / nvim / ls look right against). */
    static const struct {
        const char *key;
        const char *hex;
    } palette[16] = {
        {"terminal/colors/normal/black", "#1d1f21"}, {"terminal/colors/normal/red", "#cc6666"},
        {"terminal/colors/normal/green", "#b5bd68"}, {"terminal/colors/normal/yellow", "#f0c674"},
        {"terminal/colors/normal/blue", "#81a2be"},  {"terminal/colors/normal/magenta", "#b294bb"},
        {"terminal/colors/normal/cyan", "#8abeb7"},  {"terminal/colors/normal/white", "#c5c8c6"},
        {"terminal/colors/bright/black", "#666666"}, {"terminal/colors/bright/red", "#d54e53"},
        {"terminal/colors/bright/green", "#b9ca4a"}, {"terminal/colors/bright/yellow", "#e7c547"},
        {"terminal/colors/bright/blue", "#7aa6da"},  {"terminal/colors/bright/magenta", "#c397d8"},
        {"terminal/colors/bright/cyan", "#70c0ba"},  {"terminal/colors/bright/white", "#eaeaea"},
    };

    VTermState *state = vterm_obtain_state(vterm);
    if (!state) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        VTermColor col = color_from_config(config, palette[i].key, palette[i].hex);
        vterm_state_set_palette_color(state, i, &col);
    }

    /* Default fg/bg keep yetty's existing look unless config overrides them:
     * near-white text on a black canvas (the pane background is painted
     * separately by yui). */
    VTermColor fg = color_from_config(config, "terminal/colors/foreground", "#f0f0f0");
    VTermColor bg = color_from_config(config, "terminal/colors/background", "#000000");
    vterm_state_set_default_colors(state, &fg, &bg);
}

/* Mark-sweep the rich-handle table against the cell buffers. Nothing releases
 * a handle on clear/scroll/reflow — those paths only zero a cell's rich_handle
 * and freely duplicate cells via memmove — so a slot is reclaimed only once no
 * physical cell (in either the primary or alt buffer) still references it. This
 * sidesteps every double-free hazard the cell-duplicating paths would create.
 * Cost: a couple of linear scans of the 2*rows*cols cell buffers. */
static struct yetty_ycore_void_result text_layer_gc_handles(
    struct yetty_yvterm_text_layer *text_layer)
{
    /* Handles only ever live on the primary buffer. While the altscreen is
     * active the active buffer is the altscreen (no handles), so scanning it
     * would reap the primary's still-referenced handles — suppress GC until
     * the primary screen is restored. */
    if (!text_layer->screen || text_layer->alt_active) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_cell_ref_table *table = &text_layer->cell_ref_table;
    uint32_t live_before = table->count > 1 ? (table->count - 1 - table->free_count) : 0;
    struct yetty_ycore_void_result gb = yetty_ydraw_cell_ref_table_gc_begin(table);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gb, "text_layer_gc_handles: gc_begin");
    const VTermScreenCell *buffer = vterm_screen_get_buffer(text_layer->screen);
    size_t cells = vterm_screen_get_buffer_size(text_layer->screen) / sizeof(VTermScreenCell);
    for (size_t cell = 0; cell < cells; cell++) {
        yetty_ydraw_cell_ref_table_gc_mark(table, buffer[cell].rich_handle);
    }
    struct yetty_ycore_void_result ge = yetty_ydraw_cell_ref_table_gc_end(table);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ge, "text_layer_gc_handles: gc_end");
    uint32_t live_after = table->count > 1 ? (table->count - 1 - table->free_count) : 0;
    if (live_before != live_after) {
        ydebug("rich: gc reclaimed %u handles (live %u -> %u)", live_before - live_after,
               live_before, live_after);
    }
    return YETTY_OK_VOID();
}

/* cell_source.handle_at — returns a writable pointer to the rich_handle of
 * the live-screen cell at (row, col), or NULL if off-screen. The ydraw canvas
 * calls this to read/stamp handles without depending on libvterm. */
static uint32_t *text_layer_cell_handle_at(void *user, uint32_t row, uint32_t col)
{
    struct yetty_yvterm_text_layer *text_layer = user;
    if (!text_layer->screen || row >= text_layer->base.grid_size.rows ||
        col >= text_layer->base.grid_size.cols) {
        return NULL;
    }
    /* Index the live screen inside libvterm's 2*rows-tall buffer, same as the
     * zero-copy GPU upload does (root_row marks visible row 0). Cast away const
     * to write the handle back — the text-layer already owns this buffer for
     * upload. */
    VTermScreenCell *buffer = (VTermScreenCell *)vterm_screen_get_buffer(text_layer->screen);
    uint32_t root_row = (uint32_t)vterm_screen_get_buffer_root_row(text_layer->screen);
    uint32_t cols = text_layer->base.grid_size.cols;
    return &buffer[(size_t)(root_row + row) * cols + col].rich_handle;
}

struct yetty_ycore_void_result yetty_yvterm_text_layer_bind_ydraw(
    struct yetty_yrender_terminal_layer *self, struct yetty_yrender_terminal_layer *ydraw_layer)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);
    /* Build the cell source locally — handle_at + the ref table stay private
     * to the text-layer. The ydraw canvas reads/stamps cell handles through
     * it without ever seeing the table. */
    struct yetty_ydraw_cell_source source = {
        .handle_at = text_layer_cell_handle_at,
        .user = text_layer,
        .table = &text_layer->cell_ref_table,
    };
    return yetty_yvterm_ydraw_content_set_cell_source(ydraw_layer, &source);
}

/* yclass object behind the owned shader-glyph figure (for render/dirty/destroy
 * dispatch), or NULL if none. */
static struct yetty_yclass_object *text_layer_sgf_object(struct yetty_yvterm_text_layer *text_layer)
{
    if (!text_layer->shader_glyph_figure) {
        return NULL;
    }
    struct yetty_yfigure_figure *base =
        yetty_yvterm_shader_glyph_figure_as_figure(text_layer->shader_glyph_figure);
    return (struct yetty_yclass_object *)base - 1;
}

struct yetty_yterminal_layer_result yetty_yvterm_text_layer_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterminal_cursor_fn cursor_fn,
    void *cursor_userdata)
{
    struct yetty_yvterm_text_layer *text_layer;

    /* Load text-layer shader from file */
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/text-layer.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_yterminal_layer,
                         "text_layer_create: read_file(text-layer.wgsl) failed", shader_res);
    }

    text_layer = calloc(1, sizeof(struct yetty_yvterm_text_layer));
    if (!text_layer) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_yterminal_layer, "failed to allocate text layer");
    }

    /* Initialise the scrollback arena with the configured max-lines cap. */
    yetty_yvterm_text_sb_arena_init(&text_layer->sb, config->ops->scrollback_lines
                                                         ? config->ops->scrollback_lines(config)
                                                         : 10000);

    text_layer->shader_code = shader_res.value;
    text_layer->base.ops = &text_layer_ops;
    text_layer->base.grid_size.cols = cols;
    text_layer->base.grid_size.rows = rows;
    text_layer->base.cell_size.width = 10.0f;
    text_layer->base.cell_size.height = 20.0f;
    text_layer->base.dirty = 1;
    text_layer->base.pty_write_fn = pty_write_fn;
    text_layer->base.pty_write_userdata = pty_write_userdata;
    text_layer->base.request_render_fn = request_render_fn;
    text_layer->base.request_render_userdata = request_render_userdata;
    text_layer->base.scroll_fn = scroll_fn;
    text_layer->base.scroll_userdata = scroll_userdata;
    text_layer->base.cursor_fn = cursor_fn;
    text_layer->base.cursor_userdata = cursor_userdata;

    /* Create font from config */
    /* Default MSDF: on a fresh install, try_load_config_file() runs BEFORE
     * extract_assets writes config.yaml to disk, so this fallback is what
     * actually ships. Raster bitmaps look fuzzy under Ctrl+Scroll (shader-level
     * zoom just stretches the atlas); MSDF re-evaluates per fragment and
     * stays crisp at any scale. */
    const char *render_method =
        config->ops->get_string(config, YETTY_YCONFIG_KEY_TERMINAL_FONT_RENDER_METHOD, "msdf");
    ydebug("text_layer: render_method='%s'", render_method);
    struct yetty_font_ms_font_result font_res;
    if (strcmp(render_method, "msdf") == 0) {
        const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
        const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
        const char *font_family = config->ops->font_family(config);
        if (!font_family || strcmp(font_family, "default") == 0) {
            font_family = "DejaVuSansMNerdFontMono";
        }
        char cdb_path[512];
        char shader_path[512];
        snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir,
                 font_family);
        snprintf(shader_path, sizeof(shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);
        ydebug("text_layer: ms-msdf cdb_path='%s' shader='%s'", cdb_path, shader_path);
        /* Config carries the font size in logical (CSS-style) pixels;
         * scale once here to framebuffer pixels so the glyphs render at
         * the right physical size on HiDPI displays without every other
         * pipeline stage having to know about content_scale. */
        float content_scale = context->runtime->gpu.app_gpu_context.content_scale;
        if (content_scale <= 0.0f) {
            content_scale = 1.0f;
        }
        float msdf_font_size =
            (float)config->ops->get_int(config, "terminal/text-layer/font/size", 14) *
            content_scale;

        /* Cell padding around the glyph, fractions of glyph dim. Default 0
         * = cell exactly wraps the glyph extent, which fixes the "glyph too
         * small in cell" feel introduced by the underscore fix. */
        struct yetty_yfont_ms_padding padding = {
            .left = strtof(
                config->ops->get_string(config, "terminal/text-layer/font/padding/left", "0.0"),
                NULL),
            .right = strtof(
                config->ops->get_string(config, "terminal/text-layer/font/padding/right", "0.0"),
                NULL),
            .top = strtof(
                config->ops->get_string(config, "terminal/text-layer/font/padding/top", "0.0"),
                NULL),
            .bottom = strtof(
                config->ops->get_string(config, "terminal/text-layer/font/padding/bottom", "0.0"),
                NULL),
        };

        font_res = yetty_yfont_ms_msdf_font_create(cdb_path, shader_path, msdf_font_size, padding);
    } else {
        font_res = yetty_yfont_ms_raster_font_create(config, text_layer->base.cell_size.width,
                                                     text_layer->base.cell_size.height);
    }
    if (YETTY_IS_ERR(font_res)) {
        free(text_layer);
        return YETTY_ERR(yetty_yterminal_layer, "text_layer_create: font creation failed",
                         font_res);
    }
    ydebug("text_layer: font created");
    text_layer->font = font_res.value;
    text_layer->font_type = (strcmp(render_method, "msdf") == 0) ? 0u : 6u;

    /* Get cell size from font */
    struct pixel_size_result cs_res = text_layer->font->ops->get_cell_size(text_layer->font);
    if (YETTY_IS_ERR(cs_res)) {
        free(text_layer);
        return YETTY_ERR(yetty_yterminal_layer, "text_layer_create: font get_cell_size failed",
                         cs_res);
    }
    text_layer->base.cell_size.width = cs_res.value.width;
    text_layer->base.cell_size.height = cs_res.value.height;
    ydebug("text_layer: cell_size from font: %.1fx%.1f", cs_res.value.width, cs_res.value.height);

    text_layer->vterm = vterm_new((int)rows, (int)cols);
    if (!text_layer->vterm) {
        free(text_layer);
        return YETTY_ERR(yetty_yterminal_layer, "failed to create vterm");
    }

    vterm_set_utf8(text_layer->vterm, 1);
    text_layer->screen = vterm_obtain_screen(text_layer->vterm, resolve_glyph, text_layer);
    vterm_screen_set_callbacks(text_layer->screen, &screen_callbacks, text_layer);
    vterm_screen_enable_altscreen(text_layer->screen, 1);
    vterm_screen_enable_reflow(text_layer->screen, 1);

    /* Per-cell rich-handle table. Slots are reclaimed by text_layer_gc_handles
     * (mark-sweep over the cell buffers), not by a per-clear callback. */
    yetty_ydraw_cell_ref_table_init(&text_layer->cell_ref_table);

    /* Override the built-in (harsh) ANSI palette with softer, config-driven
     * colours before the reset copies default fg/bg into the active pen. */
    apply_color_palette(context->runtime->config, text_layer->vterm);

    vterm_screen_reset(text_layer->screen, 1);

    /* Set up vterm output callback to write to PTY */
    vterm_output_set_callback(text_layer->vterm, vterm_output_callback, text_layer);

    /* Resource set */
    strncpy(text_layer->rs.namespace, "text_grid", YETTY_YRENDER_NAME_MAX - 1);

    text_layer->rs.buffer_count = 1;
    strncpy(text_layer->rs.buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(text_layer->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    text_layer->rs.buffers[0].readonly = 1;

    init_uniforms(&text_layer->rs);
    set_grid_size(&text_layer->rs, (float)cols, (float)rows);
    set_cell_size(&text_layer->rs, text_layer->base.cell_size.width,
                  text_layer->base.cell_size.height);
    text_layer->rs.uniforms[U_FONT_TYPE].u32 = text_layer->font_type;

    /* Set pixel size for render target */
    text_layer->rs.pixel_size.width = (float)cols * text_layer->base.cell_size.width;
    text_layer->rs.pixel_size.height = (float)rows * text_layer->base.cell_size.height;

    yetty_yrender_shader_code_set(&text_layer->rs.shader,
                                  (const char *)text_layer->shader_code.data,
                                  text_layer->shader_code.size);

    if (text_layer->font) {
        text_layer->rs.children_count = 1;
    }

    /* Initial buffer setup - point to vterm buffer directly.
     * Cast away const is safe: buffer is readonly, GPU only reads from it. */
    text_layer->rs.buffers[0].data = (uint8_t *)vterm_screen_get_buffer(text_layer->screen);
    text_layer->rs.buffers[0].size = vterm_screen_get_buffer_size(text_layer->screen);
    text_layer->rs.buffers[0].readonly = 1;

    /* Clear dirty — vterm_screen_reset fires on_damage but there's no real content yet.
     * First real dirty will come from PTY data via on_damage. */
    text_layer->base.dirty = 0;
    text_layer->rs.buffers[0].dirty = 0;

    /* Create the shader-glyph figure here — it scans this layer's cell buffer,
     * so the text-layer owns its creation + management. The terminal attaches
     * it to the root container (which renders + owns its destroy). */
    {
        struct yetty_ycore_rectangle sgf_rect = {
            .min = {.x = 0.0f, .y = 0.0f},
            .max = {.x = (float)cols * text_layer->base.cell_size.width,
                    .y = (float)rows * text_layer->base.cell_size.height},
        };
        struct yetty_yvterm_shader_glyph_figure_ptr_result sgf_res =
            yetty_yvterm_shader_glyph_figure_create(
                sgf_rect, cols, rows, text_layer->base.cell_size.width,
                text_layer->base.cell_size.height, &text_layer->base, context, request_render_fn,
                request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_yterminal_layer, sgf_res,
                            "text_layer_create: shader_glyph figure create failed");
        text_layer->shader_glyph_figure = sgf_res.value;
    }

    return YETTY_OK(yetty_yterminal_layer, &text_layer->base);
}

/* Ops implementations */

static struct yetty_ycore_void_result text_layer_destroy(struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    /* We own the shader-glyph figure (it's not a compositor child) — destroy it
     * before vterm, while the cell buffer it borrows is still valid. */
    struct yetty_yclass_object *sgf_obj = text_layer_sgf_object(text_layer);
    if (sgf_obj) {
        struct yetty_ycore_void_result dr = yetty_yfigure_destroy(NULL, sgf_obj);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        text_layer->shader_glyph_figure = NULL;
    }

    if (text_layer->vterm) {
        vterm_free(text_layer->vterm);
    }
    /* Free the table after vterm: no cell can reference it once vterm is gone,
     * and nothing in teardown calls back into it (GC reclaims, no callback). */
    yetty_ydraw_cell_ref_table_destroy(&text_layer->cell_ref_table);

    yetty_yvterm_text_sb_arena_destroy(&text_layer->sb);
    free(text_layer->view_staging);

    free(text_layer->shader_code.data);
    free(text_layer);
    return YETTY_OK_VOID();
}

/* Process — drains raw bytes from the SM into vterm. The SM is in
 * SCAN_RAW; osc_statemachine_read returns 0 when an OSC opener is at
 * the cursor (or input is exhausted). */
/* Runs on the persistent default-layer coro spawned by the wire SM at
 * set_default. Never returns under normal operation — `sm_read` yields
 * when no raw bytes are deliverable, the SM scanner resumes us when
 * more arrive. A returned ERR would surface to the SM at the next tick.
 *
 * userdata is the layer's base pointer (passed to set_default at
 * registration time). */
struct yetty_ycore_void_result yetty_yvterm_text_layer_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *osc_statemachine)
{
    struct yetty_yrender_terminal_layer *self = userdata;
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    if (!text_layer->vterm) {
        return YETTY_ERR(yetty_ycore_void, "vterm is NULL");
    }

    uint8_t buf[4096];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(osc_statemachine, buf, sizeof(buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "text_layer: osc read");
        /* sm_read in RAW mode never returns 0 unless something is
         * fundamentally wrong — it yields when no raw bytes are ready.
         * If we ever DO see 0, just yield again (defensive: avoid a
         * tight loop in the unlikely event the contract changes). */
        if (rr.value == 0) {
            continue;
        }
        vterm_input_write(text_layer->vterm, (const char *)buf, rr.value);
    }
}

static struct yetty_ycore_void_result text_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    if (!text_layer->vterm) {
        return YETTY_ERR(yetty_ycore_void, "vterm is NULL");
    }

    struct yetty_ycore_void_result cr = text_layer_apply_cell_size(text_layer, cell_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "text_layer_resize_grid: apply_cell_size");

    vterm_set_size(text_layer->vterm, (int)grid_size.rows, (int)grid_size.cols);
    self->grid_size = grid_size;
    set_grid_size(&text_layer->rs, (float)grid_size.cols, (float)grid_size.rows);

    /* Pixel size = grid * cell. With caller picking cell_size = client / rows,
     * this matches the framebuffer exactly. */
    text_layer->rs.pixel_size.width = (float)grid_size.cols * self->cell_size.width;
    text_layer->rs.pixel_size.height = (float)grid_size.rows * self->cell_size.height;

    /* Keep the owned shader-glyph figure tracking the grid (it's outside
     * layers[], so it never sees the resize broadcast on its own). */
    if (text_layer->shader_glyph_figure) {
        struct yetty_ycore_void_result sgr = yetty_yvterm_shader_glyph_figure_resize(
            text_layer->shader_glyph_figure, grid_size, cell_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sgr,
                            "text_layer_resize_grid: shader_glyph figure resize");
    }

    self->dirty = 1;
    return YETTY_OK_VOID();
}

/* Convert GLFW modifier flags to VTerm modifier flags */
static VTermModifier glfw_mods_to_vterm(int mods)
{
    VTermModifier vt_mod = VTERM_MOD_NONE;
    if (mods & 0x0001) {
        vt_mod |= VTERM_MOD_SHIFT;
    }
    if (mods & 0x0002) {
        vt_mod |= VTERM_MOD_CTRL;
    }
    if (mods & 0x0004) {
        vt_mod |= VTERM_MOD_ALT;
    }
    return vt_mod;
}

/* Convert GLFW key code to VTerm key (for special keys) */
static VTermKey glfw_key_to_vterm(int key)
{
    switch (key) {
    case 257:
        return VTERM_KEY_ENTER; /* GLFW_KEY_ENTER */
    case 258:
        return VTERM_KEY_TAB; /* GLFW_KEY_TAB */
    case 259:
        return VTERM_KEY_BACKSPACE; /* GLFW_KEY_BACKSPACE */
    case 260:
        return VTERM_KEY_INS; /* GLFW_KEY_INSERT */
    case 261:
        return VTERM_KEY_DEL; /* GLFW_KEY_DELETE */
    case 262:
        return VTERM_KEY_RIGHT; /* GLFW_KEY_RIGHT */
    case 263:
        return VTERM_KEY_LEFT; /* GLFW_KEY_LEFT */
    case 264:
        return VTERM_KEY_DOWN; /* GLFW_KEY_DOWN */
    case 265:
        return VTERM_KEY_UP; /* GLFW_KEY_UP */
    case 266:
        return VTERM_KEY_PAGEUP; /* GLFW_KEY_PAGE_UP */
    case 267:
        return VTERM_KEY_PAGEDOWN; /* GLFW_KEY_PAGE_DOWN */
    case 268:
        return VTERM_KEY_HOME; /* GLFW_KEY_HOME */
    case 269:
        return VTERM_KEY_END; /* GLFW_KEY_END */
    case 256:
        return VTERM_KEY_ESCAPE; /* GLFW_KEY_ESCAPE */
    case 290:
        return VTERM_KEY_FUNCTION(1);
    case 291:
        return VTERM_KEY_FUNCTION(2);
    case 292:
        return VTERM_KEY_FUNCTION(3);
    case 293:
        return VTERM_KEY_FUNCTION(4);
    case 294:
        return VTERM_KEY_FUNCTION(5);
    case 295:
        return VTERM_KEY_FUNCTION(6);
    case 296:
        return VTERM_KEY_FUNCTION(7);
    case 297:
        return VTERM_KEY_FUNCTION(8);
    case 298:
        return VTERM_KEY_FUNCTION(9);
    case 299:
        return VTERM_KEY_FUNCTION(10);
    case 300:
        return VTERM_KEY_FUNCTION(11);
    case 301:
        return VTERM_KEY_FUNCTION(12);
    default:
        return VTERM_KEY_NONE;
    }
}

static int text_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    if (!text_layer->vterm) {
        return 0;
    }

    VTermKey vt_key = glfw_key_to_vterm(key);
    if (vt_key != VTERM_KEY_NONE) {
        VTermModifier vt_mod = glfw_mods_to_vterm(mods);
        vterm_keyboard_key(text_layer->vterm, vt_key, vt_mod);
        ydebug("text_layer_on_key: key=%d vt_key=%d mods=%d", key, (int)vt_key, mods);
        return 1;
    }
    return 0; /* Not a special key */
}

static int text_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                              int mods)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    if (!text_layer->vterm) {
        return 0;
    }

    VTermModifier vt_mod = glfw_mods_to_vterm(mods);
    vterm_keyboard_unichar(text_layer->vterm, codepoint, vt_mod);
    ydebug("text_layer_on_char: codepoint=U+%04X mods=%d", codepoint, mods);
    return 1;
}

static struct yetty_yrender_gpu_resource_set_result text_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yvterm_text_layer *text_layer =
        (struct yetty_yvterm_text_layer *)((const char *)self -
                                           offsetof(struct yetty_yvterm_text_layer, base));

    /* Reclaim rich-handle slots no cell references anymore. Runs before the
     * ydraw layer (rendered after us) reads the table, and after this frame's
     * ingestion has stamped any new handles, so live refs are never collected.
     *
     * GC is best-effort: an OOM growing the mark bitmap just defers the sweep
     * to the next dirty pass (slots stay live, never wrongly collected), so a
     * failure here must not abort producing this frame's resource set. */
    struct yetty_ycore_void_result gc_res = text_layer_gc_handles(text_layer);
    if (YETTY_IS_ERR(gc_res)) {
        ydebug("rich: gc pass skipped: %s", gc_res.error.msg ? gc_res.error.msg : "(no msg)");
        yetty_ycore_error_destroy(gc_res.error);
    }

    /* Live mode: GPU reads vterm's buffer directly (zero-copy).
     * Scrollback view: rebuild the stitched buffer every dirty pass — new
     * pushlines arriving in the background change what live[0] is, and the
     * bottom of the viewport may dip into live, so a single snapshot at
     * view-enter time isn't enough. The cost is cols*rows cells per dirty
     * frame, which is small (e.g. 80*30*12 = 28KB).
     * Cast away const is safe: buffer is readonly, GPU only reads from it. */
    if (text_layer->base.dirty) {
        if (text_layer->view_active) {
            text_layer_build_view(text_layer);
            text_layer->rs.buffers[0].data = (uint8_t *)text_layer->view_staging;
            text_layer->rs.buffers[0].size = (size_t)text_layer->base.grid_size.cols *
                                             text_layer->base.grid_size.rows *
                                             sizeof(VTermScreenCell);
            /* view_staging is laid out cols*rows starting at row 0, so the
             * shader should NOT apply any root_row offset. */
            set_root_row(&text_layer->rs, 0);
        } else {
            text_layer->rs.buffers[0].data = (uint8_t *)vterm_screen_get_buffer(text_layer->screen);
            text_layer->rs.buffers[0].size = vterm_screen_get_buffer_size(text_layer->screen);
            /* libvterm's buffer is allocated 2*rows tall; the live screen
             * starts at row root_row within it. The shader uses this to
             * locate the visible cells. */
            set_root_row(&text_layer->rs,
                         (uint32_t)vterm_screen_get_buffer_root_row(text_layer->screen));
        }
        text_layer->rs.buffers[0].dirty = 1;
    }

    /* Update font child pointer */
    if (text_layer->font && text_layer->font->ops && text_layer->font->ops->get_gpu_resource_set) {
        struct yetty_yrender_gpu_resource_set_result font_rs =
            text_layer->font->ops->get_gpu_resource_set(text_layer->font);
        if (YETTY_IS_OK(font_rs)) {
            text_layer->rs.children[0] = (struct yetty_yrender_gpu_resource_set *)font_rs.value;
        }
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &text_layer->rs);
}

/* Render layer to target - delegate to render_target */
static struct yetty_ycore_int_result text_layer_render(struct yetty_yrender_terminal_layer *self,
                                                       struct yetty_ydraw_target *target, int force)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);
    struct yetty_yclass_object *sgf_obj = text_layer_sgf_object(text_layer);
    int figure_dirty = sgf_obj && yetty_yfigure_figure_dirty_get(sgf_obj).value;

    /* Render iff the text grid is dirty/forced, or the owned shader-glyph
     * figure has animation pending. Return 1 when we drew so higher layers
     * cascade their repaint. */
    if (!self->dirty && !force && !figure_dirty) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    int drew = 0;
    if (self->dirty || force) {
        struct yetty_ycore_void_result rr = target->ops->render_layer(target, self);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rr, "text_layer_render: target->render_layer");
        self->dirty = 0;
        drew = 1;
    }

    /* ISOLATION TEST: figure rendered separately after the layer loop. */
    (void)figure_dirty;
    (void)sgf_obj;

    return YETTY_OK(yetty_ycore_int, drew);
}

/* TEMP isolation: render the owned shader-glyph figure (called after the layer
 * loop to mimic the old compositor timing). */
struct yetty_ycore_void_result yetty_yvterm_text_layer_render_figures(
    struct yetty_yrender_terminal_layer *self, struct yetty_ydraw_target *target)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);
    struct yetty_yclass_object *sgf_obj = text_layer_sgf_object(text_layer);
    if (!sgf_obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result fr = yetty_yfigure_render(NULL, sgf_obj, target);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "text_layer_render_figures: shader_glyph render");
    {
        struct yetty_ycore_void_result drop_r = yetty_yfigure_figure_dirty_set(sgf_obj, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    return YETTY_OK_VOID();
}

/* Borrow the current GPU cell buffer. Used by sibling layers (e.g. the
 * shader-glyph layer) that need to read the same grid as the text shader.
 * Returns the same pointer text-layer uploads — live screen in normal mode,
 * stitched scrollback view when view_active. */
void yetty_yvterm_text_layer_get_cells(const struct yetty_yrender_terminal_layer *self,
                                       const uint8_t **out_data, size_t *out_size)
{
    const struct yetty_yvterm_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_text_layer, base);

    if (text_layer->view_active && text_layer->view_staging) {
        if (out_data) {
            *out_data = (const uint8_t *)text_layer->view_staging;
        }
        if (out_size) {
            *out_size = (size_t)text_layer->base.grid_size.cols * text_layer->base.grid_size.rows *
                        sizeof(VTermScreenCell);
        }
        return;
    }

    if (out_data) {
        *out_data = (const uint8_t *)vterm_screen_get_buffer(text_layer->screen);
    }
    if (out_size) {
        *out_size = vterm_screen_get_buffer_size(text_layer->screen);
    }
}

uint32_t yetty_yvterm_text_layer_get_root_row(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_text_layer, base);

    /* Scrollback view hands back a staging buffer laid out from row 0 — same
     * convention text_layer_get_gpu_resource_set forces on the shader uniform. */
    if (text_layer->view_active && text_layer->view_staging) {
        return 0;
    }
    if (!text_layer->screen) {
        return 0;
    }
    return (uint32_t)vterm_screen_get_buffer_root_row(text_layer->screen);
}

/* VTerm callbacks */

YETTY_EXTERNAL_CALLBACK
static int on_damage(VTermRect rect, void *user)
{
    struct yetty_yvterm_text_layer *text_layer = user;
    ydebug("on_damage: rect(%d,%d)-(%d,%d) -> dirty=1", rect.start_row, rect.start_col,
           rect.end_row, rect.end_col);
    text_layer->base.dirty = 1;
    return 1;
}

/* libvterm state-level erase, routed through screen — fires on CSI ED/EL.
 * libvterm has already cleared the affected cells; we only forward the
 * full-screen erase up to the terminal so non-text layers (ydraw scene
 * canvas etc.) can wipe their own content. Partial erases (single line,
 * cursor-to-end) don't translate to scene-canvas semantics and are
 * ignored here. */
YETTY_EXTERNAL_CALLBACK
static int on_erase(VTermRect rect, int selective, void *user)
{
    struct yetty_yvterm_text_layer *layer = user;
    (void)selective;
    if (!layer) {
        return 1;
    }

    int full_rows = (rect.start_row == 0 && rect.end_row >= (int)layer->base.grid_size.rows);
    int full_cols = (rect.start_col == 0 && rect.end_col >= (int)layer->base.grid_size.cols);
    if (!(full_rows && full_cols)) {
        return 1;
    }

    if (layer->base.clear_screen_fn) {
        struct yetty_ycore_void_result r =
            layer->base.clear_screen_fn(layer->base.clear_screen_userdata);
        if (YETTY_IS_ERR(r)) {
            yerror("text_layer on_erase: clear_screen_fn failed: %s", r.error.msg);
            if (YETTY_IS_OK(layer->pending_error)) {
                layer->pending_error = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int on_move_cursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    struct yetty_yvterm_text_layer *text_layer = user;
    (void)oldpos;
    set_cursor_pos(&text_layer->rs, (float)pos.col, (float)pos.row);
    /* Track vterm's reported visibility so we can restore it on exit from
     * scrollback view. Only push to the GPU uniform when not in view —
     * while in view the cursor is forced hidden. */
    text_layer->vterm_cursor_visible = visible ? 1.0f : 0.0f;
    if (!text_layer->view_active) {
        set_cursor_visible(&text_layer->rs, text_layer->vterm_cursor_visible);
    }
    text_layer->base.dirty = 1;

    /* Notify cursor callback */
    if (text_layer->base.cursor_fn) {
        struct yetty_ycore_void_result r =
            text_layer->base.cursor_fn(&text_layer->base,
                                       (struct yetty_ycore_grid_cursor_pos){
                                           .cols = (uint32_t)pos.col, .rows = (uint32_t)pos.row},
                                       text_layer->base.cursor_userdata);
        if (YETTY_IS_ERR(r)) {
            yerror("text_layer on_move_cursor: cursor_fn failed: %s", r.error.msg);
            if (YETTY_IS_OK(text_layer->pending_error)) {
                text_layer->pending_error = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    return 1;
}

YETTY_EXTERNAL_CALLBACK
static int on_sb_pushline(int cols, const VTermScreenCell *cells, void *user)
{
    struct yetty_yvterm_text_layer *text_layer = user;

    /* Capture the row vterm is evicting from the top of the live screen.
     * The arena copies the cells into its own storage so libvterm's buffer
     * can be reused immediately. Resizes don't rewrite stored lines. */
    if (cells && cols > 0) {
        yetty_yvterm_text_sb_arena_push(&text_layer->sb, cells, cols);
        ydebug("on_sb_pushline: stored line cols=%d sb_count=%u", cols, text_layer->sb.lines_count);
    }

    /* Notify scroll callback - 1 line scrolled down
     * BUT: if in_external_scroll is set, this scroll was triggered by another
     * layer and we should NOT propagate back to avoid double-scroll loop */
    if (text_layer->base.scroll_fn && !text_layer->base.in_external_scroll) {
        struct yetty_ycore_void_result res =
            text_layer->base.scroll_fn(&text_layer->base, 1, text_layer->base.scroll_userdata);
        if (YETTY_IS_ERR(res)) {
            yerror("on_sb_pushline: scroll_fn failed: %s", res.error.msg);
            if (YETTY_IS_OK(text_layer->pending_error)) {
                text_layer->pending_error = res;
            } else {
                yetty_ycore_error_destroy(res.error);
            }
        }
    }
    return 1;
}

/* Live anchor — count of rows pushed off the top of the screen so far. The
 * terminal converts mouse-wheel deltas relative to this so view_top_total_idx
 * stays absolute and stable as new content keeps arriving during scrollback. */
static uint32_t text_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_text_layer, base);
    /* ABSOLUTE index of the live-screen top in libvterm's full timeline:
     * scrollback lines ever pushed, net of pops. Equals the ydraw canvas's
     * rolling_row_0 (both advance 1:1 via the lockstep scroll), so the two
     * layers index the same line by the same number even after eviction
     * starts saturating lines_count. */
    return (uint32_t)(text_layer->sb.lines_evicted + text_layer->sb.lines_count);
}

/* Oldest absolute line index still held in the scrollback arena — the floor a
 * scrollback view may scroll up to. Below this the line has been evicted. */
static uint32_t text_layer_get_scrollback_floor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_text_layer, base);
    return (uint32_t)text_layer->sb.lines_evicted;
}

/* Reallocate view_staging if the requested cell count outgrew capacity.
 * Returns 1 on success. Caller writes cells * sizeof(VTermScreenCell) bytes. */
static int ensure_view_staging(struct yetty_yvterm_text_layer *layer, size_t cells)
{
    size_t bytes = cells * sizeof(VTermScreenCell);
    if (bytes <= layer->view_staging_capacity) {
        return 1;
    }
    void *new_buf = realloc(layer->view_staging, bytes);
    if (!new_buf) {
        yerror("ensure_view_staging: realloc(%zu) failed", bytes);
        return 0;
    }
    layer->view_staging = new_buf;
    layer->view_staging_capacity = bytes;
    return 1;
}

/* Stitch sb_lines + live screen into view_staging so the GPU sees a frozen
 * historical viewport. Each gpu_y maps to absolute total_idx
 * (view_top_total_idx + gpu_y); below sb_count we read from sb_lines, at or
 * above sb_count we read from the live vterm screen. Beyond either source we
 * clear the row to a blank cell so old garbage from a prior view doesn't leak.
 *
 * Width mismatches between an sb line (captured at one cols) and the current
 * grid cols are handled by truncating or clearing the trailing columns. */
static void text_layer_build_view(struct yetty_yvterm_text_layer *layer)
{
    uint32_t cols = layer->base.grid_size.cols;
    uint32_t rows = layer->base.grid_size.rows;
    if (cols == 0 || rows == 0) {
        return;
    }

    if (!ensure_view_staging(layer, (size_t)cols * rows)) {
        return;
    }

    const VTermScreenCell *live = vterm_screen_get_buffer(layer->screen);
    uint32_t live_root_row = (uint32_t)vterm_screen_get_buffer_root_row(layer->screen);
    VTermScreenCell blank;
    memset(&blank, 0, sizeof(blank));

    /* view_top_total_idx is now an ABSOLUTE line index in libvterm's full
     * timeline (matching the ydraw canvas's rolling_row_0). The arena holds
     * the half-open absolute window [evicted, evicted+lines_count); above that
     * is the live screen; below `evicted` the line is gone. */
    uint32_t evicted = (uint32_t)layer->sb.lines_evicted;
    uint32_t live_top = evicted + layer->sb.lines_count;

    for (uint32_t gpu_y = 0; gpu_y < rows; gpu_y++) {
        uint32_t total_idx = layer->view_top_total_idx + gpu_y;
        VTermScreenCell *dst = layer->view_staging + (size_t)gpu_y * cols;

        if (total_idx >= evicted && total_idx < live_top) {
            uint32_t arena_slot = total_idx - evicted;
            const struct yetty_yvterm_text_sb_line_rec *rec =
                yetty_yvterm_text_sb_arena_peek(&layer->sb, arena_slot);
            /* Sanity-check the record. If anything looks off (uninit slot,
             * stale offset, negative cols), fill blank and shout. Without
             * this we segfault deep in memcpy with no breadcrumbs. */
            if (!rec || rec->cols <= 0 || rec->offset >= layer->sb.cells_cap ||
                (size_t)rec->cols * sizeof(VTermScreenCell) > layer->sb.cells_cap) {
                yerror("build_view: bogus rec total_idx=%u arena_slot=%u view_top=%u gpu_y=%u "
                       "evicted=%u lines_count=%u lines_cap=%u lines_tail=%u rec=%p "
                       "rec->cols=%d rec->offset=%zu cells_cap=%zu",
                       total_idx, arena_slot, layer->view_top_total_idx, gpu_y, evicted,
                       layer->sb.lines_count, layer->sb.lines_cap, layer->sb.lines_tail,
                       (const void *)rec, rec ? rec->cols : 0, rec ? rec->offset : (size_t)0,
                       layer->sb.cells_cap);
                for (uint32_t c = 0; c < cols; c++) {
                    dst[c] = blank;
                }
                continue;
            }
            int copy = (rec->cols < (int)cols) ? rec->cols : (int)cols;
            yetty_yvterm_text_sb_arena_read(&layer->sb, rec->offset, copy, dst);
            for (int c = copy; c < (int)cols; c++) {
                dst[c] = blank;
            }
        } else if (total_idx >= live_top && live) {
            uint32_t live_row = total_idx - live_top;
            if (live_row < rows) {
                memcpy(dst, live + (size_t)(live_root_row + live_row) * cols,
                       (size_t)cols * sizeof(VTermScreenCell));
            } else {
                for (uint32_t c = 0; c < cols; c++) {
                    dst[c] = blank;
                }
            }
        } else {
            /* total_idx < evicted: line aged out of the arena, or no live
             * buffer — nothing to show. */
            for (uint32_t c = 0; c < cols; c++) {
                dst[c] = blank;
            }
        }
    }
}

/* Pin the layer to a historical viewport (active=1) or release back to the
 * live screen (active=0). When activating, hide the cursor and snap the GPU
 * buffer to the synthetic stitched view; on release, restore the cursor to
 * whatever vterm last reported and re-point the buffer at the live screen. */
static struct yetty_ycore_void_result text_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);

    text_layer->view_active = active ? 1 : 0;
    text_layer->view_top_total_idx = view_top_total_idx;

    if (text_layer->view_active) {
        set_cursor_visible(&text_layer->rs, 0.0f);
    } else {
        set_cursor_visible(&text_layer->rs, text_layer->vterm_cursor_visible);
    }

    text_layer->base.dirty = 1;
    if (text_layer->base.request_render_fn) {
        struct yetty_ycore_void_result r =
            text_layer->base.request_render_fn(text_layer->base.request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                            "text_layer_set_view_top: request_render_fn failed");
    }
    return YETTY_OK_VOID();
}

/* vterm asks for a previously-pushed line back, e.g. when the screen grows
 * and rows above the cursor need to be backfilled. We hand back the most
 * recent stored line and drop it from our buffer. Width mismatches are
 * vterm's problem: it only reads up to min(stored_cols, target_cols) and
 * clears the rest itself (see screen.c sb_popline call site). */
YETTY_EXTERNAL_CALLBACK
static int on_sb_popline(int cols, VTermScreenCell *cells, void *user)
{
    struct yetty_yvterm_text_layer *text_layer = user;
    int copy_cols = yetty_yvterm_text_sb_arena_pop(&text_layer->sb, cells, cols);
    if (copy_cols == 0) {
        return 0;
    }
    /* Scrollback storage may carry a stale rich_handle (the live cell that
     * was evicted still owns the table slot). Zero it on restore so a popped
     * line never re-introduces a handle that aliases a live/recycled slot —
     * rich content is not preserved across scrollback in this increment. */
    for (int col = 0; col < copy_cols; col++) {
        cells[col].rich_handle = 0;
    }
    ydebug("on_sb_popline: returned %d cols (target=%d) sb_count=%u", copy_cols, cols,
           text_layer->sb.lines_count);
    return 1;
}

/* Sort (anchor_row, anchor_col) and (head_row, head_col) into a reading-order
 * (start, end) pair. start always precedes end in top-down, left-to-right
 * stream order. */
static void text_layer_sel_sorted(const struct yetty_yvterm_text_layer *layer, uint32_t *out_sr,
                                  uint32_t *out_sc, uint32_t *out_er, uint32_t *out_ec)
{
    uint32_t sr = layer->sel_anchor_row;
    uint32_t sc = layer->sel_anchor_col;
    uint32_t er = layer->sel_head_row;
    uint32_t ec = layer->sel_head_col;
    if (sr > er || (sr == er && sc > ec)) {
        uint32_t tr = sr, tc = sc;
        sr = er;
        sc = ec;
        er = tr;
        ec = tc;
    }
    *out_sr = sr;
    *out_sc = sc;
    *out_er = er;
    *out_ec = ec;
}

/* Encode a Unicode codepoint as UTF-8 into a 4-byte buffer. Returns the
 * byte count. Mirrors libvterm's fill_utf8 — we re-implement here because
 * cells store font glyph indices (not codepoints), so we can't reuse
 * vterm_screen_get_text and need our own UTF-8 emission path. */
static size_t text_layer_cp_to_utf8(uint32_t cp, uint8_t out[4])
{
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (uint8_t)(0xF0 | (cp >> 18));
        out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    /* Out of range — fall back to U+FFFD REPLACEMENT CHARACTER. */
    out[0] = 0xEF;
    out[1] = 0xBF;
    out[2] = 0xBD;
    return 3;
}

/* Recover the codepoint a cell was drawn from.
 *
 * Cells store the font *atlas glyph index*, not the original codepoint —
 * yetty's libvterm modification calls the resolver and stashes
 * `resolved.glyph_index` in `cell->glyph_index` (see screen.c::putglyph),
 * which drops the codepoint on the floor. For clipboard extraction we
 * walk back through the font's inverse map (or the shader-glyph PUA
 * inverse for animated cells) to reconstruct it. */
static uint32_t text_layer_codepoint_from_cell(struct yetty_yvterm_text_layer *layer,
                                               uint32_t glyph_index)
{
    if (glyph_index == 0) {
        return 0x20; /* empty cell → space */
    }
    if (yetty_shader_glyph_is(glyph_index)) {
        return yetty_shader_glyph_codepoint_from_id(glyph_index);
    }
    if (layer->font && layer->font->ops && layer->font->ops->get_codepoint) {
        struct uint32_result r = layer->font->ops->get_codepoint(layer->font, glyph_index);
        if (YETTY_IS_OK(r)) {
            return r.value;
        }
    }
    return 0xFFFD; /* unknown — emit U+FFFD instead of garbage */
}

/* Append a single row's selected slice [start_col, end_col) to out. The
 * range is exclusive on the right so end_col==cols means "include the last
 * cell". We trim trailing blanks within the slice — copying 80 trailing
 * spaces because the user dragged past EOL is never useful. */
static struct yetty_ycore_void_result text_layer_append_row_slice(
    struct yetty_yvterm_text_layer *layer, uint32_t row, uint32_t start_col, uint32_t end_col,
    struct yetty_ycore_buffer *out)
{
    int cols = (int)layer->base.grid_size.cols;
    if (cols <= 0 || (int)start_col >= cols || end_col == 0 || start_col >= end_col) {
        return YETTY_OK_VOID();
    }
    if ((int)end_col > cols) {
        end_col = (uint32_t)cols;
    }

    /* Trim trailing blanks within the slice. Walks cells right-to-left
     * looking for the rightmost non-empty glyph index; GLYPH_WIDE_CONT
     * is treated as non-empty since it belongs to a wide char to the left. */
    int last_col = -1;
    for (int c = (int)end_col - 1; c >= (int)start_col; c--) {
        VTermScreenCell cell;
        VTermPos pos = {.row = (int)row, .col = c};
        if (!vterm_screen_get_cell(layer->screen, pos, &cell)) {
            continue;
        }
        uint32_t g = cell.glyph_index;
        if (g != 0 && g != 0xFFFEu /* GLYPH_WIDE_CONT */) {
            last_col = c;
            break;
        }
    }
    if (last_col < (int)start_col) {
        return YETTY_OK_VOID();
    }

    /* Walk cells left-to-right, mapping each glyph_index back to a
     * codepoint via the font's inverse op and emitting UTF-8. We skip
     * GLYPH_WIDE_CONT (the right half of a double-width glyph) since
     * its codepoint already went out with the left half. */
    for (int c = (int)start_col; c <= last_col; c++) {
        VTermScreenCell cell;
        VTermPos pos = {.row = (int)row, .col = c};
        if (!vterm_screen_get_cell(layer->screen, pos, &cell)) {
            continue;
        }
        uint32_t g = cell.glyph_index;
        if (g == 0xFFFEu /* GLYPH_WIDE_CONT */) {
            continue;
        }
        uint32_t cp = text_layer_codepoint_from_cell(layer, g);
        uint8_t utf8[4];
        size_t n = text_layer_cp_to_utf8(cp, utf8);
        struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(out, utf8, n);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "text_layer row slice write");
    }
    return YETTY_OK_VOID();
}

/* Stream-extract the current selection. Empty selection → no-op. */
static struct yetty_ycore_void_result text_layer_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out)
{
    /* Cast away const — extraction goes through the font, whose
     * get_codepoint may lazily populate caches (raster font scans). The
     * cells themselves are read-only here. */
    struct yetty_yvterm_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_text_layer, base);

    if (!text_layer->sel_active || !text_layer->screen || !out) {
        return YETTY_OK_VOID();
    }

    uint32_t cols = self->grid_size.cols;
    uint32_t rows = self->grid_size.rows;
    if (cols == 0 || rows == 0) {
        return YETTY_OK_VOID();
    }

    uint32_t sr, sc, er, ec;
    text_layer_sel_sorted(text_layer, &sr, &sc, &er, &ec);
    if (sr >= rows) {
        return YETTY_OK_VOID();
    }
    if (er >= rows) {
        er = rows - 1;
        ec = cols;
    }

    if (sr == er) {
        /* Single-row selection. The exclusive end is sc..ec; clamp to cols. */
        uint32_t end = ec > cols ? cols : ec;
        return text_layer_append_row_slice(text_layer, sr, sc, end, out);
    }

    /* First row: from sc to end of row. */
    struct yetty_ycore_void_result r = text_layer_append_row_slice(text_layer, sr, sc, cols, out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "selection first row");
    {
        struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(out, "\n", 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "selection LF");
    }

    /* Middle rows: full rows. */
    for (uint32_t row = sr + 1; row < er; row++) {
        r = text_layer_append_row_slice(text_layer, row, 0, cols, out);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "selection middle row");
        struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(out, "\n", 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "selection middle LF");
    }

    /* Last row: from 0 to ec. */
    uint32_t end = ec > cols ? cols : ec;
    r = text_layer_append_row_slice(text_layer, er, 0, end, out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "selection last row");

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result text_layer_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col)
{
    struct yetty_yvterm_text_layer *text_layer =
        container_of(self, struct yetty_yvterm_text_layer, base);
    text_layer->sel_active = active;
    text_layer->sel_anchor_row = anchor_row;
    text_layer->sel_anchor_col = anchor_col;
    text_layer->sel_head_row = head_row;
    text_layer->sel_head_col = head_col;
    set_selection_state(&text_layer->rs, active, anchor_row, anchor_col, head_row, head_col);
    text_layer->base.dirty = 1;
    return YETTY_OK_VOID();
}
