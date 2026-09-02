/* ydraw control cmds — non-drawing primitives that have side effects on
 * the receiving canvas at decode time.
 *
 * Type-id space for the ydraw wire format:
 *   [0x00000000, 0x0000FFFF]  cmds (this header)
 *   [0x10000000, 0x1FFFFFFF]  SDF (paint primitives, generated)
 *   [0x40000000, 0x7FFFFFFF]  drawable-list entry (FONT, TEXT_DRAWABLE_LIST)
 *   [0x80000000, 0xFFFFFFFF]  complex (yplot, yimage, ...)
 *
 * Cmds use the same FAM wire layout as drawable-list entries:
 *     u32 type
 *     u32 payload_size  (bytes of payload, 4-aligned)
 *     u8  payload[payload_size]
 * — so the iterator walks them with the same stride machinery.
 */
#ifndef YETTY_YDRAW_CORE_CMDS_H
#define YETTY_YDRAW_CORE_CMDS_H

#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YDRAW_CMD_BASE 0x00000000u
#define YETTY_YDRAW_CMD_END 0x0000FFFFu

/* Top bit of the type word. When set on the wire, a u32 id follows the
 * type word — for ADD this names the drawable so it can be DELETEd later;
 * for DELETE the id field carries the target. */
#define YETTY_YDRAW_HAS_ID_FLAG 0x80000000u

/*------------------------------------------------------------------------*
 * Record-kind encoding — bits 31:30 of the type word.                    *
 *                                                                        *
 * The top two bits of every wire type word classify what the parser      *
 * should expect after the type word:                                     *
 *                                                                        *
 *   bits 31:30 = 00   anonymous content    (today's plain prim)          *
 *                                            shape: [type] [payload…]    *
 *                                                                        *
 *   bits 31:30 = 01   id + content         (declaration of an id'd       *
 *                                            entity carrying its body)   *
 *                                            shape: [type] [id]          *
 *                                                    [payload_size]      *
 *                                                    [payload…]          *
 *                                                                        *
 *   bits 31:30 = 10   id only              (back-reference / membership; *
 *                                            no body — refers to a       *
 *                                            previously-declared id)     *
 *                                            shape: [type] [id]          *
 *                                                                        *
 *   bits 31:30 = 11   reserved             (parser must reject)          *
 *                                                                        *
 * Inside a group body, records of all three kinds may interleave: a      *
 * group's children may be anonymous leaf prims (kind 00), declarations   *
 * of sub-groups (kind 01), or back-references to groups declared         *
 * elsewhere in the envelope (kind 10). The receiver dispatches on the    *
 * top two bits to know how to consume each record.                       *
 *                                                                        *
 * Today's existing constants (CMD_GROUP, CMD_UPDATE in kind-10 bits with *
 * an embedded body; TEXT_DRAWABLE_LIST / FONT in kind-01 bits while anonymous;    *
 * SDF prims spread across various ranges) predate this scheme and are    *
 * NOT yet consistent with these bit assignments. A follow-up sweep will  *
 * renumber them; for now use the kind-bit constants below for any        *
 * record introduced by the new group/coord-frame work.                   *
 *------------------------------------------------------------------------*/
#define YETTY_YDRAW_KIND_MASK 0xC0000000u
#define YETTY_YDRAW_KIND_ANON_CONTENT 0x00000000u /* 00 — payload follows */
#define YETTY_YDRAW_KIND_DECL 0x40000000u         /* 01 — id + payload */
#define YETTY_YDRAW_KIND_REF 0x80000000u          /* 10 — id only */
#define YETTY_YDRAW_KIND_RESERVED 0xC0000000u     /* 11 — invalid */

/* CMD_GROUP_REF: include an already-declared group (by id) as a child of
 * the current parent group. Used when the same sub-group can be reached
 * from multiple parents, or when the producer prefers to declare every
 * group at envelope top-level and only reference them from parents.
 *
 * Wire layout (kind=REF / 10): [type] [id]
 *
 * Receiver behaviour: add `id` to the current parent group's child list.
 * Forward references (id declared later in the same envelope) are
 * allowed; the receiver resolves the link after the whole envelope has
 * been walked. A reference to an id that never arrives is treated as
 * "skip" (the child slot stays empty), not an error. */
#define YETTY_YDRAW_CMD_GROUP_REF (YETTY_YDRAW_KIND_REF | 0x00000020u)

/* CMD_PAINT_Z / CMD_PAINT_Z_END: an explicit paint-z SCOPE for the records
 * that follow, so drawables that carry no z field of their own — complexes
 * (yplot, yimage, ymesh, yvideo, yshadertoy) — can be stacked at an
 * arbitrary depth like any SDF shape.
 *
 * Wire layout (kind=REF / 10):
 *   CMD_PAINT_Z:     [type] [z]   — push `z` (a signed int32, on the wire as
 *                                   its raw 32 bits) as the ambient paint-z.
 *   CMD_PAINT_Z_END: [type] [pad] — pop the innermost ambient paint-z.
 *
 * While an ambient paint-z is active, EVERY render-leaf record ingested
 * inside the scope takes that z as its paint key's z, overriding any z the
 * record carries in its own wire words. This is what lets one figure (a
 * plot emits a complex plus its axis-label / legend primitives) move as a
 * unit. Scopes nest; the innermost wins. Outside any scope, records keep
 * their own wire z (SDF z word, text `layer`, complex default 0), so
 * existing producers are unaffected. Unbalanced scopes are reset at the
 * envelope boundary. */
#define YETTY_YDRAW_CMD_PAINT_Z (YETTY_YDRAW_KIND_REF | 0x00000030u)
#define YETTY_YDRAW_CMD_PAINT_Z_END (YETTY_YDRAW_KIND_REF | 0x00000031u)

/* CMD_NODE_ID: assign an addressable id to the IMMEDIATELY FOLLOWING record.
 *
 * Wire layout (kind=REF / 10): [type] [id]
 *
 * This is how a COMPLEX carries its own id on the wire (`Plot(id=7)` in the
 * contract, drawable-use-cases.md): complex type words occupy the whole
 * 0x8XXXXXXX range, so they cannot gain a HAS_ID bit the way SDF primitives
 * do — instead the producer emits CMD_NODE_ID(id) directly before the complex
 * record. The receiver folds the id with the enclosing group path and binds
 * the complex as the addressable node at that path (update/delete/replace by
 * that path). The id latches onto the NEXT COMPLEX record in the current
 * scope — interleaved primitives (a plot's axis/legend prims) do not consume
 * it; a GROUP declaration or the batch end drops an unconsumed latch. */
#define YETTY_YDRAW_CMD_NODE_ID (YETTY_YDRAW_KIND_REF | 0x00000032u)

/* CMD_PATH: the ABSOLUTE ancestor path for the IMMEDIATELY FOLLOWING
 * update/delete command, so a nested node can be addressed at any depth
 * without re-declaring (and thereby replacing) its ancestors.
 *
 * Wire layout: [type] [count] [id0] [id1] ... [id(count-1)]
 *   — count in [1, YETTY_YDRAW_CMD_PATH_MAX_IDS]; ids outermost first,
 *     measured from the ROOT (never relative to the ambient group scope).
 *
 * The receiver folds id0..id(count-1) into the ancestor-scope key; the next
 * UPDATE/DELETE's own id is the FINAL path component:
 *
 *     update(7.10000, data)        ->  CMD_PATH[7]        UPDATE(10000, data)
 *     update(dialog.row2.chart,d)  ->  CMD_PATH[dlg,row2] UPDATE(chart, d)
 *     delete(a.b.c)                ->  CMD_PATH[a,b]      DELETE(c)
 *
 * The latch is consumed by exactly one following UPDATE/DELETE — or, at
 * TOP level, by a GROUP declaration: the addressed-insert form. A LIVE key
 * at the latched path REOPENS that nested group in place (exact-subtree,
 * like any depth-1 reopen — how a widget toolkit repaints one nested
 * widget without re-declaring its ancestors); a fresh key at a latched
 * path (create-missing-descendant) is not yet supported and the command is
 * skipped whole. Any other record or the batch end drops the latch.
 * Depth-1 targets need no CMD_PATH (a bare id folds at the ambient scope,
 * root at top level). */
#define YETTY_YDRAW_CMD_PATH (YETTY_YDRAW_KIND_REF | 0x00000033u)
#define YETTY_YDRAW_CMD_PATH_MAX_IDS 8u

/* Group-state fields for CMD_UPDATE addressed at a GROUP node. What `update`
 * means depends on the node's kind: a complex receives its runtime payload; a
 * GROUP receives a state-field write. target_field selects the field:
 *
 *   GROUP_FIELD_OFFSET: payload = [f32 x][f32 y] — the group's translation
 *   offset in pixels (ABSOLUTE values, not deltas). The ONE mover of the
 *   model: children keep their declared local coordinates; rendering projects
 *   them at local + accumulated ancestor offsets, clipped to the insertion's
 *   row span. Content projected out of view is detached (no rows, no history)
 *   and re-attaches when the offset brings it back. */
#define YETTY_YDRAW_GROUP_FIELD_OFFSET 0x00000001u
/* GROUP state field: the CLIP rectangle (non-layout PROJECTION state —
 * never affects row span or placement). update payload:
 * [field u32][f32 x][f32 y][f32 w][f32 h], local content space. A zero-size
 * rect disables clipping. Descendant projection intersects ancestor clips. */
#define YETTY_YDRAW_GROUP_FIELD_CLIP 0x00000002u

/* CMD_RESERVE: DECLARE this batch's insertion row span instead of deriving
 * it from the content bottom — the viewport primitive.
 *
 * Wire layout (kind=REF / 10): [type] [height_px]
 *
 * The insertion's span becomes max(1, ceil(height_px / row_height)) rows.
 * Content taller than the declared span NEVER extends the reservation — it
 * is clipped by the projection (drawable-use-cases.md §1a): emit a tall
 * scrollable page once, pan it with the enclosing group's offset. Without
 * CMD_RESERVE the span derives from the content bottom as always. */
#define YETTY_YDRAW_CMD_RESERVE (YETTY_YDRAW_KIND_REF | 0x00000034u)

/* CMD_ZERO: clear the receiving ydraw canvas AND reset the cursor to
 * (col=0, row=0). The canvas's cursor-set callback fires on the reset, so
 * sibling layers (text/vterm) see the cursor move too — that's how the
 * "all layers cursor" semantic propagates without this header reaching
 * outside the ydraw module.
 *
 * Empty payload. Use at the start of every full-redraw frame buffer in
 * GUI / fullscreen producers (ygui sends one CMD_ZERO + the new prims per
 * frame, eliminating the separate YDRAW_CLEAR DCS envelope). */
#define YETTY_YDRAW_CMD_ZERO 0x00000000u

/* CMD_DELETE: remove the entity / addressable drawable named by `id`.
 * Wire layout: (type|HAS_ID_FLAG) | id | payload_size=0.
 *
 * The drawable-iterator surfaces this with command.kind=DELETE,
 * command.id=<target>. Canvas variants without an addressable-entity
 * model (e.g. scrolling-canvas today) treat it as a no-op. */
#define YETTY_YDRAW_CMD_DELETE (YETTY_YDRAW_HAS_ID_FLAG | 0x00000001u)

/*------------------------------------------------------------------------*
 * CRITICAL — type-word namespace collision risk:                         *
 *                                                                        *
 * cmd values that set HAS_ID_FLAG (CMD_DELETE, CMD_GROUP, CMD_UPDATE)    *
 * land in the same 0x8XXXXXXX range as complex type_ids             *
 * (YETTY_YDRAW_COMPLEX_TYPE_BASE = 0x80000000). The drawable iterator    *
 * disambiguates by *exact-match* on each cmd constant before falling     *
 * through to the prim registry — so each cmd value MUST stay distinct    *
 * from every complex type_id ever assigned.                         *
 *                                                                        *
 * Existing complex prim type_ids cluster at 0x80000003 (yplot),          *
 * 0x80000004 (yimage), 0x80000005 (ymesh). To avoid future clashes new   *
 * cmds are assigned numbers from 0x10 upward; new prim type_ids should   *
 * stay below 0x10 OR move to 0x81000000+ if the cmd space ever grows.    *
 *------------------------------------------------------------------------*/

/* CMD_UPDATE: deliver an opaque payload to the addressable drawable named
 * by `id`. Sibling of DELETE — same 12-byte framing plus a `payload_size`
 * tail whose semantics belong entirely to the targeted primitive's
 * factory. The canvas resolves `id` to a figure_instance and calls
 *   factory->update_instance(instance, payload, payload_size)
 * which interprets the bytes per the prim's schema (yplot uses
 * `[buffer_index u32][sample_offset u32][count u32][samples × f32]`).
 *
 * Wire layout: (type|HAS_ID_FLAG) | id | payload_size | payload[payload_size].
 *
 * The drawable-iterator surfaces this with command.kind=UPDATE,
 * command.update.{id,data,size}. Canvases without an addressable model
 * drop the record. */
#define YETTY_YDRAW_CMD_UPDATE (YETTY_YDRAW_HAS_ID_FLAG | 0x00000010u)

/* CMD_GROUP: open or re-open a named entity scope. The payload is a
 * stream of nested commands belonging to this entity.
 *
 * Wire layout: (type|HAS_ID_FLAG) | id | payload_size | payload[payload_size]
 *
 * The drawable-iterator surfaces this as an ADD with entry.data
 * pointing at the full record; canvases with an entity model (e.g.
 * scene-canvas) recognise the type, look up / create the entity by id,
 * and walk the payload bytes as nested commands attached to that
 * entity. Canvas variants without entities (scrolling-canvas) drop it. */
#define YETTY_YDRAW_CMD_GROUP (YETTY_YDRAW_HAS_ID_FLAG | 0x00000002u)

struct yetty_ydraw_drawable_list;

/* Append a CMD_ZERO at the current write head of the buffer. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_cmd_zero(
    struct yetty_ydraw_drawable_list *buf);

/* Drawable-list entry handler for the cmd tier — returns the cmd base_ops which
 * stride by the FAM `8 + payload_size` bytes (same as drawable-list entries).
 * Register at startup with:
 *   yetty_ydraw_drawable_list_registry_add(reg,
 *       YETTY_YDRAW_CMD_BASE, YETTY_YDRAW_CMD_END,
 *       yetty_ydraw_cmd_handler);
 */
struct yetty_ydraw_drawable_list_entry_ops_ptr_result yetty_ydraw_cmd_handler(
    uint32_t drawable_type);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_CMDS_H */
