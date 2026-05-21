/* scene-canvas.h — public API for the scene-canvas variant.
 *
 * A fixed-size grid of cells, with named drawables forming a tree.
 *
 * Every scene-canvas carries an implicit ROOT GROUP with external_id 0.
 * It is virtual: producers never create it, never name it, never delete
 * it — the canvas owns it from create() to destroy(). Any drawable
 * emitted on the wire without a surrounding CMD_GROUP lands as a
 * direct child of the root group. CMD_GROUP exists only when the
 * producer wants a named, addressable subtree (so DELETE / UPDATE
 * can target the whole bundle by one id).
 *
 * A drawable is polymorphic — one of:
 *   - GROUP      : has a children[] of more drawables + spatial
 *                  membership; addressable via external_id.
 *   - FLYWEIGHT  : a simple paint record (SDF / TEXT_SPAN / FONT) —
 *                  just wire bytes, no live state.
 *   - FIGURE     : a stateful complex drawable (yplot / yimage / yvideo
 *                  / ymesh) wrapping a runtime figure_instance with
 *                  its own GPU resources + vtable.
 *
 * Drawables and their bodies are owned by their parent group's
 * children[] array (a single heap allocation per group, drawable
 * values packed inline). Bodies (group / figure) are separate heap
 * allocations; flyweight bodies are the bare wire-byte pointer.
 *
 * Lookup by external_id returns the drawable directly. The grid keys
 * on (parent_group *, child_index) — those pairs stay valid across
 * children[] realloc, which lets the grid drop / re-place a drawable
 * cheaply on bounds change without re-walking the whole tree.
 *
 * Deletion of a group walks its own touched-cells list, drops its
 * grid bucket from each, then recurses into children. No grid-wide
 * scan, no AABB recomputation at delete time.
 *
 * Two input paths share one internal API:
 *   - OSC state machine via the polymorphic process_input.
 *   - Direct in-process calls — the drawable API below.
 */
#pragma once

#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;

/* Drawable variants. Tags the body pointer in `struct yetty_ydraw_drawable`. */
enum yetty_ydraw_drawable_class {
    YETTY_YDRAW_DRAWABLE_GROUP     = 0,
    YETTY_YDRAW_DRAWABLE_FLYWEIGHT = 1,
    YETTY_YDRAW_DRAWABLE_FIGURE    = 2,
};

/* Opaque drawable handle. Stable for the drawable's lifetime; never
 * moves until the drawable is deleted. */
struct yetty_ydraw_drawable;

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_ptr,
                      struct yetty_ydraw_drawable *);

/* Create a scene-canvas. Returns the polymorphic base pointer; the
 * caller frees with yetty_ydraw_canvas_destroy. */
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_canvas_ptr_result yetty_ydraw_scene_canvas_create(
    const struct yetty_context *context);

/* Minimal-init constructor for unit tests — no GPU device, no figure
 * factory, no font cache. The resulting canvas supports the drawable
 * tree, grid/staging rebuild, and dispatch of CMD_ZERO / CMD_GROUP /
 * CMD_DELETE / plain SDF flyweights. TEXT_SPAN, FONT, and figure
 * drawables are NOT supported here — they need the GPU stack.
 *
 * Free with the same yetty_ydraw_canvas_destroy path as the full
 * constructor. */
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_canvas_ptr_result yetty_ydraw_scene_canvas_create_for_test(void);

/* The implicit root group (drawable_class == GROUP, external_id == 0).
 * Receives any drawable added without an explicit parent group.
 * Lives for the canvas's lifetime; cannot be deleted. */
struct yetty_ydraw_drawable *yetty_ydraw_scene_canvas_root(
    struct yetty_ydraw_canvas *canvas);

/* Create a new GROUP drawable as a child of `parent` (root group if
 * NULL). `external_id` is the caller's stable id (e.g. a ygui widget
 * id); passing the same external_id as an existing drawable returns
 * an error. */
struct yetty_ydraw_drawable_ptr_result yetty_ydraw_drawable_create_group(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_drawable *parent,
    uint64_t external_id);

/* Look up a drawable by external_id. Returns NULL if not found. The
 * root group is looked up with external_id == 0. */
struct yetty_ydraw_drawable *yetty_ydraw_drawable_lookup(
    struct yetty_ydraw_canvas *canvas, uint64_t external_id);

/* Append a flyweight paint record to `parent` (which must be a GROUP).
 * The wire bytes are copied; the AABB is computed via `fw->ops->aabb`
 * and the resulting cell range is inserted into each overlapping
 * cell's bucket (and recorded in the parent's touched-cells list). */
struct yetty_ycore_void_result yetty_ydraw_drawable_add_prim(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_drawable *parent,
    const struct yetty_ydraw_drawable_flyweight *fw);

/* Remove every direct child of this GROUP drawable, detaching them
 * from every cell their parent had touched. Nested children are
 * destroyed recursively. The drawable itself remains usable and may
 * be repopulated. */
struct yetty_ycore_void_result yetty_ydraw_drawable_clear(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_drawable *drawable);

/* Recursively delete this drawable and all its descendants. The handle
 * is invalid after this call. The root group cannot be deleted — call
 * `clear` instead. */
struct yetty_ycore_void_result yetty_ydraw_drawable_delete(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_drawable *drawable);

/* Recursively delete every child subtree of this GROUP drawable. The
 * drawable itself remains, with its own direct flyweight / figure
 * drawables and touched cells untouched. */
struct yetty_ycore_void_result yetty_ydraw_drawable_delete_children(
    struct yetty_ydraw_canvas *canvas,
    struct yetty_ydraw_drawable *drawable);

#ifdef __cplusplus
}
#endif
