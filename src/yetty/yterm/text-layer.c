#include <yetty/yterm/text-layer.h>
#include <yetty/yterm/shader-glyph-layer.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/ms-raster-font.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
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
static inline void set_grid_size(struct yetty_ypaint_core_gpu_resource_set *rs, float cols,
                                 float rows)
{
    rs->uniforms[U_GRID_SIZE].vec2[0] = cols;
    rs->uniforms[U_GRID_SIZE].vec2[1] = rows;
}
static inline void set_cell_size(struct yetty_ypaint_core_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_CELL_SIZE].vec2[0] = w;
    rs->uniforms[U_CELL_SIZE].vec2[1] = h;
}
static inline void set_cursor_pos(struct yetty_ypaint_core_gpu_resource_set *rs, float col,
                                  float row)
{
    rs->uniforms[U_CURSOR_POS].vec2[0] = col;
    rs->uniforms[U_CURSOR_POS].vec2[1] = row;
}
static inline void set_cursor_visible(struct yetty_ypaint_core_gpu_resource_set *rs, float v)
{
    rs->uniforms[U_CURSOR_VISIBLE].f32 = v;
}
static inline void set_cursor_shape(struct yetty_ypaint_core_gpu_resource_set *rs, float s)
{
    rs->uniforms[U_CURSOR_SHAPE].f32 = s;
}
static inline void set_scale(struct yetty_ypaint_core_gpu_resource_set *rs, float s)
{
    rs->uniforms[U_SCALE].f32 = s;
}
static inline void set_default_fg(struct yetty_ypaint_core_gpu_resource_set *rs, uint32_t c)
{
    rs->uniforms[U_DEFAULT_FG].u32 = c;
}
static inline void set_default_bg(struct yetty_ypaint_core_gpu_resource_set *rs, uint32_t c)
{
    rs->uniforms[U_DEFAULT_BG].u32 = c;
}
static inline void set_visual_zoom(struct yetty_ypaint_core_gpu_resource_set *rs, float scale,
                                   float off_x, float off_y)
{
    rs->uniforms[U_VZ_SCALE].f32 = scale;
    rs->uniforms[U_VZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_VZ_OFF].vec2[1] = off_y;
}
static inline void set_root_row(struct yetty_ypaint_core_gpu_resource_set *rs, uint32_t r)
{
    rs->uniforms[U_ROOT_ROW].u32 = r;
}
static inline void set_selection_state(struct yetty_ypaint_core_gpu_resource_set *rs, int active,
                                       uint32_t anchor_row, uint32_t anchor_col,
                                       uint32_t head_row, uint32_t head_col)
{
    rs->uniforms[U_SEL_ACTIVE].u32 = active ? 1u : 0u;
    rs->uniforms[U_SEL_ANCHOR].vec2[0] = (float)anchor_row;
    rs->uniforms[U_SEL_ANCHOR].vec2[1] = (float)anchor_col;
    rs->uniforms[U_SEL_HEAD].vec2[0] = (float)head_row;
    rs->uniforms[U_SEL_HEAD].vec2[1] = (float)head_col;
}

/* Init — names and types use the same constants */
static void init_uniforms(struct yetty_ypaint_core_gpu_resource_set *rs)
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

/* Per-line descriptor in the scrollback arena: byte offset into the cells
 * ring + width at push time. Width is frozen — resize never rewrites stored
 * lines; pops just truncate/pad to whatever vterm asks for. */
struct yetty_yterm_text_sb_line_rec {
    size_t offset;
    int cols;
};

/* Scrollback arena.
 *
 * Cells payload is packed back-to-back in a byte ring; lines[] is a parallel
 * ring of (offset, cols) descriptors. Two phases:
 *
 *   Growth phase (lines_cap < max_lines): both buffers double std::vector-
 *     style when full. No eviction. Data is contiguous from offset 0, so
 *     plain realloc preserves it.
 *
 *   Steady phase (lines_cap == max_lines): both buffers fixed; push evicts
 *     oldest line(s) until the new line fits in cells[] and lines[] has a
 *     free slot. Pure circular ring.
 *
 * Logical indexing: position 0 = oldest live line, lines_count-1 = newest.
 * Stored at lines[(lines_tail + i) % lines_cap]. */
struct yetty_yterm_text_sb_arena {
    uint8_t *cells;
    size_t cells_cap;
    size_t cells_head;
    size_t cells_tail;
    size_t cells_used;

    struct yetty_yterm_text_sb_line_rec *lines;
    uint32_t lines_cap;
    uint32_t lines_head;
    uint32_t lines_tail;
    uint32_t lines_count;

    uint32_t max_lines;
};

/* Text layer - embeds base as first member */
struct yetty_yterm_terminal_text_layer {
    struct yetty_yrender_terminal_layer base;
    VTerm *vterm;
    VTermScreen *screen;
    struct yetty_yfont_ms_font *font;
    uint32_t font_type; /* 0=msdf, 6=raster */
    struct yetty_ycore_buffer shader_code;
    struct yetty_ypaint_core_gpu_resource_set rs;
    struct yetty_ycore_void_result pending_error; /* Error from vterm callbacks */
    /* DEC mode 1500/1501 — mirrored from libvterm via settermprop. The
     * terminal reads these (via base.mouse_sub_fn) to decide whether to
     * forward GLFW mouse events as OSC 777777/777778. */
    int mouse_click_subscribed;
    int mouse_move_subscribed;

    /* Scrollback buffer. Two-ring arena (cells + line descriptors) — see
     * struct yetty_yterm_text_sb_arena above. Logical position 0 is the
     * oldest live line; pop reads the newest. */
    struct yetty_yterm_text_sb_arena sb;

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
static struct yetty_ycore_void_result text_layer_destroy(struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result text_layer_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_yterm_osc_statemachine *osc_statemachine);
static struct yetty_ycore_void_result text_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size);
static struct yetty_yrender_gpu_resource_set_result text_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static int text_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int text_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                              int mods);
static struct yetty_ycore_void_result text_layer_render(struct yetty_yrender_terminal_layer *self,
                                                        struct yetty_ypaint_core_target *target);
static uint32_t text_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result text_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx);
static void text_layer_build_view(struct yetty_yterm_terminal_text_layer *layer);
static struct yetty_ycore_void_result text_layer_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out);
static void text_layer_set_selection(struct yetty_yrender_terminal_layer *self, int active,
                                     uint32_t anchor_row, uint32_t anchor_col, uint32_t head_row,
                                     uint32_t head_col);

/* VTerm callbacks */
static int on_damage(VTermRect rect, void *user);
static int on_move_cursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
static int on_sb_pushline(int cols, const VTermScreenCell *cells, void *user);
static int on_sb_popline(int cols, VTermScreenCell *cells, void *user);
static int on_settermprop(VTermProp prop, VTermValue *val, void *user);

/* Glyph resolver — called by vterm for every codepoint */
static VTermResolvedGlyph resolve_glyph(const uint32_t *chars, int count, int bold, int italic,
                                        void *user)
{
    struct yetty_yterm_terminal_text_layer *text_layer = user;
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
     * IDs are allocated from UINT32_MAX downward. */
    if (yetty_shader_glyph_codepoint_in_range(chars[0])) {
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
        ydebug("resolve_glyph: get_glyph_index_styled ERR for U+%04X: %s", chars[0],
               glyph_res.error.msg);
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

/*=============================================================================
 * Scrollback arena
 *
 * See struct yetty_yterm_text_sb_arena for the design. All operations are
 * O(1) amortised; eviction is only entered after lines_cap hits max_lines.
 *===========================================================================*/

static void sb_arena_init(struct yetty_yterm_text_sb_arena *a, uint32_t max_lines)
{
    memset(a, 0, sizeof(*a));
    a->max_lines = max_lines ? max_lines : 1;
}

static void sb_arena_destroy(struct yetty_yterm_text_sb_arena *a)
{
    free(a->cells);
    free(a->lines);
    memset(a, 0, sizeof(*a));
}

/* Drop the oldest line, freeing its bytes from the cells ring. */
static void sb_arena_drop_oldest(struct yetty_yterm_text_sb_arena *a)
{
    if (a->lines_count == 0) {
        return;
    }
    size_t bytes = (size_t)a->lines[a->lines_tail].cols * sizeof(VTermScreenCell);
    a->cells_tail = (a->cells_tail + bytes) % a->cells_cap;
    a->cells_used -= bytes;
    a->lines_tail = (a->lines_tail + 1) % a->lines_cap;
    a->lines_count--;
}

/* Drop the newest line. Used by pop. */
static void sb_arena_drop_newest(struct yetty_yterm_text_sb_arena *a)
{
    if (a->lines_count == 0) {
        return;
    }
    a->lines_head = (a->lines_head + a->lines_cap - 1) % a->lines_cap;
    size_t bytes = (size_t)a->lines[a->lines_head].cols * sizeof(VTermScreenCell);
    a->cells_head = (a->cells_head + a->cells_cap - bytes) % a->cells_cap;
    a->cells_used -= bytes;
    a->lines_count--;
}

/* Read cols cells from the ring at byte offset into dst, handling wrap.
 * Defensive: garbage line records (uninit/stale) have shown up under heavy
 * ypaint scrollback. Bail with a loud yerror rather than memcpy from a
 * bogus pointer — the caller has already filled dst with blank if needed. */
static void sb_arena_read(const struct yetty_yterm_text_sb_arena *a, size_t offset, int cols,
                          VTermScreenCell *dst)
{
    if (cols <= 0) {
        return;
    }
    size_t bytes = (size_t)cols * sizeof(VTermScreenCell);
    if (offset >= a->cells_cap || bytes > a->cells_cap) {
        yerror("sb_arena_read: invalid args offset=%zu cols=%d bytes=%zu "
               "cells_cap=%zu cells_head=%zu cells_tail=%zu cells_used=%zu "
               "lines_cap=%u lines_head=%u lines_tail=%u lines_count=%u",
               offset, cols, bytes, a->cells_cap, a->cells_head, a->cells_tail, a->cells_used,
               a->lines_cap, a->lines_head, a->lines_tail, a->lines_count);
        return;
    }
    if (offset + bytes <= a->cells_cap) {
        memcpy(dst, a->cells + offset, bytes);
    } else {
        size_t first = a->cells_cap - offset;
        memcpy(dst, a->cells + offset, first);
        memcpy((uint8_t *)dst + first, a->cells, bytes - first);
    }
}

/* Resolve logical index (0=oldest) to a descriptor pointer. */
static const struct yetty_yterm_text_sb_line_rec *sb_arena_peek(
    const struct yetty_yterm_text_sb_arena *a, uint32_t i)
{
    if (i >= a->lines_count) {
        return NULL;
    }
    return &a->lines[(a->lines_tail + i) % a->lines_cap];
}

/* Push one line. Returns 1 on success. */
static int sb_arena_push(struct yetty_yterm_text_sb_arena *a, const VTermScreenCell *src, int cols)
{
    if (cols <= 0) {
        return 1;
    }
    size_t bytes = (size_t)cols * sizeof(VTermScreenCell);

    /* Growth phase: extend lines[] up to max_lines. The live range is
     * always linear in this phase (lines_tail = 0), so realloc preserves it. */
    if (a->lines_count >= a->lines_cap && a->lines_cap < a->max_lines) {
        uint32_t new_cap = a->lines_cap == 0 ? 256 : a->lines_cap * 2;
        if (new_cap > a->max_lines) {
            new_cap = a->max_lines;
        }
        struct yetty_yterm_text_sb_line_rec *grown =
            realloc(a->lines, (size_t)new_cap * sizeof(*grown));
        if (!grown) {
            yerror("sb_arena: lines realloc failed (new_cap=%u)", new_cap);
            return 0;
        }
        a->lines = grown;
        a->lines_cap = new_cap;
    }

    /* Growth phase: extend cells[] until the new line fits. Same linearity
     * argument — cells_tail = 0 so realloc keeps the range valid. */
    while (a->cells_used + bytes > a->cells_cap && a->lines_cap < a->max_lines) {
        size_t new_cap = a->cells_cap == 0 ? 64 * 1024 : a->cells_cap * 2;
        while (new_cap < a->cells_used + bytes) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(a->cells, new_cap);
        if (!grown) {
            yerror("sb_arena: cells realloc failed (%zu)", new_cap);
            return 0;
        }
        a->cells = grown;
        a->cells_cap = new_cap;
    }

    /* Steady phase: evict oldest until lines[] has a free slot and cells[]
     * has room. If a single line exceeds the entire cells_cap (extreme
     * resize), one-time grow rather than dropping data silently. */
    while (a->lines_count >= a->lines_cap || a->cells_used + bytes > a->cells_cap) {
        if (a->lines_count == 0) {
            uint8_t *grown = realloc(a->cells, bytes);
            if (!grown) {
                yerror("sb_arena: emergency cells realloc (%zu) failed", bytes);
                return 0;
            }
            a->cells = grown;
            a->cells_cap = bytes;
            break;
        }
        sb_arena_drop_oldest(a);
    }

    /* Write payload, handling ring wrap. */
    if (a->cells_head + bytes <= a->cells_cap) {
        memcpy(a->cells + a->cells_head, src, bytes);
    } else {
        size_t first = a->cells_cap - a->cells_head;
        memcpy(a->cells + a->cells_head, src, first);
        memcpy(a->cells, (const uint8_t *)src + first, bytes - first);
    }

    /* Record line descriptor. */
    a->lines[a->lines_head].offset = a->cells_head;
    a->lines[a->lines_head].cols = cols;
    a->lines_head = (a->lines_head + 1) % a->lines_cap;
    a->cells_head = (a->cells_head + bytes) % a->cells_cap;
    a->cells_used += bytes;
    a->lines_count++;
    return 1;
}

/* Pop newest line. Copies up to copy_cols cells into dst. Returns the number
 * of cells copied (0 on empty). */
static int sb_arena_pop(struct yetty_yterm_text_sb_arena *a, VTermScreenCell *dst, int copy_cols)
{
    if (a->lines_count == 0 || !dst || copy_cols <= 0) {
        return 0;
    }
    uint32_t newest = (a->lines_head + a->lines_cap - 1) % a->lines_cap;
    int line_cols = a->lines[newest].cols;
    int n = line_cols < copy_cols ? line_cols : copy_cols;
    sb_arena_read(a, a->lines[newest].offset, n, dst);
    sb_arena_drop_newest(a);
    return n;
}

/* Append a synthetic blank scrollback line — used when ypaint asks to
 * scroll more lines than the live screen contains. Without this we'd
 * desync from ypaint's rolling_row_0 (which counts every row of unified
 * scroll history, including space the text screen never had content in). */
static void push_blank_sb_line(struct yetty_yterm_terminal_text_layer *layer, int cols)
{
    if (cols <= 0) {
        return;
    }
    enum { YETTY_YTERM_STACK_BLANK_CELLS = 4096 };
    if (cols <= YETTY_YTERM_STACK_BLANK_CELLS) {
        VTermScreenCell stack_blanks[YETTY_YTERM_STACK_BLANK_CELLS] = {0};
        sb_arena_push(&layer->sb, stack_blanks, cols);
    } else {
        VTermScreenCell *blanks = calloc((size_t)cols, sizeof(VTermScreenCell));
        if (!blanks) {
            yerror("push_blank_sb_line: calloc failed (cols=%d)", cols);
            return;
        }
        sb_arena_push(&layer->sb, blanks, cols);
        free(blanks);
    }
}

/* Receive scroll from other layers (e.g., ypaint).
 *
 * libvterm's vterm_scroll_rect() takes a fast path when |downward| >= rows:
 * it just erases the screen and skips the per-row moverect machinery, so
 * sb_pushline never fires. That fast path desyncs us from ypaint's
 * rolling_row_0 — every row of unified scroll history must add exactly one
 * entry to sb_lines, even when the text screen had no content for it.
 *
 * Solution: cap the chunk vterm sees at rows-1 (so it always takes the slow,
 * sb-pushing path), and once vterm has emptied the screen, append blank
 * entries directly for the remainder. */
static struct yetty_ycore_void_result text_layer_scroll(struct yetty_yrender_terminal_layer *self,
                                                        int lines)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

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

/* Receive cursor position from other layers (e.g., ypaint) */
static struct yetty_ycore_void_result text_layer_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

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

static struct yetty_ycore_void_result text_layer_set_cell_size(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell size");
    }

    /* Ask the font to re-rasterize (raster) or update its requested render
     * size (MSDF) FIRST, so get_cell_size() reports something useful if we
     * later want to snap to the font's natural cell. */
    if (text_layer->font && text_layer->font->ops && text_layer->font->ops->set_cell_size) {
        struct yetty_ycore_void_result r =
            text_layer->font->ops->set_cell_size(text_layer->font, cell_size);
        if (!YETTY_IS_OK(r)) {
            ywarn("text_layer_set_cell_size: font set_cell_size failed: %s", r.error.msg);
        }
    }

    self->cell_size = cell_size;
    /* Push to the GPU uniform the shader actually reads. Keeping base.cell_size
     * in sync without this is invisible to the shader. */
    set_cell_size(&text_layer->rs, cell_size.width, cell_size.height);
    text_layer->rs.pixel_size.width = (float)self->grid_size.cols * cell_size.width;
    text_layer->rs.pixel_size.height = (float)self->grid_size.rows * cell_size.height;
    self->dirty = 1;
    ydebug("text_layer_set_cell_size: %.1fx%.1f", cell_size.width, cell_size.height);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result text_layer_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);
    set_visual_zoom(&text_layer->rs, scale, off_x, off_y);
    self->dirty = 1;
    return YETTY_OK_VOID();
}

/* Ops */
static const struct yetty_yterm_terminal_layer_ops text_layer_ops = {
    .destroy = text_layer_destroy,
    .process_input = text_layer_process_input,
    .resize_grid = text_layer_resize_grid,
    .set_cell_size = text_layer_set_cell_size,
    .set_visual_zoom = text_layer_set_visual_zoom,
    .get_gpu_resource_set = text_layer_get_gpu_resource_set,
    .render = text_layer_render,
    .is_empty = text_layer_is_empty,
    .on_key = text_layer_on_key,
    .on_char = text_layer_on_char,
    .scroll = text_layer_scroll,
    .set_cursor = text_layer_set_cursor,
    .get_live_anchor = text_layer_get_live_anchor,
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
};

/* libvterm settermprop callback — only the props we care about are
 * forwarded; everything else is ignored. CARDCLICK / CARDMOVE come from
 * DEC modes ?1500 / ?1501 and gate whether the terminal emits OSC
 * 777777 / 777778 mouse events. */
static int on_settermprop(VTermProp prop, VTermValue *val, void *user)
{
    struct yetty_yterm_terminal_text_layer *layer = user;
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
        layer->rs.buffers[0].data = (uint8_t *)vterm_screen_get_buffer(layer->screen);
        layer->rs.buffers[0].size = vterm_screen_get_buffer_size(layer->screen);
        layer->rs.buffers[0].dirty = 1;
        layer->base.dirty = 1;
        if (layer->base.alt_screen_fn) {
            layer->base.alt_screen_fn(val->boolean ? 1 : 0, layer->base.alt_screen_userdata);
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
        layer->base.mouse_sub_fn(layer->mouse_click_subscribed, layer->mouse_move_subscribed,
                                 layer->base.mouse_sub_userdata);
    }
    return 1;
}

/* Create */

/* VTerm output callback - forwards to layer's PTY write callback */
static void vterm_output_callback(const char *data, size_t len, void *user)
{
    struct yetty_yterm_terminal_text_layer *text_layer = user;
    if (text_layer->base.pty_write_fn) {
        text_layer->base.pty_write_fn(data, len, text_layer->base.pty_write_userdata);
    }
}

struct yetty_yterm_terminal_layer_result yetty_yterm_terminal_text_layer_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterm_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata)
{
    struct yetty_yterm_terminal_text_layer *text_layer;

    /* Load text-layer shader from file */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/text-layer.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_yterm_terminal_layer, shader_res.error.msg);
    }

    text_layer = calloc(1, sizeof(struct yetty_yterm_terminal_text_layer));
    if (!text_layer) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, "failed to allocate text layer");
    }

    /* Initialise the scrollback arena with the configured max-lines cap. */
    sb_arena_init(&text_layer->sb,
                  config->ops->scrollback_lines ? config->ops->scrollback_lines(config) : 10000);

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
        float content_scale = context->app_context.app_gpu_context.content_scale;
        if (content_scale <= 0.0f) {
            content_scale = 1.0f;
        }
        float msdf_font_size =
            (float)config->ops->get_int(config, "terminal/text-layer/font/size", 14) * content_scale;

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
    if (!YETTY_IS_OK(font_res)) {
        yerror("text_layer: font creation failed: %s", font_res.error.msg);
        free(text_layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, font_res.error.msg);
    }
    ydebug("text_layer: font created");
    text_layer->font = font_res.value;
    text_layer->font_type = (strcmp(render_method, "msdf") == 0) ? 0u : 6u;

    /* Get cell size from font */
    struct pixel_size_result cs_res = text_layer->font->ops->get_cell_size(text_layer->font);
    if (YETTY_IS_ERR(cs_res)) {
        free(text_layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, cs_res.error.msg);
    }
    text_layer->base.cell_size.width = cs_res.value.width;
    text_layer->base.cell_size.height = cs_res.value.height;
    ydebug("text_layer: cell_size from font: %.1fx%.1f", cs_res.value.width, cs_res.value.height);

    text_layer->vterm = vterm_new((int)rows, (int)cols);
    if (!text_layer->vterm) {
        free(text_layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, "failed to create vterm");
    }

    vterm_set_utf8(text_layer->vterm, 1);
    text_layer->screen = vterm_obtain_screen(text_layer->vterm, resolve_glyph, text_layer);
    vterm_screen_set_callbacks(text_layer->screen, &screen_callbacks, text_layer);
    vterm_screen_enable_altscreen(text_layer->screen, 1);
    vterm_screen_enable_reflow(text_layer->screen, 1);
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

    return YETTY_OK(yetty_yterm_terminal_layer, &text_layer->base);
}

/* Ops implementations */

static struct yetty_ycore_void_result text_layer_destroy(struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

    if (text_layer->vterm) {
        vterm_free(text_layer->vterm);
    }

    sb_arena_destroy(&text_layer->sb);
    free(text_layer->view_staging);

    free(text_layer->shader_code.data);
    free(text_layer);
    return YETTY_OK_VOID();
}

/* Process — drains raw bytes from the SM into vterm. The SM is in
 * SCAN_RAW; osc_statemachine_read returns 0 when an OSC opener is at
 * the cursor (or input is exhausted). */
static struct yetty_ycore_void_result text_layer_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_yterm_osc_statemachine *osc_statemachine)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

    if (!text_layer->vterm) {
        return YETTY_ERR(yetty_ycore_void, "vterm is NULL");
    }

    uint8_t buf[4096];
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_yterm_osc_statemachine_read(osc_statemachine, buf, sizeof(buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "text_layer: osc read");
        if (rr.value == 0) {
            break;
        }
        vterm_input_write(text_layer->vterm, (const char *)buf, rr.value);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result text_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

    if (!text_layer->vterm) {
        return YETTY_ERR(yetty_ycore_void, "vterm is NULL");
    }

    vterm_set_size(text_layer->vterm, (int)grid_size.rows, (int)grid_size.cols);
    self->grid_size = grid_size;
    set_grid_size(&text_layer->rs, (float)grid_size.cols, (float)grid_size.rows);

    /* Update pixel size */
    text_layer->rs.pixel_size.width = (float)grid_size.cols * self->cell_size.width;
    text_layer->rs.pixel_size.height = (float)grid_size.rows * self->cell_size.height;

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
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

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
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

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
    struct yetty_yterm_terminal_text_layer *text_layer =
        (struct yetty_yterm_terminal_text_layer *)((const char *)self -
                                                   offsetof(struct yetty_yterm_terminal_text_layer,
                                                            base));

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
            text_layer->rs.children[0] = (struct yetty_ypaint_core_gpu_resource_set *)font_rs.value;
        }
    }

    return YETTY_OK(yetty_yrender_gpu_resource_set, &text_layer->rs);
}

/* Render layer to target - delegate to render_target */
static struct yetty_ycore_void_result text_layer_render(struct yetty_yrender_terminal_layer *self,
                                                        struct yetty_ypaint_core_target *target)
{
    return target->ops->render_layer(target, self);
}

/* Borrow the current GPU cell buffer. Used by sibling layers (e.g. the
 * shader-glyph layer) that need to read the same grid as the text shader.
 * Returns the same pointer text-layer uploads — live screen in normal mode,
 * stitched scrollback view when view_active. */
void yetty_yterm_terminal_layer_terminal_text_layer_get_cells(
    const struct yetty_yrender_terminal_layer *self, const uint8_t **out_data, size_t *out_size)
{
    const struct yetty_yterm_terminal_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_terminal_text_layer, base);

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

/* VTerm callbacks */

static int on_damage(VTermRect rect, void *user)
{
    struct yetty_yterm_terminal_text_layer *text_layer = user;
    ydebug("on_damage: rect(%d,%d)-(%d,%d) -> dirty=1", rect.start_row, rect.start_col,
           rect.end_row, rect.end_col);
    text_layer->base.dirty = 1;
    return 1;
}

static int on_move_cursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    struct yetty_yterm_terminal_text_layer *text_layer = user;
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
        text_layer->base.cursor_fn(&text_layer->base,
                                   (struct yetty_ycore_grid_cursor_pos){.cols = (uint32_t)pos.col,
                                                                        .rows = (uint32_t)pos.row},
                                   text_layer->base.cursor_userdata);
    }
    return 1;
}

static int on_sb_pushline(int cols, const VTermScreenCell *cells, void *user)
{
    struct yetty_yterm_terminal_text_layer *text_layer = user;

    /* Capture the row vterm is evicting from the top of the live screen.
     * The arena copies the cells into its own storage so libvterm's buffer
     * can be reused immediately. Resizes don't rewrite stored lines. */
    if (cells && cols > 0) {
        sb_arena_push(&text_layer->sb, cells, cols);
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
            text_layer->pending_error = res;
        }
    }
    return 1;
}

/* Live anchor — count of rows pushed off the top of the screen so far. The
 * terminal converts mouse-wheel deltas relative to this so view_top_total_idx
 * stays absolute and stable as new content keeps arriving during scrollback. */
static uint32_t text_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_terminal_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_terminal_text_layer, base);
    return text_layer->sb.lines_count;
}

/* Reallocate view_staging if the requested cell count outgrew capacity.
 * Returns 1 on success. Caller writes cells * sizeof(VTermScreenCell) bytes. */
static int ensure_view_staging(struct yetty_yterm_terminal_text_layer *layer, size_t cells)
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
static void text_layer_build_view(struct yetty_yterm_terminal_text_layer *layer)
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
    VTermScreenCell blank;
    memset(&blank, 0, sizeof(blank));

    for (uint32_t gpu_y = 0; gpu_y < rows; gpu_y++) {
        uint32_t total_idx = layer->view_top_total_idx + gpu_y;
        VTermScreenCell *dst = layer->view_staging + (size_t)gpu_y * cols;

        if (total_idx < layer->sb.lines_count) {
            const struct yetty_yterm_text_sb_line_rec *rec = sb_arena_peek(&layer->sb, total_idx);
            /* Sanity-check the record. If anything looks off (uninit slot,
             * stale offset, negative cols), fill blank and shout. Without
             * this we segfault deep in memcpy with no breadcrumbs. */
            if (!rec || rec->cols <= 0 || rec->offset >= layer->sb.cells_cap ||
                (size_t)rec->cols * sizeof(VTermScreenCell) > layer->sb.cells_cap) {
                yerror("build_view: bogus rec total_idx=%u view_top=%u gpu_y=%u "
                       "lines_count=%u lines_cap=%u lines_tail=%u rec=%p "
                       "rec->cols=%d rec->offset=%zu cells_cap=%zu",
                       total_idx, layer->view_top_total_idx, gpu_y, layer->sb.lines_count,
                       layer->sb.lines_cap, layer->sb.lines_tail, (const void *)rec,
                       rec ? rec->cols : 0, rec ? rec->offset : (size_t)0, layer->sb.cells_cap);
                for (uint32_t c = 0; c < cols; c++) {
                    dst[c] = blank;
                }
                continue;
            }
            int copy = (rec->cols < (int)cols) ? rec->cols : (int)cols;
            sb_arena_read(&layer->sb, rec->offset, copy, dst);
            for (int c = copy; c < (int)cols; c++) {
                dst[c] = blank;
            }
        } else if (live) {
            uint32_t live_row = total_idx - layer->sb.lines_count;
            if (live_row < rows) {
                memcpy(dst, live + (size_t)live_row * cols, (size_t)cols * sizeof(VTermScreenCell));
            } else {
                for (uint32_t c = 0; c < cols; c++) {
                    dst[c] = blank;
                }
            }
        } else {
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
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);

    text_layer->view_active = active ? 1 : 0;
    text_layer->view_top_total_idx = view_top_total_idx;

    if (text_layer->view_active) {
        set_cursor_visible(&text_layer->rs, 0.0f);
    } else {
        set_cursor_visible(&text_layer->rs, text_layer->vterm_cursor_visible);
    }

    text_layer->base.dirty = 1;
    if (text_layer->base.request_render_fn) {
        text_layer->base.request_render_fn(text_layer->base.request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* vterm asks for a previously-pushed line back, e.g. when the screen grows
 * and rows above the cursor need to be backfilled. We hand back the most
 * recent stored line and drop it from our buffer. Width mismatches are
 * vterm's problem: it only reads up to min(stored_cols, target_cols) and
 * clears the rest itself (see screen.c sb_popline call site). */
static int on_sb_popline(int cols, VTermScreenCell *cells, void *user)
{
    struct yetty_yterm_terminal_text_layer *text_layer = user;
    int copy_cols = sb_arena_pop(&text_layer->sb, cells, cols);
    if (copy_cols == 0) {
        return 0;
    }
    ydebug("on_sb_popline: returned %d cols (target=%d) sb_count=%u", copy_cols, cols,
           text_layer->sb.lines_count);
    return 1;
}

/* Sort (anchor_row, anchor_col) and (head_row, head_col) into a reading-order
 * (start, end) pair. start always precedes end in top-down, left-to-right
 * stream order. */
static void text_layer_sel_sorted(const struct yetty_yterm_terminal_text_layer *layer,
                                  uint32_t *out_sr, uint32_t *out_sc, uint32_t *out_er,
                                  uint32_t *out_ec)
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
static uint32_t text_layer_codepoint_from_cell(struct yetty_yterm_terminal_text_layer *layer,
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
    struct yetty_yterm_terminal_text_layer *layer, uint32_t row, uint32_t start_col,
    uint32_t end_col, struct yetty_ycore_buffer *out)
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
    struct yetty_yterm_terminal_text_layer *text_layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_terminal_text_layer, base);

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
    struct yetty_ycore_void_result r =
        text_layer_append_row_slice(text_layer, sr, sc, cols, out);
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

static void text_layer_set_selection(struct yetty_yrender_terminal_layer *self, int active,
                                     uint32_t anchor_row, uint32_t anchor_col, uint32_t head_row,
                                     uint32_t head_col)
{
    struct yetty_yterm_terminal_text_layer *text_layer =
        container_of(self, struct yetty_yterm_terminal_text_layer, base);
    text_layer->sel_active = active;
    text_layer->sel_anchor_row = anchor_row;
    text_layer->sel_anchor_col = anchor_col;
    text_layer->sel_head_row = head_row;
    text_layer->sel_head_col = head_col;
    set_selection_state(&text_layer->rs, active, anchor_row, anchor_col, head_row, head_col);
    text_layer->base.dirty = 1;
}
