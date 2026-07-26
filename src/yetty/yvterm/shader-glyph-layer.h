/* shader-glyph-layer.h — yvterm's animated procedural "shader glyph" backend.
 *
 * A small set of codepoints in the Supplementary Private-Use-Area-B window
 * (U+100000..U+100FFF) render not as font glyphs but as per-cell animated
 * fragment shaders (spinner, heartbeat, plasma, …). The shader bodies live in
 * <paths/shaders>/glyph-shaders/0xNN-name.wgsl and are assembled into one
 * dispatcher at create time.
 *
 * This layer is the renderer: it owns a GPU resource binder + the assembled
 * shader, scans the grid's on-screen cells each frame for shader-glyph
 * codepoints, packs one instance per such cell (column, row, local id, fg, bg)
 * and draws an animated quad over each — on top of the text, using the same
 * visual-zoom transform so the glyphs stay welded to their cell under zoom and
 * scroll. An event-loop timer keeps repainting while shader glyphs are on
 * screen and self-stops when none are, so idle terminals cost nothing.
 *
 * Plain-C render helper (no object/method surface), owned by the yvterm:vterm
 * figure exactly like sdf-layer — NOT a yclass class.
 */
#ifndef YETTY_YVTERM_SHADER_GLYPH_LAYER_H
#define YETTY_YVTERM_SHADER_GLYPH_LAYER_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvterm_shader_glyph_layer;
struct yetty_context;
struct yetty_yclass_object;
struct yetty_ydraw_target;

YETTY_YRESULT_DECLARE(yetty_yvterm_shader_glyph_layer_ptr,
                      struct yetty_yvterm_shader_glyph_layer *);

/* Build the layer against the framework's GPU device/queue/allocator, assemble
 * the glyph dispatcher from <paths/shaders>/glyph-shaders, and arm the
 * animation timer on the framework event loop. `owner_figure` is the owning
 * yvterm:vterm object — the timer marks it dirty so the next frame repaints.
 * `sink` (the terminal-host ytermsink:sink) nudges the event loop to schedule
 * that frame via its request_render method. Returns a NULL-valued OK when the
 * context is headless (no GPU) — render then no-ops. */
struct yetty_yvterm_shader_glyph_layer_ptr_result yetty_yvterm_shader_glyph_layer_create(
    const struct yetty_context *context, struct yetty_yclass_object *owner_figure,
    struct yetty_yclass_object *sink);

/* Tear down the timer, binder, shader, and instance buffer. NULL-safe. */
void yetty_yvterm_shader_glyph_layer_destroy(struct yetty_yvterm_shader_glyph_layer *layer);

/* Scan `grid_obj`'s on-screen cells for shader-glyph codepoints and draw the
 * animated glyphs into `target` within the pane-local `rect`. cell_width/height
 * are the cell metrics; cols/rows the visible grid; `window_slots` the resolved
 * view window from yetty_yvterm_grid_view_window (the same mapping the text
 * pass draws) so glyphs land on exactly the rows their cells do, live and
 * scrolled-back. visual_zoom_* mirror the text shader's transform. */
struct yetty_ycore_void_result yetty_yvterm_shader_glyph_layer_render(
    struct yetty_yvterm_shader_glyph_layer *layer, struct yetty_yclass_object *grid_obj,
    struct yetty_ydraw_target *target, struct yetty_ycore_rectangle rect, float cell_width,
    float cell_height, uint32_t cols, uint32_t rows, const uint32_t *window_slots,
    uint32_t window_rows, float visual_zoom_scale, float visual_zoom_off_x,
    float visual_zoom_off_y);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_SHADER_GLYPH_LAYER_H */
