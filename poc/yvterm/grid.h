/*
 * poc/yvterm/grid.h — the text grid as a contiguous collection of lines,
 * driven from the libvterm STATE layer.
 *
 * This is the heart of the yvterm-new probe: no VTermScreen, no parallel
 * scrollback. One contiguous cell ring is both the model and the GPU upload
 * source; the layer plugs into the production render-target via the standard
 * yetty_yrender_terminal_layer interface so it goes through the real
 * MSDF/cdb text shader.
 */
#ifndef POC_YVTERM_GRID_H
#define POC_YVTERM_GRID_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yterminal/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

struct poc_yvterm_grid;

YETTY_YRESULT_DECLARE(poc_yvterm_grid_ptr, struct poc_yvterm_grid *);

/* Create the grid. Borrows `font` (caller keeps ownership / destroys it after
 * the grid). `text_shader_path` is the path to text-layer.wgsl. The cell size
 * is taken from the font. */
struct poc_yvterm_grid_ptr_result poc_yvterm_grid_create(uint32_t cols, uint32_t rows,
                                                         struct yetty_yfont_ms_font *font,
                                                         const char *text_shader_path);

struct yetty_ycore_void_result poc_yvterm_grid_destroy(struct poc_yvterm_grid *grid);

/* Upcast to the render-layer base (first member) — pass to
 * target->ops->render_layer. */
struct yetty_yrender_terminal_layer *poc_yvterm_grid_as_layer(struct poc_yvterm_grid *grid);

/* Feed raw PTY bytes through the vterm state machine; updates the line ring
 * and the dirty span. Surfaces the first error raised inside a state
 * callback. */
struct yetty_ycore_void_result poc_yvterm_grid_feed(struct poc_yvterm_grid *grid,
                                                    const char *bytes, size_t len);

/* True if anything changed since the last render. */
int poc_yvterm_grid_is_dirty(const struct poc_yvterm_grid *grid);

/* --stress: mark the entire grid dirty so the next frame re-uploads and
 * re-shades everything (worst case). */
void poc_yvterm_grid_force_full_dirty(struct poc_yvterm_grid *grid);

/* Perf counters, read once per stats interval. */
struct poc_yvterm_grid_stats {
    uint64_t scroll_fastpath;  /* whole-screen scrolls served by root_row slide */
    uint64_t scroll_memmove;   /* scrolls that fell back to content memmove */
    uint64_t cells_uploaded;   /* cells handed to the GPU buffer this interval */
    uint32_t dirty_rows_last;  /* dirty row span width on the last frame */
};
struct poc_yvterm_grid_stats poc_yvterm_grid_stats_take(struct poc_yvterm_grid *grid);

#ifdef __cplusplus
}
#endif

#endif /* POC_YVTERM_GRID_H */
