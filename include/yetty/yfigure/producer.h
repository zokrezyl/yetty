/*
 * producer.h — generic figure-tree wire producer (the emit side).
 *
 * The container (container.c) is the RECEIVER: it consumes a stream of
 * wire records (CREATE_CHILD / SET_CHILD_* / body records — see wire.h)
 * and builds/updates a live figure tree. This is its mirror image: an
 * object that an out-of-process client uses to PRODUCE that same record
 * stream and ship it, wrapped in the shared YCOMPOSITOR_BIN yface
 * envelope, down an output pty to a hosting yetty.
 *
 * It is ygui-free on purpose. The ygui framework has its own emit path
 * for ygui widget trees; this is the lower-level primitive any app — a
 * ygui app, a bare ydraw tool (ymaze), or the window chrome (ychrome) —
 * uses to put figures on a remote pane without reimplementing the wire
 * framing every time.
 *
 * Records accumulate across emit_* calls and are flushed as ONE envelope
 * by flush(); the buffer is cleared on a successful flush. Call order is
 * preserved on the wire, so the receiver applies CREATE_CHILD before the
 * SET_CHILD_Z / body records that reference the new id.
 *
 * The output pty is borrowed — the caller owns its lifetime.
 */
#ifndef YETTY_YFIGURE_PRODUCER_H
#define YETTY_YFIGURE_PRODUCER_H

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <stdint.h>

struct yetty_platform_pty;
struct yetty_yfigure_producer;

YETTY_YRESULT_DECLARE(yetty_yfigure_producer_ptr, struct yetty_yfigure_producer *);

/* Create a producer that ships its envelopes down `output_pty` (borrowed). */
struct yetty_yfigure_producer_ptr_result yetty_yfigure_producer_create(
    struct yetty_platform_pty *output_pty);

struct yetty_ycore_void_result yetty_yfigure_producer_destroy(
    struct yetty_yfigure_producer *producer);

/* Create a child figure of the target container. `kind` is one of
 * yetty_yfigure_wire_kind. `init_bytes` is the kind-specific init payload
 * forwarded to the child's process_input (for a ygrid figure this is a
 * ydraw drawable-list byte stream). */
struct yetty_ycore_void_result yetty_yfigure_producer_create_child(
    struct yetty_yfigure_producer *producer, uint32_t child_id, uint32_t kind,
    struct yetty_ycore_rectangle rect, const uint8_t *init_bytes, uint32_t init_len);

/* Re-ship a child's body (id != 0 record) — replaces the child's content
 * without a re-CREATE. For a ygrid figure this is a fresh drawable-list
 * stream. */
struct yetty_ycore_void_result yetty_yfigure_producer_figure_body(
    struct yetty_yfigure_producer *producer, uint32_t child_id, const uint8_t *bytes, uint32_t len);

struct yetty_ycore_void_result yetty_yfigure_producer_set_child_z(
    struct yetty_yfigure_producer *producer, uint32_t child_id, int32_t z);

struct yetty_ycore_void_result yetty_yfigure_producer_set_child_rect(
    struct yetty_yfigure_producer *producer, uint32_t child_id, struct yetty_ycore_rectangle rect);

struct yetty_ycore_void_result yetty_yfigure_producer_delete_child(
    struct yetty_yfigure_producer *producer, uint32_t child_id);

/* Ship everything accumulated since the last flush as one yface envelope
 * and clear the record buffer. A no-op (and OK) when nothing is pending. */
struct yetty_ycore_void_result yetty_yfigure_producer_flush(
    struct yetty_yfigure_producer *producer);

/* Same as flush(), but writes the envelope straight to `fd` with a BLOCKING
 * write instead of the (async) output pty. Use this at process teardown, where
 * the event loop has stopped and an async pty write can never drain — the same
 * reason yetty_ygui_framework_clear_remote_fd writes to the fd directly. */
struct yetty_ycore_void_result yetty_yfigure_producer_flush_fd(
    struct yetty_yfigure_producer *producer, int fd);

#endif /* YETTY_YFIGURE_PRODUCER_H */
