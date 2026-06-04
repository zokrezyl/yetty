/* drawable-iterator.h — streaming command iterator over a ydraw envelope.
 *
 * Pulls already-decoded bytes from the wire-statemachine and reassembles
 * them into commands. Each call to _next() returns one command of one of
 * two kinds:
 *
 *   ADD     — followed by a flyweight: a drawable to place on the canvas.
 *             For ADD, command.entry is valid; .data points into the
 *             iter's scratch and is stable until the next _next call.
 *   DELETE  — followed by an id: an addressable entity to remove.
 *
 * Commands are recognised at the iter level by the wire type word:
 *
 *   type == YETTY_YDRAW_CMD_DELETE           → DELETE  (12 bytes:
 *                                              type | id | payload_size=0)
 *   anything else                            → ADD     (flyweight via the
 *                                              registry, FAM or fixed)
 *
 * Future addressable-ADD producers may set the HAS_ID bit on the type
 * word to embed an id alongside the drawable; that id lives in the
 * flyweight bytes — the iter does not unpack it.
 *
 * The iter runs inside the layer's process_input coro. When the
 * wire-statemachine has no more bytes right now and the envelope
 * terminator has not been seen, the iter yields the coro and resumes
 * when fresh bytes arrive. From the caller's view, _next is synchronous
 * and returns OK (command populated), EOE (clean envelope end), or ERR.
 *
 * Lifecycle:
 *   - init: zero-out, no allocation yet.
 *   - next: returns OK / EOE / ERR. After OK, the command is valid until
 *     the next _next call. After EOE the iter should be destroyed.
 *   - destroy: frees scratch.
 *
 * Single-use scratch: after _next returns OK with kind=ADD and the caller
 * has consumed command.entry, the next _next call may overwrite the
 * scratch. Do NOT cache flyweight.data beyond the next iter step.
 */
#ifndef YETTY_YDRAW_CORE_DRAWABLE_ITERATOR_H
#define YETTY_YDRAW_CORE_DRAWABLE_ITERATOR_H

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list-registry.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ywire_wire_statemachine;

enum yetty_ydraw_drawable_iterator_status {
    YETTY_YDRAW_ITERATOR_OK = 0, /* command populated; caller consumes then re-calls */
    YETTY_YDRAW_ITERATOR_EOE,    /* end-of-envelope: clean stop */
    YETTY_YDRAW_ITERATOR_ERR,    /* internal error (truncated, alloc, …) */
};

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_iterator_status,
                      enum yetty_ydraw_drawable_iterator_status);

/* Verb / kind of the yielded command. */
enum yetty_ydraw_command_kind {
    YETTY_YDRAW_COMMAND_ADD,    /* operand: a drawable flyweight */
    YETTY_YDRAW_COMMAND_DELETE, /* operand: the id of the target entity */
    YETTY_YDRAW_COMMAND_UPDATE, /* operand: target id + opaque prim payload */
};

/* CMD_UPDATE payload view: id + a slice pointing into iter scratch.
 * Bytes belong to the targeted primitive's factory — no canvas-level
 * interpretation. Lifetime mirrors flyweight.data: valid only until the
 * next iter step. */
struct yetty_ydraw_command_update {
    uint32_t id;
    const uint8_t *data;
    uint32_t size;
};

/* One decoded wire command. The kind selects which union arm is valid. */
struct yetty_ydraw_command {
    enum yetty_ydraw_command_kind kind;
    union {
        /* kind == ADD: the drawable. data points into iter scratch. */
        struct yetty_ydraw_drawable_list_entry entry;
        /* kind == DELETE: the named entity to remove. */
        uint32_t id;
        /* kind == UPDATE: target id + per-prim opaque payload. */
        struct yetty_ydraw_command_update update;
    };
};

struct yetty_ydraw_drawable_iterator {
    /* Public: valid after _next returns OK. */
    struct yetty_ydraw_command command;

    /* Scene bounds parsed from the 24-byte framed envelope header. Valid
     * once header_done flips to true. */
    float scene_min_x, scene_min_y, scene_max_x, scene_max_y;

    /* Private: caller should not touch. */
    uint8_t *scratch;
    uint32_t scratch_cap;
    uint32_t filled;        /* bytes currently buffered for the in-flight record */
    uint32_t total_size;    /* full record stride, once known (0 = unknown) */
    uint32_t header_filled; /* bytes of the envelope header already pulled */
    bool header_done;       /* envelope header parsed and validated */

    struct yetty_ywire_wire_statemachine *wire_statemachine;
    const struct yetty_ydraw_drawable_list_registry *reg;
};

/* Initialise. No allocation; scratch grows on demand. */
struct yetty_ycore_void_result yetty_ydraw_drawable_iterator_init(
    struct yetty_ydraw_drawable_iterator *iter,
    struct yetty_ywire_wire_statemachine *wire_statemachine,
    const struct yetty_ydraw_drawable_list_registry *reg);

/* Free the scratch. Safe on a zero-inited iter (no-op). */
void yetty_ydraw_drawable_iterator_destroy(struct yetty_ydraw_drawable_iterator *iter);

/* Advance one command. See enum docs above for status semantics. */
struct yetty_ydraw_drawable_iterator_status_result yetty_ydraw_drawable_iterator_next(
    struct yetty_ydraw_drawable_iterator *iter);

/* Parse one command from an in-memory byte buffer (no wire-statemachine
 * involvement). Used for the GROUP-body inner loop in scene-canvas where
 * the bytes are already buffered as the GROUP record's payload.
 *
 * On success: populates *out_command with .entry.data pointing into
 * `bytes` (caller-owned, must stay alive while command is used) and
 * returns the number of bytes consumed.
 *
 * Layouts mirror the wire iter:
 *   - DELETE             : 12 bytes, kind=DELETE, command.id = bytes[4..8].
 *   - HAS_ID record (e.g. GROUP)
 *                        : 12 + payload_size bytes, kind=ADD, flyweight
 *                          points at the full record (data[0] carries
 *                          the type, data[1] the id, data[2] the size).
 *   - Plain flyweight    : ops->size bytes, kind=ADD, flyweight populated
 *                          via the registry.
 */
struct yetty_ycore_size_result yetty_ydraw_drawable_command_parse(
    const struct yetty_ydraw_drawable_list_registry *reg, const uint8_t *bytes, uint32_t bytes_len,
    struct yetty_ydraw_command *out_command);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_DRAWABLE_ITERATOR_H */
