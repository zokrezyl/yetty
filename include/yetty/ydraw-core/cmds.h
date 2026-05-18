/* ydraw control cmds — non-drawing primitives that have side effects on
 * the receiving canvas at decode time.
 *
 * Type-id space for the ydraw wire format:
 *   [0x00000000, 0x0000FFFF]  cmds (this header)
 *   [0x10000000, 0x1FFFFFFF]  SDF (paint primitives, generated)
 *   [0x40000000, 0x7FFFFFFF]  flyweight (FONT, TEXT_SPAN)
 *   [0x80000000, 0xFFFFFFFF]  complex (yplot, yimage, ...)
 *
 * Cmds use the same FAM wire layout as flyweight prims:
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

/* CMD_ZERO: clear the receiving ydraw canvas AND reset the cursor to
 * (col=0, row=0). The canvas's cursor-set callback fires on the reset, so
 * sibling layers (text/vterm) see the cursor move too — that's how the
 * "all layers cursor" semantic propagates without this header reaching
 * outside the ydraw module.
 *
 * Empty payload. Use at the start of every full-redraw frame buffer in
 * GUI / fullscreen producers (ygui sends one CMD_ZERO + the new prims per
 * frame, eliminating the separate YDRAW_CLEAR OSC envelope). */
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
 * land in the same 0x8XXXXXXX range as complex-prim type_ids             *
 * (YETTY_YDRAW_COMPLEX_TYPE_BASE = 0x80000000). The drawable iterator    *
 * disambiguates by *exact-match* on each cmd constant before falling     *
 * through to the prim registry — so each cmd value MUST stay distinct    *
 * from every complex-prim type_id ever assigned.                         *
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
 * The drawable-iterator surfaces this as an ADD with flyweight.data
 * pointing at the full record; canvases with an entity model (e.g.
 * scene-canvas) recognise the type, look up / create the entity by id,
 * and walk the payload bytes as nested commands attached to that
 * entity. Canvas variants without entities (scrolling-canvas) drop it. */
#define YETTY_YDRAW_CMD_GROUP (YETTY_YDRAW_HAS_ID_FLAG | 0x00000002u)

struct yetty_ydraw_draw_list;

/* Append a CMD_ZERO at the current write head of the buffer. */
struct yetty_ycore_void_result yetty_ydraw_draw_list_add_cmd_zero(
    struct yetty_ydraw_draw_list *buf);

/* Flyweight handler for the cmd tier — returns the cmd base_ops which
 * stride by the FAM `8 + payload_size` bytes (same as flyweight prims).
 * Register at startup with:
 *   yetty_ydraw_flyweight_registry_add(reg,
 *       YETTY_YDRAW_CMD_BASE, YETTY_YDRAW_CMD_END,
 *       yetty_ydraw_cmd_handler);
 */
struct yetty_ydraw_drawable_base_ops_ptr_result yetty_ydraw_cmd_handler(uint32_t drawable_type);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_CMDS_H */
