/*
 * yfigure wire format — record + admin sub-cmd constants.
 *
 * Every wire envelope on the figure-tree OSC code is a sequence of
 * records. Each record:
 *
 *   u32 length     bytes of payload that follow this header
 *   u32 id         parent-scoped figure id; 0 = admin / self-target
 *   bytes payload[length]
 *
 * Length-first so a pipeline linter / proxy can walk and validate
 * without knowing ids or figure semantics. id=0 = admin: the receiving
 * container consumes the payload itself (sub-cmd codes below). id!=0:
 * the container resolves id to a child figure and hands the payload to
 * the child's process_input(self, sm, length).
 *
 * Admin record payload layout:
 *   u32 admin_op    sub-cmd code (yetty_yfigure_wire_admin_op below)
 *   bytes admin_payload   sub-cmd-specific, length = (record length - 4)
 */
#ifndef YETTY_YFIGURE_WIRE_H
#define YETTY_YFIGURE_WIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Top-level record header. */
struct yetty_yfigure_header {
    uint32_t length;
    uint32_t id;
};

/* Admin record sub-cmds — payload of an id==0 record begins with one
 * of these as a u32 discriminator. */
enum yetty_yfigure_wire_admin_op {
    /* Drop every child of the receiving container. No further fields. */
    YETTY_YFIGURE_ADMIN_CLEAR_ALL = 1,

    /* Create a child. Payload layout after admin_op:
     *   u32   child_id
     *   u32   kind             (one of yetty_yfigure_wire_kind)
     *   f32   rect_min_x, rect_min_y, rect_max_x, rect_max_y
     *   u32   init_payload_bytes
     *   bytes init[init_payload_bytes]   forwarded to child's
     *                                    process_input(sm, init_payload_bytes)
     */
    YETTY_YFIGURE_ADMIN_CREATE_CHILD = 2,

    /* Drop a single child. Payload after admin_op:
     *   u32   child_id
     */
    YETTY_YFIGURE_ADMIN_DELETE_CHILD = 3,

    /* Update a child's rect without rebuilding its state. Payload:
     *   u32   child_id
     *   f32   rect_min_x, rect_min_y, rect_max_x, rect_max_y
     */
    YETTY_YFIGURE_ADMIN_SET_CHILD_RECT = 4,

    /* Update THIS container's own rect. Payload:
     *   f32   rect_min_x, rect_min_y, rect_max_x, rect_max_y
     */
    YETTY_YFIGURE_ADMIN_SET_RECT = 5,

    /* Set a child's stacking order (z). Payload after admin_op:
     *   u32   child_id
     *   i32   z                (signed; higher renders in front)
     * Additive to CREATE_CHILD: a child is created at z=0 and the
     * producer follows up with SET_CHILD_Z only when z != 0 / changes.
     * The container re-sorts its children by (z, insertion-seq). */
    YETTY_YFIGURE_ADMIN_SET_CHILD_Z = 6,

    /* Show/hide a child without destroying it. Payload after admin_op:
     *   u32   child_id
     *   u32   hidden           (0 = visible, non-zero = hidden)
     * A hidden child is skipped for render and hit but keeps its figure
     * and last body — re-showing it costs one record, not a re-CREATE +
     * full-body re-ship. Used for dialogs that open/close repeatedly. */
    YETTY_YFIGURE_ADMIN_SET_CHILD_HIDDEN = 7,

    /* Set a scrollable child's scroll offset — the content coordinate
     * shown at the child's rect top-left. Payload after admin_op:
     *   u32   child_id
     *   f32   scroll_x, scroll_y
     * The child keeps its already-shipped content; only the view moves, so
     * this is a single record, no body re-ship. A client that owns a
     * scrollable surface (e.g. an nvim plugin viewport) emits this as the
     * user scrolls within its region. Ignored by children that aren't
     * scrollable (no set_scroll slot). */
    YETTY_YFIGURE_ADMIN_SET_CHILD_SCROLL = 8,

    /* Declare a scrollable child's content extent in px. Payload:
     *   u32   child_id
     *   f32   content_w, content_h
     * The child's rect stays the visible window; content larger than the
     * rect makes it a scroll viewport. Optional — a child that derives its
     * content extent from the shipped body (scene bounds) never needs this. */
    YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE = 9,
};

/* Figure-kind codes — registered with yetty_yfigure_registry by each
 * figure-kind module's _register_factory call. Reserved range 0..0xFFFF
 * for built-in kinds; user kinds start at 0x10000.
 *
 * YPLOT/YIMAGE/YVIDEO/YZOO/YJUNGLE are sibling figure kinds emitted by
 * ygui's complex producer widgets — they live alongside the widget's
 * own chrome ygrid in the parent container rather than being inlined
 * into it. The receiver still renders them via the ygrid factory today
 * (their payload is an SDF/glyph prim stream), but the distinct kind
 * code keeps the wire semantically labelled and leaves room for
 * kind-specific renderers later. */
enum yetty_yfigure_wire_kind {
    YETTY_YFIGURE_KIND_CONTAINER = 1,
    YETTY_YFIGURE_KIND_YGRID = 2,
    YETTY_YFIGURE_KIND_YMGUI = 3,
    YETTY_YFIGURE_KIND_YRDAWN = 4,
    YETTY_YFIGURE_KIND_YPLOT = 5,
    YETTY_YFIGURE_KIND_YIMAGE = 6,
    YETTY_YFIGURE_KIND_YVIDEO = 7,
    YETTY_YFIGURE_KIND_YZOO = 8,
    YETTY_YFIGURE_KIND_YJUNGLE = 9,

    /* Standalone Shadertoy-style shader figure. Unlike the producer
     * kinds above (which reuse the ygrid factory over an SDF/glyph prim
     * stream), this has its own factory + renderer in src/yetty/yshadertoy.
     * Its CREATE_CHILD init payload is the raw WGSL shader text. */
    YETTY_YFIGURE_KIND_YSHADERTOY = 10,
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_WIRE_H */
