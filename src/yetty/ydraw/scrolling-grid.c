/* scrolling-grid.c — opaque grid implementation for scrolling-canvas.
 *
 * Owns the line buffer, scrollbuffer, sb_offsets, and all per-line
 * grid_line storage (prims, cells, refs, fonts, figures, arena). The
 * scrolling-canvas only ever sees the opaque pointer.
 */

#include "scrolling-grid.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw/scrollbuffer.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Constants — kept module-private. (Previously also referenced from
 * canvas-internal.h; now scoped here since the grid is the sole user.)
 *===========================================================================*/

/* Glyph primitive type (not in ysdf types.gen.h since not SDF). */
#define YETTY_YSDF_GLYPH 200
/* Glyph prim layout: type, z_order, x, y, font_size,
 * packed(glyph_idx | font_id), color */
#define YDRAW_GLYPH_WORDS 7

#define YDRAW_CANVAS_INITIAL_LINE_CAPACITY 64
#define YDRAW_CANVAS_INITIAL_CELL_CAPACITY 16
#define YDRAW_CANVAS_INITIAL_PRIM_CAPACITY 16
#define YDRAW_CANVAS_INITIAL_REF_CAPACITY 2

#define SB_OFFSET_UNSET 0xFFFFFFFFu

/* Cap on the GPU grid width — clip beyond this so a buggy/malicious
 * producer can't blow up staging. */
#define YDRAW_GRID_COLS_MAX 4096u

/*===========================================================================
 * Private types
 *===========================================================================*/

struct drawable_ref {
    uint16_t lines_ahead;
    uint16_t drawable_index;
};

struct drawable_ref_array {
    struct drawable_ref *data;
    uint32_t count;
    uint32_t capacity;
};

struct grid_cell {
    struct drawable_ref_array refs;
};

struct drawable_data {
    uint32_t rolling_row;
    uint32_t arena_offset;
    uint32_t word_count;
};

struct drawable_data_array {
    struct drawable_data *data;
    uint32_t count;
    uint32_t capacity;
};

struct font_entry {
    yetty_yfont_cache_handle handle;
};

struct grid_line {
    struct drawable_data_array prims;

    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;

    struct grid_cell *cells;
    uint32_t cell_count;
    uint32_t cell_capacity;

    struct font_entry *fonts;
    uint32_t font_count;
    uint32_t font_capacity;

    struct yetty_ydraw_figure **figures;
    uint32_t figure_count;
    uint32_t figure_capacity;
};

struct yetty_ydraw_scrolling_grid {
    struct grid_line *lines;
    uint32_t lines_count;
    uint32_t lines_capacity;

    struct yetty_ydraw_scrollbuffer scrollbuffer;
    uint32_t *sb_offsets;
    uint32_t sb_offsets_count;
    uint32_t sb_offsets_capacity;
};

/*===========================================================================
 * Small helpers — drawable_ref_array, drawable_data_array, grid_line
 *===========================================================================*/

static void drawable_ref_array_init(struct drawable_ref_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void drawable_ref_array_free(struct drawable_ref_array *arr)
{
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static struct yetty_ycore_void_result drawable_ref_array_push(struct drawable_ref_array *arr,
                                                              struct drawable_ref ref)
{
    if (arr->count >= arr->capacity) {
        uint32_t new_cap =
            arr->capacity == 0 ? YDRAW_CANVAS_INITIAL_REF_CAPACITY : arr->capacity * 2;
        struct drawable_ref *grown = realloc(arr->data, new_cap * sizeof(struct drawable_ref));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "drawable_ref_array_push: realloc failed");
        }
        arr->data = grown;
        arr->capacity = new_cap;
    }
    arr->data[arr->count++] = ref;
    return YETTY_OK_VOID();
}

static void drawable_data_array_init(struct drawable_data_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void drawable_data_array_free(struct drawable_data_array *arr)
{
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static struct yetty_ycore_void_result grid_line_init(struct grid_line *line)
{
    drawable_data_array_init(&line->prims);
    line->arena = NULL;
    line->arena_count = 0;
    line->arena_capacity = 0;
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;
    line->cells = NULL;
    line->cell_count = 0;
    line->cell_capacity = 0;
    line->figures = NULL;
    line->figure_count = 0;
    line->figure_capacity = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_line_free(struct grid_line *line,
                                                     struct yetty_yfont_cache *cache)
{
    drawable_data_array_free(&line->prims);
    free(line->arena);
    line->arena = NULL;
    line->arena_count = 0;
    line->arena_capacity = 0;
    for (uint32_t i = 0; i < line->font_count; i++) {
        if (cache) {
            yetty_yfont_cache_release_font(cache, line->fonts[i].handle);
        }
    }
    free(line->fonts);
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;
    for (uint32_t i = 0; i < line->figure_count; i++) {
        yetty_ydraw_figure_destroy(line->figures[i]);
    }
    free(line->figures);
    line->figures = NULL;
    line->figure_count = 0;
    line->figure_capacity = 0;
    for (uint32_t i = 0; i < line->cell_count; i++) {
        drawable_ref_array_free(&line->cells[i].refs);
    }
    free(line->cells);
    line->cells = NULL;
    line->cell_count = 0;
    line->cell_capacity = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_line_ensure_cells(struct grid_line *line,
                                                             uint32_t min_cells)
{
    if (min_cells <= line->cell_capacity) {
        if (min_cells > line->cell_count) {
            for (uint32_t i = line->cell_count; i < min_cells; i++) {
                drawable_ref_array_init(&line->cells[i].refs);
            }
            line->cell_count = min_cells;
        }
        return YETTY_OK_VOID();
    }

    uint32_t new_cap =
        line->cell_capacity == 0 ? YDRAW_CANVAS_INITIAL_CELL_CAPACITY : line->cell_capacity;
    while (new_cap < min_cells) {
        new_cap *= 2;
    }

    struct grid_cell *new_cells = realloc(line->cells, new_cap * sizeof(struct grid_cell));
    if (!new_cells) {
        return YETTY_ERR(yetty_ycore_void, "realloc failed for grid cells");
    }
    line->cells = new_cells;
    for (uint32_t i = line->cell_capacity; i < new_cap; i++) {
        drawable_ref_array_init(&line->cells[i].refs);
    }
    line->cell_capacity = new_cap;
    line->cell_count = min_cells;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_line_arena_append(struct grid_line *line,
                                                             const float *data, uint32_t word_count,
                                                             uint32_t *out_offset)
{
    if (line->arena_count + word_count > line->arena_capacity) {
        uint32_t new_cap = line->arena_capacity ? line->arena_capacity : 32;
        while (new_cap < line->arena_count + word_count) {
            new_cap *= 2;
        }
        uint32_t *new_arena = realloc(line->arena, new_cap * sizeof(uint32_t));
        if (!new_arena) {
            return YETTY_ERR(yetty_ycore_void, "realloc failed for prim arena");
        }
        line->arena = new_arena;
        line->arena_capacity = new_cap;
    }
    *out_offset = line->arena_count;
    memcpy(line->arena + line->arena_count, data, word_count * sizeof(uint32_t));
    line->arena_count += word_count;
    return YETTY_OK_VOID();
}

static struct uint32_result grid_line_push_drawable_internal(struct grid_line *line,
                                                             uint32_t rolling_row,
                                                             const float *data, uint32_t word_count)
{
    struct drawable_data_array *arr = &line->prims;
    if (arr->count >= arr->capacity) {
        uint32_t new_cap =
            arr->capacity == 0 ? YDRAW_CANVAS_INITIAL_PRIM_CAPACITY : arr->capacity * 2;
        struct drawable_data *grown = realloc(arr->data, new_cap * sizeof(struct drawable_data));
        if (!grown) {
            return YETTY_ERR(uint32, "grid_line_push_prim: prims realloc failed");
        }
        arr->data = grown;
        arr->capacity = new_cap;
    }
    uint32_t offset = 0;
    struct yetty_ycore_void_result ar = grid_line_arena_append(line, data, word_count, &offset);
    YETTY_RETURN_IF_ERR(uint32, ar, "grid_line_push_prim: arena append");
    uint32_t idx = arr->count++;
    arr->data[idx].rolling_row = rolling_row;
    arr->data[idx].arena_offset = offset;
    arr->data[idx].word_count = word_count;
    return YETTY_OK(uint32, idx);
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ydraw_scrolling_grid_ptr_result yetty_ydraw_scrolling_grid_create(void)
{
    struct yetty_ydraw_scrolling_grid *grid = calloc(1, sizeof(struct yetty_ydraw_scrolling_grid));
    if (!grid) {
        return YETTY_ERR(yetty_ydraw_scrolling_grid_ptr, "scrolling-grid: alloc failed");
    }
    yetty_ydraw_scrollbuffer_init(&grid->scrollbuffer);
    return YETTY_OK(yetty_ydraw_scrolling_grid_ptr, grid);
}

static struct yetty_ycore_void_result grid_free_all_lines(struct yetty_ydraw_scrolling_grid *grid,
                                                          struct yetty_yfont_cache *font_cache)
{
    for (uint32_t i = 0; i < grid->lines_count; i++) {
        struct yetty_ycore_void_result r = grid_line_free(&grid->lines[i], font_cache);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
    }
    free(grid->lines);
    grid->lines = NULL;
    grid->lines_count = 0;
    grid->lines_capacity = 0;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_destroy(
    struct yetty_ydraw_scrolling_grid *grid, struct yetty_yfont_cache *font_cache)
{
    if (!grid) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = grid_free_all_lines(grid, font_cache);
    if (YETTY_IS_ERR(r)) {
        return r;
    }
    yetty_ydraw_scrollbuffer_free(&grid->scrollbuffer);
    free(grid->sb_offsets);
    free(grid);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_clear(
    struct yetty_ydraw_scrolling_grid *grid, struct yetty_yfont_cache *font_cache)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }
    struct yetty_ycore_void_result r = grid_free_all_lines(grid, font_cache);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "scrolling-grid clear: free_all_lines");

    yetty_ydraw_scrollbuffer_free(&grid->scrollbuffer);
    yetty_ydraw_scrollbuffer_init(&grid->scrollbuffer);
    free(grid->sb_offsets);
    grid->sb_offsets = NULL;
    grid->sb_offsets_count = 0;
    grid->sb_offsets_capacity = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Inspection
 *===========================================================================*/

uint32_t yetty_ydraw_scrolling_grid_line_count(const struct yetty_ydraw_scrolling_grid *grid)
{
    return grid ? grid->lines_count : 0;
}

uint32_t yetty_ydraw_scrolling_grid_total_drawable_count(
    const struct yetty_ydraw_scrolling_grid *grid)
{
    if (!grid) {
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < grid->lines_count; i++) {
        count += grid->lines[i].prims.count;
    }
    return count;
}

uint32_t yetty_ydraw_scrolling_grid_figure_count_in_window(
    const struct yetty_ydraw_scrolling_grid *grid, uint32_t top, uint32_t end)
{
    if (!grid) {
        return 0;
    }
    uint32_t e = end > grid->lines_count ? grid->lines_count : end;
    uint32_t count = 0;
    for (uint32_t i = top; i < e; i++) {
        count += grid->lines[i].figure_count;
    }
    return count;
}

struct yetty_ydraw_figure *yetty_ydraw_scrolling_grid_figure_in_window(
    const struct yetty_ydraw_scrolling_grid *grid, uint32_t top, uint32_t end, uint32_t index)
{
    if (!grid) {
        return NULL;
    }
    uint32_t e = end > grid->lines_count ? grid->lines_count : end;
    uint32_t current = 0;
    for (uint32_t i = top; i < e; i++) {
        const struct grid_line *line = &grid->lines[i];
        if (index < current + line->figure_count) {
            return line->figures[index - current];
        }
        current += line->figure_count;
    }
    return NULL;
}

uint32_t yetty_ydraw_scrolling_grid_max_cell_count_in_window(
    const struct yetty_ydraw_scrolling_grid *grid, uint32_t top, uint32_t end)
{
    if (!grid) {
        return 0;
    }
    uint32_t e = end > grid->lines_count ? grid->lines_count : end;
    uint32_t max_cells = 0;
    for (uint32_t i = top; i < e; i++) {
        if (grid->lines[i].cell_count > max_cells) {
            max_cells = grid->lines[i].cell_count;
        }
    }
    return max_cells;
}

/*===========================================================================
 * Mutation
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_ensure_lines(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t min_count)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }
    if (min_count > grid->lines_capacity) {
        uint32_t new_cap =
            grid->lines_capacity == 0 ? YDRAW_CANVAS_INITIAL_LINE_CAPACITY : grid->lines_capacity;
        while (new_cap < min_count) {
            new_cap *= 2;
        }
        struct grid_line *new_lines = realloc(grid->lines, new_cap * sizeof(struct grid_line));
        if (!new_lines) {
            return YETTY_ERR(yetty_ycore_void, "realloc failed for line buffer");
        }
        grid->lines = new_lines;
        grid->lines_capacity = new_cap;
    }
    while (grid->lines_count < min_count) {
        struct yetty_ycore_void_result r = grid_line_init(&grid->lines[grid->lines_count]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ensure_lines: grid_line_init");
        grid->lines_count++;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Scrollbuffer machinery — eviction/restore
 *===========================================================================*/

static struct yetty_ycore_void_result sb_offsets_ensure(struct yetty_ydraw_scrolling_grid *grid,
                                                        uint32_t min_count)
{
    if (min_count <= grid->sb_offsets_count) {
        return YETTY_OK_VOID();
    }
    if (min_count > grid->sb_offsets_capacity) {
        uint32_t new_cap = grid->sb_offsets_capacity ? grid->sb_offsets_capacity : 64u;
        while (new_cap < min_count) {
            new_cap *= 2;
        }
        uint32_t *grown = realloc(grid->sb_offsets, new_cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "sb_offsets: realloc failed");
        }
        grid->sb_offsets = grown;
        grid->sb_offsets_capacity = new_cap;
    }
    for (uint32_t i = grid->sb_offsets_count; i < min_count; i++) {
        grid->sb_offsets[i] = SB_OFFSET_UNSET;
    }
    grid->sb_offsets_count = min_count;
    return YETTY_OK_VOID();
}

/* Word-count lookup for the scrollbuffer decoder. */
static uint32_t grid_sb_word_count_fn(uint32_t type_word, void *ctx)
{
    (void)ctx;
    if (type_word == YETTY_YSDF_GLYPH) {
        return YDRAW_GLYPH_WORDS;
    }
    /* Every ysdf primitive type (box, segment, circle, …) is a high-range
     * tag (0x7FFFFFxx). yetty_ysdf_word_count returns the record's word
     * count for a known tag and 0 for anything else, so it doubles as the
     * validity gate — no separate range check is needed (a stale range here
     * silently rejected box/segment lines on scrollback restore). */
    uint32_t wc = yetty_ysdf_word_count((enum yetty_ysdf_type)type_word);
    if (wc > 0) {
        return wc;
    }
    return 0;
}

struct sb_restore_ctx {
    struct grid_line *line;
};

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_header(void *ctx, uint32_t line_rolling_row,
                                                           uint32_t drawable_count)
{
    (void)ctx;
    (void)line_rolling_row;
    (void)drawable_count;
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_cell(
    void *ctx, uint32_t col, const struct yetty_ydraw_scrollbuffer_ref *refs, uint32_t ref_count)
{
    struct sb_restore_ctx *r = ctx;
    struct yetty_ycore_void_result er = grid_line_ensure_cells(r->line, col + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "restore: ensure_cells");
    for (uint32_t i = 0; i < ref_count; i++) {
        struct drawable_ref pr = {
            .lines_ahead = refs[i].lines_ahead,
            .drawable_index = refs[i].drawable_idx,
        };
        struct yetty_ycore_void_result rp = drawable_ref_array_push(&r->line->cells[col].refs, pr);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rp, "restore: ref_array_push");
    }
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_prim(void *ctx, uint32_t rolling_row,
                                                         const uint32_t *payload,
                                                         uint32_t word_count)
{
    struct sb_restore_ctx *r = ctx;
    struct uint32_result push_res =
        grid_line_push_drawable_internal(r->line, rolling_row, (const float *)payload, word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, push_res, "restore: push_prim");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_restore_line(struct yetty_ydraw_scrolling_grid *grid,
                                                        uint32_t idx)
{
    if (idx >= grid->sb_offsets_count || grid->sb_offsets[idx] == SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }
    if (idx >= grid->lines_count) {
        return YETTY_OK_VOID();
    }
    struct grid_line *line = &grid->lines[idx];
    if (line->prims.count > 0 || line->cell_count > 0) {
        return YETTY_OK_VOID();
    }

    struct sb_restore_ctx rctx = {.line = line};
    struct yetty_ydraw_scrollbuffer_decode_sinks sinks = {
        .ctx = &rctx,
        .on_header = sb_restore_on_header,
        .on_cell = sb_restore_on_cell,
        .on_prim = sb_restore_on_prim,
    };
    struct yetty_ydraw_scrollbuffer_offset_result dec = yetty_ydraw_scrollbuffer_decode_line(
        &grid->scrollbuffer, grid->sb_offsets[idx], grid_sb_word_count_fn, NULL, &sinks);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dec, "grid_restore_line: decode failed");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_dirty_line(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t idx)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }
    if (idx >= grid->sb_offsets_count || grid->sb_offsets[idx] == SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = grid_restore_line(grid, idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "dirty_line: restore_line");
    grid->sb_offsets[idx] = SB_OFFSET_UNSET;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_restore_range(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t first, uint32_t last)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }
    for (uint32_t i = first; i <= last; i++) {
        struct yetty_ycore_void_result r = grid_restore_line(grid, i);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "restore_range: restore_line");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_evict_line(struct yetty_ydraw_scrolling_grid *grid,
                                                      uint32_t idx,
                                                      struct yetty_yfont_cache *font_cache,
                                                      uint32_t grid_cols)
{
    if (idx >= grid->lines_count) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result er = sb_offsets_ensure(grid, idx + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "evict: ensure offsets");
    if (grid->sb_offsets[idx] != SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }

    struct grid_line *line = &grid->lines[idx];

    /* Build cell view. */
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
            cells[n_cells].refs =
                (const struct yetty_ydraw_scrollbuffer_ref *)line->cells[c].refs.data;
            n_cells++;
        }
    }

    /* Build prim view. */
    struct yetty_ydraw_scrollbuffer_prim *prims = NULL;
    uint32_t n_prims = line->prims.count;
    if (n_prims > 0) {
        prims = malloc(n_prims * sizeof(*prims));
        if (!prims) {
            free(cells);
            return YETTY_ERR(yetty_ycore_void, "evict: prim view alloc failed");
        }
        for (uint32_t p = 0; p < n_prims; p++) {
            struct drawable_data *pd = &line->prims.data[p];
            prims[p].rolling_row = pd->rolling_row;
            prims[p].word_count = pd->word_count;
            prims[p].payload = line->arena + pd->arena_offset;
        }
    }

    struct yetty_ydraw_scrollbuffer_offset_result enc = yetty_ydraw_scrollbuffer_encode_line(
        &grid->scrollbuffer, idx, grid_cols, cells, n_cells, prims, n_prims);
    free(cells);
    free(prims);
    if (YETTY_IS_ERR(enc)) {
        return YETTY_ERR(yetty_ycore_void, "evict: encode failed", enc);
    }
    grid->sb_offsets[idx] = (uint32_t)enc.value;

    /* Save figures and font handles before grid_line_free wipes them. */
    struct yetty_ydraw_figure **saved_cp = line->figures;
    uint32_t saved_cp_count = line->figure_count;
    uint32_t saved_cp_cap = line->figure_capacity;
    line->figures = NULL;
    line->figure_count = 0;
    line->figure_capacity = 0;

    struct font_entry *saved_fonts = line->fonts;
    uint32_t saved_font_count = line->font_count;
    uint32_t saved_font_capacity = line->font_capacity;
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;

    struct yetty_ycore_void_result fr = grid_line_free(line, font_cache);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "evict: grid_line_free");
    struct yetty_ycore_void_result ir = grid_line_init(line);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "evict: grid_line_init");

    line->figures = saved_cp;
    line->figure_count = saved_cp_count;
    line->figure_capacity = saved_cp_cap;
    line->fonts = saved_fonts;
    line->font_count = saved_font_count;
    line->font_capacity = saved_font_capacity;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_evict_scrollback(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t rolling_row_0,
    struct yetty_yfont_cache *font_cache, uint32_t grid_cols)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }
    if (rolling_row_0 == 0) {
        return YETTY_OK_VOID();
    }

    uint32_t end = rolling_row_0;
    if (end > grid->lines_count) {
        end = grid->lines_count;
    }

    for (uint32_t i = 0; i < end; i++) {
        if (i < grid->sb_offsets_count && grid->sb_offsets[i] != SB_OFFSET_UNSET) {
            /* Already serialised. Re-free the expanded form if any
             * (could be restored), preserving figures + font attachments. */
            struct grid_line *line = &grid->lines[i];
            if (line->prims.count > 0 || line->cell_count > 0 || line->font_count > 0) {
                struct yetty_ydraw_figure **saved_cp = line->figures;
                uint32_t saved_cp_count = line->figure_count;
                uint32_t saved_cp_cap = line->figure_capacity;
                line->figures = NULL;
                line->figure_count = 0;
                line->figure_capacity = 0;

                struct font_entry *saved_fonts = line->fonts;
                uint32_t saved_font_count = line->font_count;
                uint32_t saved_font_capacity = line->font_capacity;
                line->fonts = NULL;
                line->font_count = 0;
                line->font_capacity = 0;

                struct yetty_ycore_void_result fr = grid_line_free(line, font_cache);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "evict_scrollback: re-free line");
                struct yetty_ycore_void_result ir = grid_line_init(line);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "evict_scrollback: re-init line");

                line->figures = saved_cp;
                line->figure_count = saved_cp_count;
                line->figure_capacity = saved_cp_cap;
                line->fonts = saved_fonts;
                line->font_count = saved_font_count;
                line->font_capacity = saved_font_capacity;
            }
            continue;
        }
        struct yetty_ycore_void_result r = grid_evict_line(grid, i, font_cache, grid_cols);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "evict_scrollback: evict_line");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Mutation API (post-restore)
 *
 * push_prim / push_ref / push_figure all assume the caller has already
 * called dirty_line(idx) so the line is in expanded form.
 *===========================================================================*/

struct uint32_result yetty_ydraw_scrolling_grid_push_prim(struct yetty_ydraw_scrolling_grid *grid,
                                                          uint32_t line_idx, uint32_t rolling_row,
                                                          const float *data, uint32_t word_count)
{
    if (!grid || line_idx >= grid->lines_count) {
        return YETTY_ERR(uint32, "push_prim: line_idx out of range");
    }
    return grid_line_push_drawable_internal(&grid->lines[line_idx], rolling_row, data, word_count);
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_ensure_cells(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t line_idx, uint32_t min_cells)
{
    if (!grid || line_idx >= grid->lines_count) {
        return YETTY_ERR(yetty_ycore_void, "ensure_cells: line_idx out of range");
    }
    return grid_line_ensure_cells(&grid->lines[line_idx], min_cells);
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_push_ref(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t line_idx, uint32_t col, uint16_t lines_ahead,
    uint16_t drawable_idx)
{
    if (!grid || line_idx >= grid->lines_count) {
        return YETTY_ERR(yetty_ycore_void, "push_ref: line_idx out of range");
    }
    struct grid_line *line = &grid->lines[line_idx];
    if (col >= line->cell_count) {
        return YETTY_ERR(yetty_ycore_void, "push_ref: col out of range");
    }
    struct drawable_ref ref = {lines_ahead, drawable_idx};
    return drawable_ref_array_push(&line->cells[col].refs, ref);
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_push_figure(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t line_idx, struct yetty_ydraw_figure *figure)
{
    if (!grid || line_idx >= grid->lines_count) {
        return YETTY_ERR(yetty_ycore_void, "push_figure: line_idx out of range");
    }
    struct grid_line *line = &grid->lines[line_idx];
    if (line->figure_count >= line->figure_capacity) {
        uint32_t new_cap = line->figure_capacity == 0 ? 4 : line->figure_capacity * 2;
        struct yetty_ydraw_figure **grown =
            realloc(line->figures, new_cap * sizeof(struct yetty_ydraw_figure *));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "push_figure: realloc failed");
        }
        line->figures = grown;
        line->figure_capacity = new_cap;
    }
    line->figures[line->figure_count++] = figure;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_attach_font(
    struct yetty_ydraw_scrolling_grid *grid, struct yetty_yfont_cache *font_cache,
    yetty_yfont_cache_handle default_handle, yetty_yfont_cache_handle handle, uint32_t target_row)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }
    if (handle == YETTY_YFONT_CACHE_HANDLE_INVALID || handle == default_handle || target_row == 0) {
        return YETTY_OK_VOID();
    }
    if (target_row >= grid->lines_count) {
        return YETTY_ERR(yetty_ycore_void, "attach_font: target_row out of range");
    }
    struct grid_line *target = &grid->lines[target_row];

    /* Look for an existing attachment to migrate. */
    for (uint32_t li = 0; li < grid->lines_count; li++) {
        struct grid_line *l = &grid->lines[li];
        for (uint32_t fi = 0; fi < l->font_count; fi++) {
            if (l->fonts[fi].handle == handle) {
                if (li == target_row) {
                    return YETTY_OK_VOID();
                }
                if (target->font_count >= target->font_capacity) {
                    uint32_t new_cap = target->font_capacity == 0 ? 4 : target->font_capacity * 2;
                    struct font_entry *grown =
                        realloc(target->fonts, new_cap * sizeof(struct font_entry));
                    if (!grown) {
                        return YETTY_ERR(yetty_ycore_void, "attach_font: realloc fonts (migrate)");
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

    /* New attachment — take a fresh cache ref. */
    if (target->font_count >= target->font_capacity) {
        uint32_t new_cap = target->font_capacity == 0 ? 4 : target->font_capacity * 2;
        struct font_entry *grown = realloc(target->fonts, new_cap * sizeof(struct font_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "attach_font: realloc fonts (new)");
        }
        target->fonts = grown;
        target->font_capacity = new_cap;
    }
    yetty_yfont_cache_retain(font_cache, handle);
    target->fonts[target->font_count++].handle = handle;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * GPU staging build
 *===========================================================================*/

#define YDRAW_CANVAS_INITIAL_STAGING_CAPACITY 4096

static struct yetty_ycore_void_result ensure_staging_cap(uint32_t **buf, uint32_t *cap,
                                                         uint32_t min_size)
{
    if (min_size <= *cap) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = *cap == 0 ? YDRAW_CANVAS_INITIAL_STAGING_CAPACITY : *cap;
    while (new_cap < min_size) {
        new_cap *= 2;
    }
    uint32_t *grown = realloc(*buf, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "staging realloc failed");
    }
    *buf = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_rebuild_staging(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t window_top, uint32_t grid_rows,
    uint32_t effective_grid_cols, uint32_t **out_buf, uint32_t *out_capacity, uint32_t *out_count)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }

    /* Restore evicted lines that fall in the visible window. */
    {
        uint32_t window_last = window_top + grid_rows;
        if (window_last > grid->lines_count) {
            window_last = grid->lines_count;
        }
        if (window_last > window_top) {
            struct yetty_ycore_void_result rr =
                yetty_ydraw_scrolling_grid_restore_range(grid, window_top, window_last - 1u);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rebuild_staging: restore_range");

            /* A multi-row drawable (table grid line, code panel, image,
             * figure …) is stored on its BOTTOM row and referenced from the
             * rows above via lines_ahead. When that bottom row sits below the
             * visible window — in the still-evicted scrollback region — its
             * prim is gone, so the in-window cell refs index a stale drawable
             * and the element renders partially / garbled. Scan the restored
             * window for the lowest anchor any in-window cell points at and
             * restore those below-window lines too, so the prefix-sum and the
             * drawable staging agree. */
            uint32_t max_anchor = window_last - 1u;
            for (uint32_t canvas_y = window_top; canvas_y < window_last; canvas_y++) {
                const struct grid_line *line = &grid->lines[canvas_y];
                for (uint32_t x = 0; x < line->cell_count; x++) {
                    const struct drawable_ref_array *refs = &line->cells[x].refs;
                    for (uint32_t ri = 0; ri < refs->count; ri++) {
                        uint32_t bl = canvas_y + refs->data[ri].lines_ahead;
                        if (bl < grid->lines_count && bl > max_anchor) {
                            max_anchor = bl;
                        }
                    }
                }
            }
            if (max_anchor >= window_last) {
                struct yetty_ycore_void_result ar =
                    yetty_ydraw_scrolling_grid_restore_range(grid, window_last, max_anchor);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ar,
                                    "rebuild_staging: restore_range (below-window anchors)");
            }
        }
    }

    /* Prefix-sum of prim counts across ALL lines. */
    uint32_t total_prims = 0;
    uint32_t *line_base_drawable_idx = NULL;
    if (grid->lines_count > 0) {
        line_base_drawable_idx = malloc(grid->lines_count * sizeof(uint32_t));
        if (!line_base_drawable_idx) {
            return YETTY_ERR(yetty_ycore_void, "rebuild_staging: line_base alloc");
        }
        for (uint32_t i = 0; i < grid->lines_count; i++) {
            line_base_drawable_idx[i] = total_prims;
            total_prims += grid->lines[i].prims.count;
        }
    }

    uint32_t grid_w = effective_grid_cols;
    if (grid_w > YDRAW_GRID_COLS_MAX) {
        grid_w = YDRAW_GRID_COLS_MAX;
    }

    if (grid_w == 0 || grid_rows == 0) {
        *out_count = 0;
        free(line_base_drawable_idx);
        return YETTY_OK_VOID();
    }

    uint32_t num_cells = grid_w * grid_rows;
    struct yetty_ycore_void_result es = ensure_staging_cap(out_buf, out_capacity, num_cells * 4);
    if (YETTY_IS_ERR(es)) {
        free(line_base_drawable_idx);
        return YETTY_ERR(yetty_ycore_void, "rebuild_staging: ensure_cap", es);
    }
    uint32_t count = num_cells;

    for (uint32_t gpu_y = 0; gpu_y < grid_rows; gpu_y++) {
        uint32_t canvas_y = window_top + gpu_y;
        bool has_line = canvas_y < grid->lines_count;
        struct grid_line *line = has_line ? &grid->lines[canvas_y] : NULL;
        uint32_t line_cell_count = line ? line->cell_count : 0;

        for (uint32_t x = 0; x < grid_w; x++) {
            uint32_t cell_idx = gpu_y * grid_w + x;

            struct yetty_ycore_void_result e1 =
                ensure_staging_cap(out_buf, out_capacity, count + 2);
            if (YETTY_IS_ERR(e1)) {
                free(line_base_drawable_idx);
                return YETTY_ERR(yetty_ycore_void, "rebuild_staging: ensure_cap (h)", e1);
            }

            (*out_buf)[cell_idx] = count;
            uint32_t count_pos = count++;
            (*out_buf)[count_pos] = 0;
            uint32_t cell_count = 0;

            if (has_line && x < line_cell_count) {
                struct grid_cell *cell = &line->cells[x];
                for (uint32_t ri = 0; ri < cell->refs.count; ri++) {
                    struct drawable_ref *ref = &cell->refs.data[ri];
                    uint32_t bl = canvas_y + ref->lines_ahead;
                    if (bl < grid->lines_count && line_base_drawable_idx) {
                        struct yetty_ycore_void_result e2 =
                            ensure_staging_cap(out_buf, out_capacity, count + 1);
                        if (YETTY_IS_ERR(e2)) {
                            free(line_base_drawable_idx);
                            return YETTY_ERR(yetty_ycore_void, "rebuild_staging: ensure_cap (r)",
                                             e2);
                        }
                        (*out_buf)[count++] = line_base_drawable_idx[bl] + ref->drawable_index;
                        cell_count++;
                    }
                }
            }
            (*out_buf)[count_pos] = cell_count;
        }
    }

    free(line_base_drawable_idx);
    *out_count = count;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_build_drawable_staging(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t **out_buf, uint32_t *out_capacity,
    uint32_t *out_count, uint32_t *out_drawable_count)
{
    if (!grid) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-grid: NULL");
    }

    uint32_t drawable_count = 0;
    uint32_t total_words = 0;
    for (uint32_t i = 0; i < grid->lines_count; i++) {
        const struct grid_line *line = &grid->lines[i];
        for (uint32_t p = 0; p < line->prims.count; p++) {
            drawable_count++;
            total_words += line->prims.data[p].word_count + 1;
        }
    }
    if (drawable_count == 0) {
        *out_count = 0;
        *out_drawable_count = 0;
        return YETTY_OK_VOID();
    }
    uint32_t total_size = drawable_count + total_words;
    struct yetty_ycore_void_result e = ensure_staging_cap(out_buf, out_capacity, total_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, e, "build_drawable_staging: ensure_cap");

    uint32_t data_offset = 0;
    uint32_t drawable_idx = 0;
    for (uint32_t i = 0; i < grid->lines_count; i++) {
        struct grid_line *line = &grid->lines[i];
        for (uint32_t p = 0; p < line->prims.count; p++) {
            struct drawable_data *prim = &line->prims.data[p];
            (*out_buf)[drawable_idx] = data_offset;
            (*out_buf)[drawable_count + data_offset] = prim->rolling_row;
            const uint32_t *payload = line->arena + prim->arena_offset;
            memcpy(&(*out_buf)[drawable_count + data_offset + 1], payload,
                   prim->word_count * sizeof(uint32_t));
            data_offset += prim->word_count + 1;
            drawable_idx++;
        }
    }
    *out_count = total_size;
    *out_drawable_count = drawable_count;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Glyph iteration
 *===========================================================================*/

void yetty_ydraw_scrolling_grid_for_each_glyph(const struct yetty_ydraw_scrolling_grid *grid,
                                               float cell_h, yetty_ydraw_scrolling_grid_glyph_cb cb,
                                               void *user)
{
    if (!grid || !cb) {
        return;
    }
    for (uint32_t li = 0; li < grid->lines_count; li++) {
        const struct grid_line *line = &grid->lines[li];
        for (uint32_t pi = 0; pi < line->prims.count; pi++) {
            const struct drawable_data *pd = &line->prims.data[pi];
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

            float abs_y = gy_rel + (float)pd->rolling_row * cell_h;
            uint32_t glyph_idx = packed & 0xFFFFu;
            uint32_t slot_plus_one = (packed >> 16) & 0xFFFFu;
            int32_t font_slot = slot_plus_one ? (int32_t)(slot_plus_one - 1) : -1;
            cb(gx, abs_y, glyph_idx, font_slot, user);
        }
    }
}
