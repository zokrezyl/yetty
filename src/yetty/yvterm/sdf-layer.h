/* sdf-layer.h — yvterm's own SDF / glyph / text render backend.
 *
 * yvterm stores raw ydraw drawable records (SDF shapes, GLYPH prims,
 * TEXT_DRAWABLE_LIST runs, FONT resources) per line on its grid ring (see
 * grid.c). This layer rasterises them: it owns a GPU resource binder, the
 * combined SDF+font shader, and per-frame staging buffers, and draws every
 * visible line's primitives into the figure's target — anchored to the line
 * via the shader's rolling-row mechanism so figures scroll in lockstep with
 * the text.
 *
 * It reuses the SHARED render machinery (yrender gpu-resource-binder, the
 * generated ysdf.gen.wgsl, yrender font-dispatcher) — the same components
 * ygrid uses — but is a standalone yvterm-internal renderer: it does NOT
 * instantiate or depend on ygrid, and sources its primitives from yvterm's
 * own grid. Plain-C render helper (no object/method surface), so not a
 * yclass class.
 */
#ifndef YETTY_YVTERM_SDF_LAYER_H
#define YETTY_YVTERM_SDF_LAYER_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvterm_sdf_layer;
struct yetty_context;
struct yetty_yclass_object;
struct yetty_ydraw_target;

YETTY_YRESULT_DECLARE(yetty_yvterm_sdf_layer_ptr, struct yetty_yvterm_sdf_layer *);

/* Build the layer against the framework's GPU device/queue/allocator and load
 * the SDF lib + layer shader. Returns NULL-valued OK when context is headless
 * (no runtime) — render then becomes a no-op. */
struct yetty_yvterm_sdf_layer_ptr_result yetty_yvterm_sdf_layer_create(
    const struct yetty_context *context);

/* Tear down the binder, fonts, shader, and staging buffers. NULL-safe. */
void yetty_yvterm_sdf_layer_destroy(struct yetty_yvterm_sdf_layer *layer);

/* Render the SDF / glyph / text primitives stored on `grid_obj`'s ring into
 * `target`, within the figure's pane-local `rect`. cell_width/cell_height are
 * the terminal cell metrics; cols/rows the visible grid; root_row + slot_count
 * the ring mapping the text pass uses (visible row r ↔ slot (r+root_row) %
 * slot_count) — passed in so figures land on exactly the rows their text does,
 * in both the live and scrolled-back views. */
struct yetty_ycore_void_result yetty_yvterm_sdf_layer_render(
    struct yetty_yvterm_sdf_layer *layer, struct yetty_yclass_object *grid_obj,
    struct yetty_ydraw_target *target, struct yetty_ycore_rectangle rect, float cell_width,
    float cell_height, uint32_t cols, uint32_t rows, uint32_t root_row, uint32_t slot_count,
    float visual_zoom_scale, float visual_zoom_off_x, float visual_zoom_off_y,
    float cell_zoom_scale);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_SDF_LAYER_H */
