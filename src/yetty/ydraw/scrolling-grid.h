/* scrolling-grid.h — opaque grid for scrolling-canvas.
 *
 * Module-private header. Only scrolling-canvas.c and scrolling-grid.c
 * include this. The grid owns:
 *   - the dynamic line buffer (one grid_line per absolute canvas row),
 *   - the scrollbuffer + sb_offsets (eviction of off-window rows),
 *   - per-line prim/cell/ref/figure/font-attachment storage.
 *
 * scrolling-canvas calls into this module via the narrow API below; the
 * grid_line / line_buffer / drawable_ref / drawable_data types stay private to
 * scrolling-grid.c.
 */
#ifndef YETTY_YDRAW_SCROLLING_GRID_H
#define YETTY_YDRAW_SCROLLING_GRID_H

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/yfont/font-cache.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_scrolling_grid;
struct yetty_ydraw_figure;
struct yetty_ydraw_cell_source;

/* Result type for the grid pointer. */
YETTY_YRESULT_DECLARE(yetty_ydraw_scrolling_grid_ptr, struct yetty_ydraw_scrolling_grid *);

/* Glyph visitor — fires once per glyph primitive in the grid. The (x, y)
 * are absolute canvas pixel coordinates (rolling-row converted to abs y
 * using cell_h passed to for_each_glyph). */
typedef void (*yetty_ydraw_scrolling_grid_glyph_cb)(float x, float y, uint32_t glyph_idx,
                                                    int32_t font_slot, void *user);

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ydraw_scrolling_grid_ptr_result yetty_ydraw_scrolling_grid_create(void);

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_destroy(
    struct yetty_ydraw_scrolling_grid *grid, struct yetty_yfont_cache *font_cache);

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_clear(
    struct yetty_ydraw_scrolling_grid *grid, struct yetty_yfont_cache *font_cache);

/*===========================================================================
 * Inspection
 *===========================================================================*/

uint32_t yetty_ydraw_scrolling_grid_line_count(const struct yetty_ydraw_scrolling_grid *grid);

uint32_t yetty_ydraw_scrolling_grid_total_drawable_count(
    const struct yetty_ydraw_scrolling_grid *grid);

uint32_t yetty_ydraw_scrolling_grid_figure_count_in_window(
    const struct yetty_ydraw_scrolling_grid *grid, uint32_t top, uint32_t end);

struct yetty_ydraw_figure *yetty_ydraw_scrolling_grid_figure_in_window(
    const struct yetty_ydraw_scrolling_grid *grid, uint32_t top, uint32_t end, uint32_t index);

/* Widest cell_count among lines in the visible-window [top, end). Used by
 * the canvas to widen the GPU grid for over-wide rows. */
uint32_t yetty_ydraw_scrolling_grid_max_cell_count_in_window(
    const struct yetty_ydraw_scrolling_grid *grid, uint32_t top, uint32_t end);

/*===========================================================================
 * Mutation
 *===========================================================================*/

/* Grow the line buffer so index `min_count - 1` is addressable. New lines
 * are initialised empty. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_ensure_lines(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t min_count);

/* About to mutate line `idx` — if it had been evicted to the scrollbuffer,
 * restore the expanded form first. No-op for non-evicted lines. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_dirty_line(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t idx);

/* Push a primitive payload onto line `line_idx`. Returns the prim's local
 * index within that line on success. */
struct uint32_result yetty_ydraw_scrolling_grid_push_prim(struct yetty_ydraw_scrolling_grid *grid,
                                                          uint32_t line_idx, uint32_t rolling_row,
                                                          const float *data, uint32_t word_count);

/* Make sure line `line_idx` has at least `min_cells` cells allocated. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_ensure_cells(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t line_idx, uint32_t min_cells);

/* Append a cell ref to (line_idx, col). The ref points back at the prim
 * placed at `lines_ahead` rows down at local index `drawable_idx`. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_push_ref(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t line_idx, uint32_t col, uint16_t lines_ahead,
    uint16_t drawable_idx);

/* Attach a composite instance to line `line_idx`. The grid takes
 * ownership (destroys on grid_line_free / grid_clear). */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_push_figure(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t line_idx, struct yetty_ydraw_figure *figure);

/* Attach a font cache handle to line `target_row`. If the handle is
 * already attached to some other line, migrate (no refcount change);
 * otherwise the line takes a fresh cache ref. No-op when handle is the
 * default or invalid, or when target_row is 0. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_attach_font(
    struct yetty_ydraw_scrolling_grid *grid, struct yetty_yfont_cache *font_cache,
    yetty_yfont_cache_handle default_handle, yetty_yfont_cache_handle handle, uint32_t target_row);

/*===========================================================================
 * Scrollbuffer eviction / restore
 *===========================================================================*/

/* Walk every line strictly below rolling_row_0 and evict any that still
 * hold expanded content. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_evict_scrollback(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t rolling_row_0,
    struct yetty_yfont_cache *font_cache, uint32_t grid_cols);

/* Restore every evicted line in [first, last] (inclusive) into expanded
 * form. Idempotent for non-evicted lines. */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_restore_range(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t first, uint32_t last);

/*===========================================================================
 * GPU staging build
 *
 * `rebuild_grid_staging` writes the per-cell prim-ref lookup table into
 * (*out_buf, *out_capacity), reallocating as needed. The new
 * staging-word count is returned in `*out_count`. `effective_grid_cols`
 * is the column count to bake in (the canvas widens it for over-wide
 * lines via grid_max_cell_count_in_window).
 *
 * `build_drawable_staging` writes the per-prim payload table the same way.
 * Returns the total prim count (offset-table entries) via `*out_drawable_count`.
 *===========================================================================*/

/* `cell_source` (may be NULL) lets live-screen cells contribute anchor-row
 * refs stored on their libvterm rich_handle; `live_rolling_row_0` maps a
 * canvas row to a live vterm row. When cell_source is NULL only grid-local
 * per-cell refs are emitted (identical to the pre-merge behaviour). */
struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_rebuild_staging(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t window_top, uint32_t grid_rows,
    uint32_t effective_grid_cols, const struct yetty_ydraw_cell_source *cell_source,
    uint32_t live_rolling_row_0, uint32_t **out_buf, uint32_t *out_capacity, uint32_t *out_count);

struct yetty_ycore_void_result yetty_ydraw_scrolling_grid_build_drawable_staging(
    struct yetty_ydraw_scrolling_grid *grid, uint32_t **out_buf, uint32_t *out_capacity,
    uint32_t *out_count, uint32_t *out_drawable_count);

/*===========================================================================
 * Glyph iteration
 *===========================================================================*/

void yetty_ydraw_scrolling_grid_for_each_glyph(const struct yetty_ydraw_scrolling_grid *grid,
                                               float cell_h, yetty_ydraw_scrolling_grid_glyph_cb cb,
                                               void *user);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_SCROLLING_GRID_H */
