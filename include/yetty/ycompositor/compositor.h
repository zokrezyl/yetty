/*
 * ycompositor — positioned-figure compositor (not a terminal layer).
 *
 * The compositor hosts a z-ordered list of yfigure figures, decodes
 * incoming wire envelopes into them, and renders them to a polymorphic
 * yrender target. It is the root of the new rendering stack: in the
 * end state of the migration the terminal owns one compositor and a
 * text-layer, and nothing else. The terminal drives the compositor
 * with direct calls (no vtable indirection, no terminal_layer base
 * embed — both were transitional scaffolds that have been removed).
 *
 * Coordinate model:
 *   A top-level figure's `rect` is in target pixel space. Children of
 *   a group figure are local to the group; group->render translates
 *   and dispatches (see yfigure/figure.h).
 *
 * Repaint model — damage rects, non-overlap is the optimization:
 *   - Tracks a per-frame damage region in target pixel space (v1:
 *     single bounding union; per-rect list is a future refinement).
 *   - Figure dirty bits contribute the figure's rect to damage.
 *   - Geometry ops (set_rect / raise / remove) contribute the OLD
 *     rect too so anything previously covered is exposed.
 *   - On render, walk figures back-to-front in z-order; a figure is
 *     repainted ONLY if its rect intersects damage.
 *
 * Dependency direction: ycompositor is a lower-level module than
 * yterm. Anything plumbing from the terminal end (OSC routing, render
 * loop, resize plumbing) lives in yterm and calls into ycompositor's
 * direct API.
 */
#ifndef YETTY_YCOMPOSITOR_COMPOSITOR_H
#define YETTY_YCOMPOSITOR_COMPOSITOR_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;
struct yetty_ycompositor;

YETTY_YRESULT_DECLARE(yetty_ycompositor_ptr, struct yetty_ycompositor *);

/*===========================================================================
 * Lifecycle
 *=========================================================================*/

/* Create an empty compositor sized to `cols × rows × cell_size` in
 * target pixel space. */
struct yetty_ycompositor_ptr_result yetty_ycompositor_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height,
    const struct yetty_context *context);

struct yetty_ycore_void_result yetty_ycompositor_destroy(
    struct yetty_ycompositor *comp);

/*===========================================================================
 * Driven by the terminal
 *=========================================================================*/

/* Render every figure that intersects the current damage region into
 * `target`. `force=1` means a lower layer redrew into the shared
 * target — every figure rect is treated as damaged. Returns 1 if any
 * pixels were submitted, 0 if nothing needed redrawing. */
struct yetty_ycore_int_result yetty_ycompositor_render(
    struct yetty_ycompositor *comp,
    struct yetty_ydraw_target *target, int force);

/* Process one OSC envelope on the YETTY_OSC_YCOMPOSITOR_BIN wire code.
 * Long-lived coroutine: drains the SM, walks records via the ydraw
 * iterator, dispatches CMD_ZERO / CMD_GROUP / CMD_DELETE / ADD into
 * the default ygrid. */
struct yetty_ycore_void_result yetty_ycompositor_process_input(
    struct yetty_ycompositor *comp,
    struct yetty_ywire_wire_statemachine *sm);

/* Notify of grid + cell-size change. Resizes the default ygrid to
 * cover the new pane and marks everything dirty. */
struct yetty_ycore_void_result yetty_ycompositor_resize(
    struct yetty_ycompositor *comp,
    struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);

int yetty_ycompositor_is_dirty(const struct yetty_ycompositor *comp);
int yetty_ycompositor_is_empty(const struct yetty_ycompositor *comp);

/*===========================================================================
 * Figure management
 *=========================================================================*/

/* Append at top of z-order; compositor takes ownership (destroy
 * cascades on _destroy). */
struct yetty_ycore_void_result yetty_ycompositor_add_figure(
    struct yetty_ycompositor *comp,
    struct yetty_yfigure_figure *figure);

/* Remove without destroying; caller resumes ownership. */
struct yetty_ycore_void_result yetty_ycompositor_remove_figure(
    struct yetty_ycompositor *comp,
    struct yetty_yfigure_figure *figure);

struct yetty_ycore_void_result yetty_ycompositor_raise_figure(
    struct yetty_ycompositor *comp,
    struct yetty_yfigure_figure *figure);

/* Move + damage tracking. */
struct yetty_ycore_void_result yetty_ycompositor_set_figure_rect(
    struct yetty_ycompositor *comp,
    struct yetty_yfigure_figure *figure,
    struct yetty_ycore_rectangle new_rect);

/* Lazily create a full-pane ygrid figure that receives every record
 * arriving on YETTY_OSC_YCOMPOSITOR_BIN. Called by the terminal at
 * setup and on resize. */
struct yetty_ycore_void_result yetty_ycompositor_ensure_default_ygrid(
    struct yetty_ycompositor *comp,
    uint32_t cols, uint32_t rows, float cell_w, float cell_h,
    const struct yetty_context *context);

/* The terminal sets this once at create time so the compositor can
 * ask for a render frame after decoding an OSC envelope. NULL is OK
 * (no request will fire). */
typedef struct yetty_ycore_void_result (*yetty_ycompositor_request_render_fn)(void *user);

void yetty_ycompositor_set_request_render(
    struct yetty_ycompositor *comp,
    yetty_ycompositor_request_render_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCOMPOSITOR_COMPOSITOR_H */
