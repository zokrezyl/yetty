/* canvas.h — polymorphic ypaint canvas interface.
 *
 * `struct yetty_ypaint_canvas` is the polymorphic base for both
 * scrolling-canvas (terminal text/ypaint layer) and static-canvas (yui
 * chrome). It carries a vtable and the state shared between both
 * variants (grid + cell size, font cache, flyweight registry,
 * complex-prim factory, GPU staging buffers, dirty flag).
 *
 * Consumers that want to be canvas-agnostic hold a
 * `struct yetty_ypaint_canvas *` and call the `yetty_ypaint_canvas_*`
 * functions below. Each variant's `_create` returns the concrete type
 * (e.g. `struct yetty_ypaint_scrolling_canvas *`); pass
 * `&that->base` when you want to invoke the polymorphic surface.
 *
 * Mode-specific operations (cursor / scroll / view_top for scrolling)
 * live on the variant's own header and take the concrete pointer.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ypaint_canvas;
struct yetty_ypaint_core_buffer;
struct yetty_ypaint_core_complex_prim_factory;
struct yetty_ypaint_core_complex_prim_instance;
struct yetty_ypaint_core_flyweight_registry;
struct yetty_ypaint_font;
struct yetty_yterm_osc_statemachine;

/* Polymorphic canvas pointer result — returned by every variant's
 * create. The variant struct stays internal; consumers hold the base. */
YETTY_YRESULT_DECLARE(yetty_ypaint_canvas_ptr, struct yetty_ypaint_canvas *);

/* Public scroll/cursor callback typedefs. The scrolling variant fires the
 * scroll callback whenever ingestion advances the live viewport past its
 * current bottom; cursor callback fires when the cursor moves without
 * scrolling. Static-canvas never invokes either. userdata is opaque to
 * the canvas. */
typedef struct yetty_ycore_void_result (*yetty_ypaint_canvas_scroll_callback)(
    void *userdata, uint16_t num_lines);
typedef struct yetty_ycore_void_result (*yetty_ypaint_canvas_cursor_callback)(
    void *userdata, uint16_t new_row);

/* Pointer + size view returned by build_prim_staging.
 * word_count is 0 on an empty canvas. */
struct yetty_ypaint_prim_staging {
    const uint32_t *data;
    uint32_t word_count;
};
YETTY_YRESULT_DECLARE(yetty_ypaint_prim_staging, struct yetty_ypaint_prim_staging);

/* View of a single glyph primitive — what a visitor sees during
 * `for_each_glyph`. x/y are absolute canvas pixel coordinates of the
 * glyph's anchor; glyph_idx is the atlas index the font cache handed
 * out; font_slot is the canvas-local font index that produced the
 * glyph (0 = default), or -1 if the prim has no font slot encoded. */
struct yetty_ypaint_glyph_view {
    float    x;
    float    y;
    uint32_t glyph_idx;
    int32_t  font_slot;
};

typedef void (*yetty_ypaint_canvas_glyph_visitor)(
    const struct yetty_ypaint_glyph_view *view, void *user);

/*===========================================================================
 * Polymorphic API
 *===========================================================================*/

/* Destroy. Frees variant-specific state (lines, scrollbuffer, ...) and
 * the common state (font cache, registries, staging buffers), then
 * frees the outer canvas struct. */
struct yetty_ycore_void_result yetty_ypaint_canvas_destroy(
    struct yetty_ypaint_canvas *canvas YETTY_ANNOT_CALLEE_OWNED);

/* Config */
struct yetty_ycore_void_result yetty_ypaint_canvas_set_cell_size(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_pixel_size pixel_size);
struct yetty_ycore_void_result yetty_ypaint_canvas_set_grid_size(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_grid_size grid_size);

struct yetty_ycore_pixel_size yetty_ypaint_canvas_cell_get_pixel_size(
    const struct yetty_ypaint_canvas *canvas);
struct yetty_ycore_grid_size yetty_ypaint_canvas_get_grid_size(
    const struct yetty_ypaint_canvas *canvas);

/* Streaming OSC ingestion. Pulls already-decoded primitive bytes from the
 * OSC state machine via a per-envelope `prim_iter` parked on the variant
 * struct. Returns mid-envelope (OK_VOID) when the SM signals
 * `would_block`; the SM re-dispatches the canvas on the next process()
 * round and the iter resumes where it left off. On DONE (SM at_end with
 * everything consumed) the variant runs end-of-envelope finalisation:
 * attach pass, font_map_release_all, viewport scroll (scrolling only),
 * scrollback evict (scrolling only). */
struct yetty_ycore_void_result yetty_ypaint_canvas_process_input(
    struct yetty_ypaint_canvas *canvas, struct yetty_yterm_osc_statemachine *sm);

/* Cursor / scroll API. For static-canvas these are no-ops (cursor pinned
 * at (0,0), no scrollback). The scrolling variant treats `cursor_pos` as
 * a screen-row offset from the viewport top. */
struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_pos(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_grid_cursor_pos cursor_pos);

struct yetty_ycore_void_result yetty_ypaint_canvas_scroll_lines(
    struct yetty_ypaint_canvas *canvas, uint16_t num_lines);

/* Pin / release the visible viewport for scrollback view. While active,
 * rebuild_grid and the rolling_row_0 accessor return `view_top` instead
 * of the live anchor. Static-canvas ignores. */
struct yetty_ycore_void_result yetty_ypaint_canvas_set_view_top(
    struct yetty_ypaint_canvas *canvas, bool active, uint32_t view_top);

/* Effective viewport top (scrollback override if active, else live anchor).
 * Static-canvas returns 0. */
uint32_t yetty_ypaint_canvas_rolling_row_0(struct yetty_ypaint_canvas *canvas);

/* Live anchor — ignores any scrollback override. Static-canvas returns 0. */
uint32_t yetty_ypaint_canvas_live_rolling_row_0(struct yetty_ypaint_canvas *canvas);

struct yetty_ycore_void_result yetty_ypaint_canvas_set_scroll_callback(
    struct yetty_ypaint_canvas *canvas,
    yetty_ypaint_canvas_scroll_callback callback, void *userdata);

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_callback(
    struct yetty_ypaint_canvas *canvas,
    yetty_ypaint_canvas_cursor_callback callback, void *userdata);

/* Packed GPU format */
struct yetty_ycore_void_result yetty_ypaint_canvas_mark_dirty(
    struct yetty_ypaint_canvas *canvas);
bool yetty_ypaint_canvas_is_dirty(const struct yetty_ypaint_canvas *canvas);

struct yetty_ycore_void_result yetty_ypaint_canvas_rebuild_grid(
    struct yetty_ypaint_canvas *canvas);

const uint32_t *yetty_ypaint_canvas_grid_data(const struct yetty_ypaint_canvas *canvas);
uint32_t yetty_ypaint_canvas_grid_word_count(const struct yetty_ypaint_canvas *canvas);

struct yetty_ypaint_prim_staging_result yetty_ypaint_canvas_build_prim_staging(
    struct yetty_ypaint_canvas *canvas);

uint32_t yetty_ypaint_canvas_prim_gpu_size(const struct yetty_ypaint_canvas *canvas);

/* State management */
struct yetty_ycore_void_result yetty_ypaint_canvas_clear(struct yetty_ypaint_canvas *canvas);
uint32_t yetty_ypaint_canvas_primitive_count(const struct yetty_ypaint_canvas *canvas);

/* Fonts */
uint32_t yetty_ypaint_canvas_font_count(const struct yetty_ypaint_canvas *canvas);

/* Monotonic counter bumped on every font-cache slot alloc AND release.
 * Lets layers detect any change in the active slot set — `font_count` is
 * a high watermark and stays unchanged when a slot drops. */
uint32_t yetty_ypaint_canvas_font_generation(const struct yetty_ypaint_canvas *canvas);

struct yetty_ypaint_font *yetty_ypaint_canvas_get_font_at(
    const struct yetty_ypaint_canvas *canvas, uint32_t slot);
struct yetty_ypaint_font *yetty_ypaint_canvas_get_default_font(
    const struct yetty_ypaint_canvas *canvas);

/* Flyweight registry / complex-prim factory accessors */
const struct yetty_ypaint_core_flyweight_registry *
yetty_ypaint_canvas_get_flyweight_registry(const struct yetty_ypaint_canvas *canvas);
struct yetty_ypaint_core_complex_prim_factory *
yetty_ypaint_canvas_get_complex_prim_factory(const struct yetty_ypaint_canvas *canvas);

/* Complex primitive access (for atlas rendering) */
uint32_t yetty_ypaint_canvas_complex_prim_count(const struct yetty_ypaint_canvas *canvas);
struct yetty_ypaint_core_complex_prim_instance *yetty_ypaint_canvas_get_complex_prim(
    const struct yetty_ypaint_canvas *canvas, uint32_t index);

/* Glyph iteration (for selection / clipboard text extraction) */
void yetty_ypaint_canvas_for_each_glyph(
    struct yetty_ypaint_canvas *canvas,
    yetty_ypaint_canvas_glyph_visitor visitor, void *user);

#ifdef __cplusplus
}
#endif
