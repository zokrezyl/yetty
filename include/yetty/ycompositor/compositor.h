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

#include <stddef.h>
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

/*---------------------------------------------------------------------------
 * NULL-handling convention
 *
 * yetty_ycompositor_destroy(NULL) is a no-op that returns OK — mirrors
 * the standard `free(NULL) = no-op` pattern so cleanup paths can call
 * it without first checking whether construction succeeded.
 *
 * Every OTHER public entry point requires a valid compositor pointer.
 * Passing NULL there is a contract violation and surfaces as
 * `YETTY_ERR(..., "<func>: NULL arg")` rather than being silently
 * swallowed — keeps caller bugs visible instead of letting them
 * propagate as no-ops.
 *=========================================================================*/

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

/* Feed a contiguous block of wire-record bytes into the compositor
 * without going through the OSC / wire-statemachine framing. Used by
 * in-process producers (e.g. an embedded ygui_engine + ycompositor)
 * that already hold the drawable byte stream produced by
 * yetty_ydraw_draw_list. The bytes must be a sequence of complete
 * CMD_ZERO / CMD_GROUP / CMD_DELETE / CMD_UPDATE records in the same
 * shape the OSC pipeline would have delivered; CMD_GROUP envelopes
 * use the new with-rect form so the receiver can place each window's
 * yfigure_group + ygrid at the right position.
 *
 * Marks the compositor dirty and (if registered) invokes the
 * request_render callback at the end, same as process_input. */
struct yetty_ycore_void_result yetty_ycompositor_process_records(
    struct yetty_ycompositor *comp,
    const uint8_t *bytes, size_t bytes_len);

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

/* The terminal sets this once at create time so the compositor can
 * ask for a render frame after decoding an OSC envelope. NULL is OK
 * (no request will fire). */
typedef struct yetty_ycore_void_result (*yetty_ycompositor_request_render_fn)(void *user);

void yetty_ycompositor_set_request_render(
    struct yetty_ycompositor *comp,
    yetty_ycompositor_request_render_fn fn, void *user);

/* Attach a default MSDF / raster font for every ygrid the compositor
 * creates via group_create. ygrid borrows the pointer at slot 0; the
 * caller owns lifetime and must keep the font alive longer than the
 * compositor (and any group inside it that holds the borrowed ref).
 * Pass NULL to clear — groups created afterwards will have no font,
 * and TEXT_SPAN records in their bodies will silently drop glyphs. */
struct yetty_ydraw_font;
void yetty_ycompositor_set_default_font(
    struct yetty_ycompositor *comp, struct yetty_ydraw_font *font);

/* Offset of the compositor's render area within the target's surface,
 * in target pixel space.
 *
 * Wire CMD_GROUP rects from producers (ygreeter, ygui-driven OSC) are
 * PANE-LOCAL — the producer doesn't know where the compositor's pane
 * sits inside the terminal surface (e.g. the tab strip pushes panes
 * down by ~30 px). The compositor converts incoming pane-local rects
 * to absolute target rects by adding (offset_x, offset_y); rendered
 * figures land at the same screen coords the input pipeline subtracts
 * to forward mouse events, so cursor and hit-test agree.
 *
 * Changing the offset shifts every existing figure rect by the delta
 * and marks the whole frame damaged. Default offset is (0, 0). */
void yetty_ycompositor_set_viewport_offset(
    struct yetty_ycompositor *comp, float offset_x, float offset_y);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCOMPOSITOR_COMPOSITOR_H */
