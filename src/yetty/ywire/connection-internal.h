/*
 * connection-internal.h — module-private struct definitions shared by
 * connection.c and channel.c. NOT installed; the public types stay opaque in
 * <yetty/ywire/connection.h> and <yetty/ywire/channel.h>.
 *
 * connection.c owns the transport + state machine + the channel array and does
 * the demux (writing into / firing channel sinks) plus the fair outbound
 * scheduler; channel.c owns the per-lane write/flush/read and hands pending
 * bytes to the scheduler. Both need the full structs, so they live here.
 */

#ifndef YETTY_YWIRE_CONNECTION_INTERNAL_H
#define YETTY_YWIRE_CONNECTION_INTERNAL_H

#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>

#include <yetty/yclass/transport-reactor.h>
#include <yetty/ycore/types.h> /* struct yetty_ycore_buffer */
#include <yetty/ywire/wire-statemachine.h>

#include <stddef.h>
#include <stdint.h>
#include <time.h> /* struct timespec — the input-barrier host-side deadline */

#define YETTY_YWIRE_CHANNEL_MAX 16

/* On-wire header carried in the args slot of every DCS YETTY_DCS_YWIRE_CHANNEL
 * envelope: four little-endian u32 fields (msg, channel_id, window, reserved).
 * Serialized byte-by-byte — never memcpy'd as a struct — so the wire format is
 * endian- and padding-independent. */
#define YETTY_YWIRE_CHANNEL_WIRE_HEADER_LEN 16u

/* Cap on bytes the outbound scheduler tops the transport queue up with per
 * pump, so a burst of pending channel data cannot balloon the queue — the rest
 * stays per-channel and want_write() keeps the loop's writable interest armed. */
#define YETTY_YWIRE_OUTBOUND_HIGH_WATER (64u * 1024u)

struct yetty_ywire_channel {
    struct yetty_ywire_connection *connection; /* owning connection (back-ref) */
    uint32_t id;
    enum yetty_ywire_channel_kind kind;
    int in_use;

    /* Outbound wire mapping. For envelope channels (rpc): wire_kind is the
     * yetty_ywire_envelope_kind and wire_code the DCS/OSC code. For the raw
     * channel: wire_code < 0 → emitted verbatim (tmux-wrapped). Dynamic
     * channels ride DCS YETTY_DCS_YWIRE_CHANNEL with the wire header in args. */
    int wire_kind;
    int wire_code;
    int has_args;

    /* Inbound demuxed bytes for pull consumers (rpc; raw fallback; dynamic).
     * Bytes in [inbuf_off, size) await read(). */
    struct yetty_ycore_buffer inbuf;
    size_t inbuf_off;
    /* Outbound bytes. write() appends; bytes in [outbuf_off, size) are pending
     * — flushed but not yet framed onto the wire (awaiting the fair scheduler
     * and, on dynamic channels, send-window credit). flush() marks the whole
     * buffer pending; the scheduler consumes chunk-wise from outbuf_off. */
    struct yetty_ycore_buffer outbuf;
    size_t outbuf_off;
    /* Bytes in [outbuf_off, flush_mark) are flushed (eligible for the wire);
     * bytes in [flush_mark, size) are still coalescing until the next flush. */
    size_t flush_mark;

    /* Push sinks. */
    yetty_ywire_channel_envelope_sink env_sink;
    yetty_ywire_channel_raw_sink raw_sink;
    void *sink_user;

    /* Dynamic-channel lifecycle. EOF/CLOSE are REQUESTED by the consumer and
     * SENT by the scheduler only once the channel's own pending DATA has
     * drained — a half-close must never overtake the bytes it seals. */
    yetty_ywire_channel_event_cb event_cb;
    void *event_user;
    int eof_requested;   /* consumer called send_eof — writes fail */
    int eof_sent;        /* EOF actually left through the scheduler */
    int close_requested; /* consumer called close (or we are answering one) */
    int close_sent;      /* our CLOSE is on the wire, awaiting the peer's */
    int close_rcvd;      /* peer's CLOSE seen */
    int remote_eof;      /* peer sent EOF — no more inbound DATA */

    /* Per-channel flow-control windows (SSH CHANNEL_WINDOW_ADJUST analog).
     * Enforced on DYNAMIC channels only; -1 on the well-known lanes means
     * unlimited. send_window = bytes the peer still lets us send;
     * recv_consumed accumulates drained inbound bytes until a WINDOW_ADJUST
     * grant goes back (at half the initial window). */
    int64_t send_window;
    int64_t recv_window_initial;
    int64_t recv_consumed;
};

struct yetty_ywire_connection {
    /* Byte path. Exactly one of the two shapes is active:
     *   - owned mode (create):  `reactor.ops` set — the connection owns its
     *     statemachine and queues outbound on the reactor transport.
     *   - attach mode (attach): `writer` set, `reactor.ops` NULL — the
     *     statemachine is BORROWED (its owner feeds it) and outbound envelopes
     *     are handed straight to the writer (the host terminal's PTY-master
     *     write path). */
    struct yetty_yclass_transport_reactor reactor; /* borrowed fd owner (owned mode) */
    yetty_ywire_connection_writer_fn writer;       /* outbound ship (attach mode) */
    void *writer_user;
    int sm_owned;
    int compressed;

    struct yetty_ywire_wire_statemachine *sm; /* inbound demux */

    /* Long-lived outbound framer + frame scratch. Every envelope this
     * connection emits (rpc-lane flushes, DATA chunks, control frames) is
     * built through this SM into emit_buf, so hot paths don't pay a
     * transient SM + LZ4F context + buffer allocation per frame. Created
     * lazily by yetty_ywire_connection_frame_envelope(); always owned by
     * the connection (unlike `sm`, which is borrowed in attach mode). */
    struct yetty_ywire_wire_statemachine *emit_sm;
    struct yetty_ycore_buffer emit_buf;

    struct yetty_ywire_channel channels[YETTY_YWIRE_CHANNEL_MAX];
    struct yetty_ywire_channel *chan_rpc;
    struct yetty_ywire_channel *chan_input;
    struct yetty_ywire_channel *chan_raw;

    /* Dynamic-channel id allocation: ids step by 2 from DYNAMIC_BASE
     * (+ role parity) and are never reused. */
    int role_acceptor;
    uint32_t next_dynamic_offset;
    yetty_ywire_accept_cb accept_cb;
    void *accept_user;

    /* Fair outbound scheduling: round-robin cursor into channels[]. */
    size_t outbound_cursor;

    yetty_ywire_resize_cb on_resize;
    void *resize_user;

    /* Host-owned input forwarding barrier (exit-hygiene without input loss).
     * When the in-pane client arms it (the no-payload INPUT_HOLD envelope at
     * the start of teardown), `input_barrier_armed` is set; the host then HOLDS
     * the keystrokes it would forward to the pane in `input_held` — its OWN user
     * input, never bytes from pane output — instead of writing them where the
     * client's close drain would consume them. When the client is gone (no
     * dynamic channels remain) the host releases the held bytes to the pane so
     * the resumed shell reads them exactly once. While armed the hold ALWAYS
     * takes the bytes (it grows): declining would send the overflow straight
     * into the drainable stream and re-lose it, so there is no cap-bypass.
     *
     * Retention is bounded by an ENFORCED absolute host-side deadline
     * (`input_barrier_deadline`, set at arm), NOT merely by an assumed
     * sub-second teardown. Past the deadline the hold refuses (the caller
     * forwards) and the held bytes are released to the pane — so a client that
     * crashes, wedges, or arms and keeps a channel open cannot make the host
     * accumulate keyboard input without limit. Release also fires on PTY EOF
     * (`_release_forced`) so an ungraceful client death recovers the bytes. */
    int input_barrier_armed;
    struct timespec input_barrier_deadline;
    struct yetty_ycore_buffer input_held;

    /* Client side of the barrier handshake: set when this connection parses the
     * host's INPUT_HOLD_ACK (proof the host armed its barrier). The teardown
     * path waits on it before detaching sinks so no exit-window key is lost to
     * the race between "client sent HOLD" and "host executed arm".
     *
     * The ACK is a bounded LEASE, not a permanent grant: the host's barrier has
     * its own deadline and will expire + resume forwarding. `input_hold_ack_time`
     * stamps when the ACK arrived; the teardown checks the lease is still fresh
     * (hold_ack_lease_valid) before running the close drain, so a client that
     * spends longer than the lease in framework/RPC teardown does NOT drain
     * after the host may have un-armed — which would re-expose the key-loss. */
    int input_hold_ack_seen;
    struct timespec input_hold_ack_time;
};

/* Drive ONE blocking inbound step: poll the inbound fd, read what's there, and
 * demux it to channels. Returns 1 if it made progress, 0 on EOF/timeout.
 * Defined in connection.c; used by yetty_ywire_channel_recv_blocking. */
int yetty_ywire_connection_pump_blocking_once(struct yetty_ywire_connection *connection);

/* Fair outbound scheduler: round-robin over channels with flushed pending
 * bytes, emitting at most one frame/chunk per channel per round into the
 * transport queue, until everything eligible is queued or the high-water cap
 * is hit. Defined in connection.c; called from channel flush, pump_writable,
 * and WINDOW_ADJUST receipt. */
struct yetty_ycore_void_result yetty_ywire_connection_pump_outbound(
    struct yetty_ywire_connection *connection);

/* Emit one control message (OPEN / EOF / CLOSE / WINDOW_ADJUST) for
 * `channel_id` straight into the transport queue (control jumps the data
 * scheduler; it is tiny and ordering-critical). */
struct yetty_ycore_void_result yetty_ywire_connection_send_control(
    struct yetty_ywire_connection *connection, enum yetty_ywire_channel_msg msg,
    uint32_t channel_id, uint32_t window);

/* Frame one envelope through the connection's long-lived emit SM into
 * connection->emit_buf (cleared on entry). On success the complete envelope
 * is emit_buf[0 .. emit_buf.size); the view is only valid until the next
 * frame call, so ship/queue (which copy) before framing again. On failure
 * emit_buf is cleared — no partial envelope ever reaches the wire. Defined
 * in connection.c; also used by channel.c for the rpc-lane flush. */
struct yetty_ycore_void_result yetty_ywire_connection_frame_envelope(
    struct yetty_ywire_connection *connection, enum yetty_ywire_envelope_kind kind, int code,
    int has_args, int compressed, const void *args, size_t args_len, const void *body,
    size_t body_len);

/* Account `consumed` drained inbound bytes on a dynamic channel and send the
 * WINDOW_ADJUST grant back once half the initial window has been consumed. */
struct yetty_ycore_void_result yetty_ywire_connection_grant_credit(
    struct yetty_ywire_channel *channel, size_t consumed);

/* Release a closed channel's slot once its close handshake is complete and the
 * inbound buffer is drained. */
void yetty_ywire_connection_maybe_release(struct yetty_ywire_channel *channel);

#endif /* YETTY_YWIRE_CONNECTION_INTERNAL_H */
