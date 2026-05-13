/* static-canvas.h — public API for the static (non-scrolling) ypaint canvas.
 *
 * Used by yui chrome (popups, dialogs, statusbar) where the viewport is
 * fixed, no scrolling occurs, and primitives whose AABB falls outside the
 * grid are discarded. Cursor is pinned at (0,0); rolling_row_0 is always 0.
 *
 * The grid model, font cache, flyweight registry, complex-prim factory,
 * and GPU staging buffers are shared with scrolling-canvas via
 * `struct ypaint_canvas_common` (see canvas-internal.h in the
 * implementation).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ypaint/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ypaint_static_canvas;
struct yetty_context;
struct yetty_ypaint_core_buffer;

YETTY_YRESULT_DECLARE(yetty_ypaint_static_canvas_ptr, struct yetty_ypaint_static_canvas *);

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

YETTY_ANNOT_CALLER_OWNED
struct yetty_ypaint_static_canvas_ptr_result yetty_ypaint_static_canvas_create(
    const struct yetty_context *context);

struct yetty_ycore_void_result yetty_ypaint_static_canvas_destroy(
    struct yetty_ypaint_static_canvas *canvas YETTY_ANNOT_CALLEE_OWNED);

/*===========================================================================
 * Configuration
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ypaint_static_canvas_set_cell_size(
    struct yetty_ypaint_static_canvas *canvas, struct yetty_ycore_pixel_size pixel_size);

/* Set the grid dimensions. Reallocates the line storage to grid_size.rows;
 * any prims already on the canvas are cleared. */
struct yetty_ycore_void_result yetty_ypaint_static_canvas_set_grid_size(
    struct yetty_ypaint_static_canvas *canvas, struct yetty_ycore_grid_size grid_size);

struct yetty_ycore_pixel_size yetty_ypaint_static_canvas_cell_get_pixel_size(
    struct yetty_ypaint_static_canvas *canvas);

struct yetty_ycore_grid_size yetty_ypaint_static_canvas_get_grid_size(
    struct yetty_ypaint_static_canvas *canvas);

/*===========================================================================
 * Buffer management
 *
 * Primitives are positioned at absolute canvas coordinates (cursor pinned
 * at (0,0)). Any primitive whose AABB falls entirely outside
 * [0..rows) × [0..cols) is discarded; partially-visible primitives have
 * their refs clipped to the grid bounds.
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ypaint_static_canvas_add_buffer(
    struct yetty_ypaint_static_canvas *canvas, struct yetty_ypaint_core_buffer *buffer);

/*===========================================================================
 * Packed GPU format
 *===========================================================================*/

bool yetty_ypaint_static_canvas_is_dirty(struct yetty_ypaint_static_canvas *canvas);

struct yetty_ycore_void_result yetty_ypaint_static_canvas_rebuild_grid(
    struct yetty_ypaint_static_canvas *canvas);

const uint32_t *yetty_ypaint_static_canvas_grid_data(struct yetty_ypaint_static_canvas *canvas);
uint32_t yetty_ypaint_static_canvas_grid_word_count(struct yetty_ypaint_static_canvas *canvas);

struct yetty_ypaint_prim_staging_result yetty_ypaint_static_canvas_build_prim_staging(
    struct yetty_ypaint_static_canvas *canvas);

/*===========================================================================
 * State management
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ypaint_static_canvas_clear(
    struct yetty_ypaint_static_canvas *canvas);

uint32_t yetty_ypaint_static_canvas_primitive_count(struct yetty_ypaint_static_canvas *canvas);

uint32_t yetty_ypaint_static_canvas_font_count(const struct yetty_ypaint_static_canvas *canvas);

struct yetty_ypaint_font *yetty_ypaint_static_canvas_get_font_at(
    const struct yetty_ypaint_static_canvas *canvas, uint32_t slot);

/*===========================================================================
 * Complex primitive access
 *===========================================================================*/

uint32_t yetty_ypaint_static_canvas_complex_prim_count(
    struct yetty_ypaint_static_canvas *canvas);

struct yetty_ypaint_core_complex_prim_instance;
struct yetty_ypaint_core_complex_prim_instance *yetty_ypaint_static_canvas_get_complex_prim(
    struct yetty_ypaint_static_canvas *canvas, uint32_t index);

struct yetty_ypaint_core_complex_prim_factory;
struct yetty_ypaint_core_complex_prim_factory *
yetty_ypaint_static_canvas_get_complex_prim_factory(struct yetty_ypaint_static_canvas *canvas);

/*===========================================================================
 * Glyph iteration (for selection / clipboard text extraction)
 *===========================================================================*/

typedef void (*yetty_ypaint_static_canvas_glyph_visitor)(
    const struct yetty_ypaint_glyph_view *view, void *user);

void yetty_ypaint_static_canvas_for_each_glyph(
    struct yetty_ypaint_static_canvas *canvas,
    yetty_ypaint_static_canvas_glyph_visitor visitor, void *user);

#ifdef __cplusplus
}
#endif
