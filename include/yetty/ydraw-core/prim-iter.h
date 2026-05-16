/* prim-iter.h — streaming primitive iterator.
 *
 * Pulls already-decoded primitive bytes from an OSC state machine (the
 * source is the SM, not a buffer) and reassembles them into typed
 * flyweight views. Hands the canvas one prim at a time; survives
 * mid-envelope yields (would-block) — the iter owns a scratch buffer
 * that carries partial-prim bytes across re-entries.
 *
 * Wire format:
 *   - First 4 bytes of every prim: u32 type.
 *   - FAM prims (FONT, TEXT_SPAN, complex, cmd): bytes 4..8 = u32 payload_size;
 *     total = 8 + payload_size.
 *   - Fixed-size SDF / glyph prims: ops->size returns a constant determined
 *     by the type word alone.
 *
 * 8 bytes is universally enough to call ops->size — fixed-size prims
 * ignore byte 4 onward, FAM prims encode their length there. After the
 * first 8 bytes are buffered we know the total stride; grow the scratch
 * if needed and keep pulling.
 *
 * Lifecycle:
 *   - init: zero-out, no allocation yet.
 *   - next: returns OK/WOULD_BLOCK/DONE/ERR. On OK, iter->fw is valid
 *     until the next _next call. On WOULD_BLOCK, the iter has parked
 *     partial state and the canvas should park its other per-envelope
 *     state (font_map, attach_list) too — the SM will re-dispatch
 *     process_input later.
 *   - destroy: frees scratch.
 *
 * Single-use scratch: after _next returns OK and the caller has consumed
 * iter->fw, the next _next call may overwrite the scratch. Do NOT cache
 * fw.data beyond the next iter step.
 */
#ifndef YETTY_YDRAW_CORE_PRIM_ITER_H
#define YETTY_YDRAW_CORE_PRIM_ITER_H

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/flyweight.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ywire_wire_statemachine;

enum yetty_ydraw_drawable_iter_status {
    YETTY_PRIM_ITER_OK = 0,        /* fw populated; caller consumes then re-calls */
    YETTY_PRIM_ITER_WOULD_BLOCK,   /* SM has no more bytes; envelope not at_end yet */
    YETTY_PRIM_ITER_DONE,          /* SM at_end and scratch empty — clean tail */
    YETTY_PRIM_ITER_ERR,           /* internal error (truncated, alloc, …) */
};

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_iter_status,
                      enum yetty_ydraw_drawable_iter_status);

struct yetty_ydraw_drawable_iter {
    /* Public: valid after _next returns OK. fw.data points into the
     * iter's scratch — stable until the next _next call. */
    struct yetty_ydraw_drawable_flyweight fw;

    /* Scene bounds parsed from the 24-byte framed envelope header. Valid
     * once header_done flips to true (after the header bytes have been
     * pulled from the SM). */
    float    scene_min_x, scene_min_y, scene_max_x, scene_max_y;

    /* Private: caller should not touch. */
    uint8_t *scratch;
    uint32_t scratch_cap;
    uint32_t filled;          /* bytes currently buffered for the in-flight prim / header */
    uint32_t total_size;      /* full prim stride, once known from the 8-byte header (0 = unknown) */
    uint32_t header_filled;   /* bytes of the 24-byte envelope header already pulled */
    bool     header_done;     /* true once envelope header is parsed and validated */

    struct yetty_ywire_wire_statemachine             *sm;
    const struct yetty_ydraw_flyweight_registry *reg;
};

/* Initialise. No allocation; scratch grows on demand. */
struct yetty_ycore_void_result yetty_ydraw_drawable_iter_init(
    struct yetty_ydraw_drawable_iter *iter,
    struct yetty_ywire_wire_statemachine *sm,
    const struct yetty_ydraw_flyweight_registry *reg);

/* Free the scratch. Safe on a zero-inited iter (no-op). */
void yetty_ydraw_drawable_iter_destroy(struct yetty_ydraw_drawable_iter *iter);

/* Advance one prim. See enum docs above for status semantics. The
 * result struct's `value` is the status; on ERR, .ok=0 and .error.msg
 * describes the failure. */
struct yetty_ydraw_drawable_iter_status_result yetty_ydraw_drawable_iter_next(
    struct yetty_ydraw_drawable_iter *iter);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_PRIM_ITER_H */
