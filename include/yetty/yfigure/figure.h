/*
 * yfigure — base type for compositor figures.
 *
 * A figure is a positioned, axis-aligned rectangular thing that knows
 * how to paint itself inside its rectangle. The compositor (ycompositor,
 * defined elsewhere) hosts a list of figures and drives damage-rect
 * repaint; the figure itself is unaware of the compositor.
 *
 * Concrete figure kinds (ygrid, yimage, yplot, …) each live in their
 * own module, embed `struct yetty_yfigure_figure` as the first member,
 * and install an ops vtable. A group is itself a figure (composite
 * pattern, same model as scene-canvas).
 *
 * Coordinate system:
 *   A figure's `rect` is its position + size in absolute target pixel
 *   space. The figure knows where it is from its own state — render
 *   ops do NOT take position parameters.
 *
 *   The wire format encodes coordinates relative to the enclosing
 *   group for compactness and cheap subtree moves; the decoder
 *   translates wire-relative to runtime-absolute as it walks each
 *   CMD_GROUP. When a group moves at runtime, the move walks
 *   descendants and updates their absolute rects accordingly.
 */
#ifndef YETTY_YFIGURE_FIGURE_H
#define YETTY_YFIGURE_FIGURE_H

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yfigure_figure;
struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;

YETTY_YRESULT_DECLARE(yetty_yfigure_figure_ptr, struct yetty_yfigure_figure *);

struct yetty_yfigure_figure_ops {
    /* Destroy concrete state and free `self`. For composite figures
     * (groups) this cascades to children. The base struct guarantees
     * `self` is non-NULL when ops are invoked. */
    struct yetty_ycore_void_result (*destroy)(struct yetty_yfigure_figure *self);

    /* Paint into `target`. The figure already knows its position and
     * size from `self->rect` (absolute, target pixel space). It uses
     * `target` polymorphically — view via `target->ops->get_view`,
     * pane via `target->viewport`. The figure owns its own pipeline
     * + binder (yplot-pattern); the concrete kind of target sitting
     * behind the handle doesn't matter to it. */
    struct yetty_ycore_void_result (*render)(
        struct yetty_yfigure_figure *self,
        struct yetty_ydraw_target *target);

    /* Consume input directly from the wire-statemachine. A coroutine —
     * yields when the SM has no bytes ready and resumes when the next
     * chunk arrives. Same shape every figure speaks; no buffered
     * payload, no length argument. The figure reads what it needs from
     * the SM in its own format.
     *
     * For composite figures (yfigure_container), the body is a stream
     * of `{length, id, body}` records — process_input loops reading
     * record headers from the SM and dispatches each to either its own
     * admin handler (id=0) or the matching child's process_input.
     *
     * For leaf figures (ygrid, ymgui, yrdawn, …), the body is the
     * figure-kind's own self-describing format. The figure reads from
     * the SM directly and decodes as it goes.
     *
     * NULL = figure is purely visual and rejects wire updates. */
    struct yetty_ycore_void_result (*process_input)(
        struct yetty_yfigure_figure *self,
        struct yetty_ywire_wire_statemachine *sm);

    /* Apply a wire update from an in-memory byte buffer. TEMPORARY —
     * the migration target is `process_input` above. Kept while ygrid
     * and the container's admin records still go through the buffered
     * path; will be deleted once every figure kind speaks `process_input`. */
    struct yetty_ycore_void_result (*process_bytes)(
        struct yetty_yfigure_figure *self,
        const uint8_t *bytes, size_t bytes_len);
};

struct yetty_yfigure_figure {
    const struct yetty_yfigure_figure_ops *ops;
    /* AABB in target pixel space. Set at construction by the concrete
     * figure; subsequent moves go through the parent's set_rect so
     * damage tracking stays correct.
     *
     * NOTE: the figure has no `id` field — id is a parent-scoped name
     * the parent group uses to address its children, not a property of
     * the child itself. See yetty_yfigure_container_add_child. */
    struct yetty_ycore_rectangle rect;
    /* Set by the figure when its contents change without geometry
     * moving. The parent ORs this into its damage region during the
     * next render pass and clears it after. */
    int dirty;
};

/*===========================================================================
 * Group — a figure that contains other figures.
 *
 * The group is itself a figure: render iterates children in insertion
 * order (back-to-front), destroy cascades.
 *
 * Children's rects, like every figure's, are in absolute target pixel
 * space. The group is primarily a lifecycle / identity container —
 * destroying a group cascades to its children. The wire decoder is
 * what translates wire-relative coords into the absolute runtime rects
 * as it walks each CMD_GROUP; once decoded, every figure knows its
 * own absolute position. A runtime "move group" helper (when needed)
 * walks descendants to translate their rects.
 *=========================================================================*/

struct yetty_yfigure_container;

YETTY_YRESULT_DECLARE(yetty_yfigure_container_ptr, struct yetty_yfigure_container *);

struct yetty_context;
struct yetty_yfigure_registry;

/* Create an empty container with the given AABB.
 *
 * `context` and `registry` are BORROWED pointers — the host owns
 * lifetime. They're used when an admin CREATE_CHILD record arrives:
 * the container calls `registry_mint(kind, child_rect, context)` and
 * `add_child` with the new figure. Sub-containers minted via CREATE_CHILD
 * inherit both pointers. Either may be NULL: a NULL registry causes
 * any CREATE_CHILD record to fail with a "no registry" error, useful
 * for containers that should never accept new children. */
struct yetty_yfigure_container_ptr_result yetty_yfigure_container_create(
    struct yetty_ycore_rectangle rect,
    const struct yetty_context *context,
    struct yetty_yfigure_registry *registry);

/* Consume one full OSC envelope's body off the SM as a record stream
 * targeted at this (root) container. Loops until the SM signals
 * end-of-envelope. This is the top-level entry the terminal's wire-SM
 * coro spawns; nested containers use the bounded `process_input` op
 * instead.
 *
 * After this returns, the container's child set reflects every CREATE/
 * DELETE/UPDATE in the envelope. */
struct yetty_ycore_void_result yetty_yfigure_container_consume_envelope(
    struct yetty_yfigure_container *container,
    struct yetty_ywire_wire_statemachine *sm);

/* Shift every child rect arriving via admin CREATE_CHILD / SET_CHILD_RECT
 * records by (offset_x, offset_y). Producers emit coords in pane-local
 * space; this offset converts them to target pixel space. Default (0, 0).
 * Doesn't retroactively shift already-bound children. */
void yetty_yfigure_container_set_viewport_offset(
    struct yetty_yfigure_container *container,
    float offset_x, float offset_y);

/* Direct byte-stream entry — same record decoding as consume_envelope
 * but reads from a contiguous byte array instead of pumping the SM.
 * Used by in-process producers (e.g. yui's embedded ygui_engine) that
 * already hold the serialized record stream. */
struct yetty_ycore_void_result yetty_yfigure_container_process_records(
    struct yetty_yfigure_container *container,
    const uint8_t *bytes, size_t bytes_len);

/* Upcast: a group is a figure. */
struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(
    struct yetty_yfigure_container *group);

/* Append child at the top of the group's z-order under the given id.
 * The id is parent-scoped — distinct from any other child's id in this
 * group, no global uniqueness required. `id == 0` is reserved (means
 * "no wire address") and rejected; the wire-decode path always supplies
 * a non-zero id read from the producer's envelope.
 *
 * The group takes ownership: child->ops->destroy runs when the group
 * is destroyed or the child is removed by id. */
struct yetty_ycore_void_result yetty_yfigure_container_add_child(
    struct yetty_yfigure_container *group, struct yetty_yfigure_figure *child,
    uint32_t id);

/* Look up a child by its parent-scoped id. Returns NULL when the id
 * isn't bound. Lookup is O(1) — the group indexes children by id via
 * an internal hash table. */
struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(
    const struct yetty_yfigure_container *group, uint32_t id);

/* Remove and destroy the child bound to `id`. No-op when `id` isn't
 * bound (returns OK_VOID — wire-replay of a stale CMD_DELETE is benign,
 * matches the existing compositor semantics). */
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yfigure_container *group, uint32_t id);

/* Move the child bound to `id` to the top of the group's z-order
 * (rendered last). No-op when `id` isn't bound. */
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *group, uint32_t id);

/* Iterate every child in z-order (back-to-front). `fn` is called once
 * per child with its parent-scoped id, figure pointer, and the caller's
 * `user` cookie. Returning non-zero from `fn` stops the walk early and
 * the same value is returned from for_each — callers use that as the
 * "found" / hit short-circuit. Returns 0 when the visitor ran to
 * completion. */
typedef int (*yetty_yfigure_container_visitor_fn)(
    uint32_t id, struct yetty_yfigure_figure *child, void *user);

int yetty_yfigure_container_for_each(
    struct yetty_yfigure_container *group,
    yetty_yfigure_container_visitor_fn fn, void *user);

/*===========================================================================
 * Hit-test against a container's children.
 *
 * The result identifies the child whose rect contains the cursor and
 * the cursor's coordinates inside that child's own pixel space
 * (origin = child rect's top-left). figure_id == 0 means "no hit".
 *
 * Iteration is insertion order = back-to-front; the first match is
 * returned, so for overlapping children this picks the BACK-most.
 * Callers that want top-most-wins should walk children in reverse via
 * for_each + their own state, or this helper can be replaced later
 * once the wire grows an explicit z-order hint.
 *=========================================================================*/

struct yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};

struct yetty_yfigure_hit yetty_yfigure_container_hit_test(
    struct yetty_yfigure_container *container, float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_FIGURE_H */
