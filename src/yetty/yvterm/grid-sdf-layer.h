/* grid-sdf-layer.h — yvterm's own SDF / glyph / text render backend
 * (paints the grid.c line ring; the grid- prefix marks that pairing).
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
#ifndef YETTY_YVTERM_GRID_SDF_LAYER_H
#define YETTY_YVTERM_GRID_SDF_LAYER_H

#include <math.h>
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

/* Plan-driven frame protocol (vterm.c walks the sorted paint plan):
 *
 *   begin        — reset the frame's staging, install wire fonts (whole
 *                  ring + resolved window), stage the shaped terminal-cell
 *                  glyph runs of the window rows (they sit below every
 *                  rich leaf — the base range [0, prim_count-at-begin)).
 *   stage_leaf   — append one primitive/text record's prims anchored at
 *                  its block-top viewport row, in PLAN order. Returns the
 *                  layer's total prim count after staging, so the caller
 *                  keeps `(first, count)` ranges for contiguous runs.
 *   finish       — one upload of the staged buffers + uniforms; after it
 *                  no more staging this frame.
 *   draw_range   — one ranged draw: composites exactly the prims in
 *                  [first, first+count) — a run may not cross a rendered
 *                  complex cut point, so vterm alternates draw_range with
 *                  delegated complex draws in plan order.
 *
 * All draws load/preserve `target` and blend premultiplied source-over.
 * cell_width/cell_height are the CURRENT terminal cell metrics;
 * density_scale x cell_zoom is the rich prims' producer-local →
 * framebuffer scale (begin's own shaped-terminal-glyph staging is
 * framebuffer text at scale 1 — the base range); cols/rows the visible
 * grid; `window_slots` the resolved view window (window row r draws at
 * viewport row r); `slot_count` bounds the whole-ring wire-font install
 * pass. */
struct yetty_ycore_void_result yetty_yvterm_sdf_layer_begin(
    struct yetty_yvterm_sdf_layer *layer, struct yetty_yclass_object *grid_obj, float cell_width,
    float cell_height, float density_scale, float cell_zoom, uint32_t cols, uint32_t rows,
    const uint32_t *window_slots, uint32_t window_rows, uint32_t slot_count);

/* THE bucketing formula — one axis of the cell range a primitive extent
 * covers, shared verbatim by CPU bucketing and mirrored by the shader's
 * inverse sample transform (content = (pixel - row_anchor) / local_scale):
 * a prim's local extent [min, max] plus its accumulated group offset,
 * scaled by local_scale (rich prims: density x structural cell zoom;
 * shaped TERMINAL glyph runs: 1 — they are framebuffer-space text and
 * follow the text grid exactly once), divided by the CURRENT framebuffer
 * cell stride. Exposed inline so the transform contract test pins the
 * exact expression the renderer uses. */
static inline void yetty_yvterm_sdf_prim_cell_span(float extent_min, float extent_max, float offset,
                                                   float local_scale, float cell_stride,
                                                   int32_t *out_cell_min, int32_t *out_cell_max)
{
    *out_cell_min = (int32_t)floorf((extent_min + offset) * local_scale / cell_stride);
    *out_cell_max = (int32_t)floorf((extent_max + offset) * local_scale / cell_stride);
}

struct yetty_ycore_uint32_result yetty_yvterm_sdf_layer_stage_leaf(
    struct yetty_yvterm_sdf_layer *layer, const uint32_t *words, uint32_t word_count,
    int32_t anchor_top_row, float offset_x, float offset_y, int32_t clip_row_min,
    int32_t clip_row_max, float clip_x, float clip_y, float clip_w, float clip_h);

/* Total prims staged so far this frame (0 outside an active frame). */
uint32_t yetty_yvterm_sdf_layer_prim_count(const struct yetty_yvterm_sdf_layer *layer);

struct yetty_ycore_void_result yetty_yvterm_sdf_layer_finish(struct yetty_yvterm_sdf_layer *layer,
                                                             struct yetty_ycore_rectangle rect,
                                                             float visual_zoom_scale,
                                                             float visual_zoom_off_x,
                                                             float visual_zoom_off_y,
                                                             float cell_zoom_scale);

struct yetty_ycore_void_result yetty_yvterm_sdf_layer_draw_range(
    struct yetty_yvterm_sdf_layer *layer, struct yetty_ydraw_target *target,
    struct yetty_ycore_rectangle rect, uint32_t first, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_GRID_SDF_LAYER_H */
