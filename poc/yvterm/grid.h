/*
 * poc/yvterm/grid.h — the text grid as a collection of lines, driven from the
 * libvterm STATE layer.
 *
 * This is the heart of the yvterm performance probe. No VTermScreen, no
 * parallel scrollback. The model is a collection of independent lines, each
 * owning its own row of 16-byte cells. The renderer (main.c) walks the lines,
 * uploads each dirty line individually into one pinned GPU storage buffer, and
 * issues a single full-screen-quad draw. No yrender binder / resource-set /
 * render-target.
 *
 * The vterm-state -> cell logic and the 16-byte (4 u32) cell packing are kept
 * verbatim from the original probe; only the storage and rendering changed.
 */
#ifndef POC_YVTERM_GRID_H
#define POC_YVTERM_GRID_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfont/ms-font.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Four u32 words per cell — the stride text.wgsl assumes. */
enum { POC_YVTERM_WORDS_PER_CELL = 4 };

/* One terminal line: its own contiguous row of `cols * 4` u32 cells, plus a
 * dirty flag the renderer clears once it has uploaded the row. */
struct poc_line {
    uint32_t *cells; /* cols * POC_YVTERM_WORDS_PER_CELL u32, one row */
    int dirty;
};

struct poc_yvterm_grid;

YETTY_YRESULT_DECLARE(poc_yvterm_grid_ptr, struct poc_yvterm_grid *);

/* Create the grid. Borrows `font` (caller keeps ownership / destroys it after
 * the grid). `cols` x `visible_rows` is the vterm size; `total_rows` is the
 * number of line objects backing the GPU buffer (>= visible_rows; the extra
 * rows let --rows N inflate the per-frame upload count). The cell size is
 * taken from the font. */
struct poc_yvterm_grid_ptr_result poc_yvterm_grid_create(uint32_t cols, uint32_t visible_rows,
                                                         uint32_t total_rows,
                                                         struct yetty_yfont_ms_font *font);

struct yetty_ycore_void_result poc_yvterm_grid_destroy(struct poc_yvterm_grid *grid);

/* Feed raw PTY bytes through the vterm state machine; updates the lines and
 * marks the touched ones dirty. Surfaces the first error raised inside a
 * state callback. */
struct yetty_ycore_void_result poc_yvterm_grid_feed(struct poc_yvterm_grid *grid,
                                                    const char *bytes, size_t len);

/* Accessors for the renderer. */
uint32_t poc_yvterm_grid_cols(const struct poc_yvterm_grid *grid);
uint32_t poc_yvterm_grid_visible_rows(const struct poc_yvterm_grid *grid);
uint32_t poc_yvterm_grid_total_rows(const struct poc_yvterm_grid *grid);
struct poc_line *poc_yvterm_grid_lines(struct poc_yvterm_grid *grid);

/* Rolling ring offset — feed into the shader's root_row uniform so it maps
 * visible row -> GPU slot the same way the grid does. */
uint32_t poc_yvterm_grid_base(const struct poc_yvterm_grid *grid);

/* True if any line is dirty since the last render. */
int poc_yvterm_grid_is_dirty(const struct poc_yvterm_grid *grid);

/* --stress: mark every line dirty so the next frame re-uploads all of them. */
void poc_yvterm_grid_force_full_dirty(struct poc_yvterm_grid *grid);

/* Renderer calls this after uploading all dirty lines, so the grid goes clean
 * again and the loop stops re-rendering an unchanged frame. */
void poc_yvterm_grid_clear_dirty(struct poc_yvterm_grid *grid);

/* Reflow to a new viewport: reallocate the line collection and resize the
 * vterm state grid to cols x visible_rows (total_rows backing lines). The
 * caller recreates the GPU cell buffer and SIGWINCHes the PTY so the child
 * repaints at the new size. Re-blanks and marks everything dirty. */
struct yetty_ycore_void_result poc_yvterm_grid_resize(struct poc_yvterm_grid *grid, uint32_t cols,
                                                      uint32_t visible_rows, uint32_t total_rows);

#ifdef __cplusplus
}
#endif

#endif /* POC_YVTERM_GRID_H */
