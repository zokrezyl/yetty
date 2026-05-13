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

/* Buffer ingestion. Each primitive in `buffer` is dispatched per its
 * type; the canvas decides whether to place it (scrolling: cursor-
 * relative, viewport scrolls; static: absolute, out-of-grid primitives
 * are dropped). */
struct yetty_ycore_void_result yetty_ypaint_canvas_add_buffer(
    struct yetty_ypaint_canvas *canvas, struct yetty_ypaint_core_buffer *buffer);

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
