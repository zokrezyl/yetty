/*
 * connection.h — the multiplexed wire link (SSH's "connection layer").
 *
 * This is the one-way yetty_ywire_wire_statemachine framer grown up into a
 * bidirectional, multi-channel link. One connection owns ONE transport (a
 * reactor-capable fd owner — see <yetty/yclass/transport-reactor.h>) and
 * interleaves independent yetty_ywire_channels over it: it demuxes inbound bytes
 * to the right channel and frames outbound channel writes back onto the single
 * stream.
 *
 * The reactor seam (fd / readiness / pump) belongs to the transport; the
 * connection just forwards it, so a host loop (libuv / asyncio / tokio) drives
 * the whole thing through connection.fd() + connection.pump_*().
 *
 * The connection BORROWS the transport reactor handle — it never destroys the
 * underlying transport. The creator owns the transport and tears it down after
 * the connection.
 */

#ifndef YETTY_YWIRE_CONNECTION_H
#define YETTY_YWIRE_CONNECTION_H

#include <yetty/yclass/transport-reactor.h>
#include <yetty/ycore/result.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_buffer;
struct yetty_ywire_channel;
struct yetty_ywire_connection;
struct yetty_ywire_wire_statemachine;

YETTY_YRESULT_DECLARE(yetty_ywire_connection_ptr, struct yetty_ywire_connection *);

/* Fired by pickup_winsize() with the controlling tty's geometry, so the
 * consumer can inject the viewport into its framework. Pixel fields are 0 when
 * the tty reports no pixel size. */
typedef void (*yetty_ywire_resize_cb)(void *user, int width_px, int height_px, int cols, int rows);

/* Create over a reactor-capable transport (e.g.
 * yetty_yclass_transport_pty_reactor(pty)). Opens the three well-known channels
 * (rpc / input / raw). `compressed`: 1 = base64+lz4 for the rpc lane, 0 =
 * base64 only. The reactor handle is borrowed (not destroyed at connection
 * destroy). */
struct yetty_ywire_connection_ptr_result yetty_ywire_connection_create(
    struct yetty_yclass_transport_reactor reactor, int compressed);

/* Ships one already-encoded envelope toward the peer. The host terminal wraps
 * its PTY-master write path in this (same shape as the RPC DCS server's emit
 * callback). Must write all `n` bytes (blocking as needed). */
typedef struct yetty_ycore_void_result (*yetty_ywire_connection_writer_fn)(const uint8_t *bytes,
                                                                           size_t n, void *user);

/* HOST-SIDE (acceptor) attach: run the connection layer over a BORROWED
 * statemachine — the host terminal's own demux — instead of owning the byte
 * stream. Registers ONLY the dynamic-channel message handler
 * (DCS YETTY_DCS_YWIRE_CHANNEL); every other code keeps whatever handler its
 * owner installed. Outbound envelopes (CLOSE replies, WINDOW_ADJUST grants,
 * DATA from host-side channel writes) go straight through `writer`.
 *
 * Differences from create():
 *   - acceptor role by default (dynamic ids odd; the client side is the
 *     initiator);
 *   - no well-known channels — channel() returns NULL for rpc/input/raw;
 *   - the reactor seam is inert: fd()/out_fd() return -1, want_write()/
 *     is_eof() return 0, pump_readable()/enable_raw_mode() fail (the SM's
 *     owner feeds bytes; process() fires the handler), pump_writable() only
 *     runs the outbound scheduler;
 *   - with no accept callback every OPEN is answered with CLOSE — a
 *     deterministic rejection instead of a silent drop.
 *
 * Lifetimes: `sm`, `writer`, and `writer_user` are borrowed and must outlive
 * the connection; destroy the connection only AFTER the statemachine it was
 * attached to is destroyed (the SM's registered handler points at it) — the
 * same contract as the RPC DCS server. */
struct yetty_ywire_connection_ptr_result yetty_ywire_connection_attach(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_connection_writer_fn writer,
    void *writer_user, int compressed);

/* The channel for a well-known or dynamic id, or NULL if not open. */
struct yetty_ywire_channel *yetty_ywire_connection_channel(
    struct yetty_ywire_connection *connection, uint32_t channel_id);

/*===========================================================================
 * Dynamic channels (SSH CHANNEL_OPEN analog — see channel.h for semantics)
 *=========================================================================*/

/* Fired during pump when the peer OPENs a channel. Attach sinks/event cb to
 * `channel` here. Return non-zero to accept; zero refuses (a CLOSE goes back
 * and the slot is released). With no callback set every OPEN is refused. */
typedef int (*yetty_ywire_accept_cb)(void *user, struct yetty_ywire_channel *channel);

struct yetty_ycore_void_result yetty_ywire_connection_set_accept_cb(
    struct yetty_ywire_connection *connection, yetty_ywire_accept_cb cb, void *user);

/* Dynamic-channel id parity. Exactly one peer of a connection must be the
 * acceptor (odd id offsets); the creator defaults to initiator (even). Call
 * before the first open_channel(). */
struct yetty_ycore_void_result yetty_ywire_connection_set_role(
    struct yetty_ywire_connection *connection, int acceptor);

/* Open a dynamic channel: allocate the next local id, emit OPEN, and return
 * the channel ready for write()/flush() (optimistic open — a peer rejection
 * arrives later as a CLOSED event). `initial_recv_window` overrides
 * YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT for the peer→local direction; 0 keeps the
 * default. */
struct yetty_ywire_channel_ptr_result yetty_ywire_connection_open_channel(
    struct yetty_ywire_connection *connection, uint32_t initial_recv_window);

/*===========================================================================
 * Reactor seam — forwarded down to the transport. Register fd()/out_fd() with
 * the host loop; call the pumps on readiness.
 *=========================================================================*/
int yetty_ywire_connection_fd(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_out_fd(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_want_write(struct yetty_ywire_connection *connection);
int yetty_ywire_connection_is_eof(struct yetty_ywire_connection *connection);
struct yetty_ycore_void_result yetty_ywire_connection_enable_raw_mode(
    struct yetty_ywire_connection *connection);

/* Read whatever is available on the inbound fd and demux it to channels (fires
 * sinks / fills channel inbufs). Returns bytes consumed this call. */
struct yetty_ycore_size_result yetty_ywire_connection_pump_readable(
    struct yetty_ywire_connection *connection);

/* Drain queued outbound bytes (non-blocking). Returns bytes written this call. */
struct yetty_ycore_size_result yetty_ywire_connection_pump_writable(
    struct yetty_ywire_connection *connection);

/* The number of dynamic channels still awaiting close COMPLETION: closed (or
 * close-requested) locally but the peer's answering CLOSE has not arrived.
 * This is the teardown drain's completion predicate — pump the still-alive
 * parser until it reaches 0 (bounded), then stop READING immediately: the
 * peer sends nothing further for this client after the last echo, so a user
 * key typed during exit stays unread in the kernel queue for the resumed
 * shell instead of being swallowed by a blind post-parser discard. */
int yetty_ywire_connection_pending_close_count(struct yetty_ywire_connection *connection);

/* Host-owned input forwarding barrier — the safe replacement for the deleted
 * child-supplied handback. The HOST side of a pane wire connection arms the
 * barrier when the in-pane client signals the START of its teardown (the
 * no-payload INPUT_HOLD envelope). These calls are host-owned end to end and
 * NEVER accept bytes from pane output, so they cannot let child output
 * synthesize PTY input — they only DEFER the host's own user keystrokes:
 *
 *   _arm(deadline_ms): arm the barrier with an ABSOLUTE host-side deadline. The
 *       client sends INPUT_HOLD before it closes any channel or drains, so a
 *       keystroke typed anywhere in the teardown window is held — including one
 *       the host writes before it processes the client's first CLOSE. The host
 *       emits INPUT_HOLD_ACK right after this so the client can wait for
 *       confirmation before it detaches its sinks. Past `deadline_ms` the
 *       barrier stops holding and releases, so a client that crashes/wedges
 *       after arming cannot make the host retain input forever.
 *   _hold(bytes, len): while armed AND within the deadline, take the host's user
 *       input the caller was about to forward to the pane (its keystroke sink)
 *       and stash it instead of writing it into the stream the client's close
 *       drain would consume. Returns 1 when held (caller must not also write), 0
 *       when not armed OR the deadline passed. While armed+in-deadline it ALWAYS
 *       holds (the buffer grows) — no size-cap bypass, because declining would
 *       push the overflow into the drained stream and re-lose it.
 *   _release(out): move the held bytes into `out` for the caller to write to the
 *       pane (read exactly once) once the client is fully gone (no dynamic
 *       channels remain) OR the deadline has passed. Returns the byte count (0
 *       while the client still has a channel open and the deadline holds, or
 *       nothing is held).
 *   _release_forced(out): unconditional release for PTY EOF / owner death — an
 *       ungraceful client death recovers the held bytes rather than stranding
 *       them. Exactly once. */
void yetty_ywire_connection_input_barrier_arm(struct yetty_ywire_connection *connection,
                                              int deadline_ms);
int yetty_ywire_connection_input_barrier_hold(struct yetty_ywire_connection *connection,
                                              const void *bytes, size_t len);
void yetty_ywire_connection_input_barrier_client_present(struct yetty_ywire_connection *connection,
                                                         int present);
int yetty_ywire_connection_input_barrier_release(struct yetty_ywire_connection *connection,
                                                 struct yetty_ycore_buffer *out);
int yetty_ywire_connection_input_barrier_release_forced(struct yetty_ywire_connection *connection,
                                                        struct yetty_ycore_buffer *out);

/* Client teardown handshake: after the client emits INPUT_HOLD it MUST call
 * this before detaching its sinks / destroying its framework. It reads inbound
 * byte-wise (parser + sinks still alive) and feeds the parser until the host's
 * INPUT_HOLD_ACK arrives — proof the host armed its barrier — or the ABSOLUTE
 * wall-clock deadline (`deadline_ms`, CLOCK_MONOTONIC) expires. Until the ACK
 * the barrier may still be unarmed, so a key the host forwards is an echo the
 * still-live client handles here; after the ACK every host keystroke is held
 * and can never reach the stream the close drain consumes. Returns 1 if the
 * ACK was seen, 0 on timeout / dead host / non-POSIX. */
int yetty_ywire_connection_drain_until_hold_ack(struct yetty_ywire_connection *connection,
                                                int deadline_ms);

/* Is the HOLD-ACK still a valid lease? Returns 1 only if the ACK was seen AND
 * no more than `lease_ms` has elapsed since it arrived. The host's barrier
 * expires on its own deadline and resumes forwarding; the teardown must gate its
 * close drain on this (with `lease_ms` below the host deadline) so a client that
 * spends longer than the lease tearing down never drains after the host un-armed
 * — which would re-expose an exit-window key to the sink-detached drain. */
int yetty_ywire_connection_hold_ack_lease_valid(struct yetty_ywire_connection *connection,
                                                int lease_ms);

/* Teardown close drain: feed the still-alive parser ONE BYTE at a time from
 * the inbound fd until every locally-closed dynamic channel has its peer's
 * framed CLOSE echo (pending_close_count() == 0) AND the parser is back at
 * GROUND, or the ABSOLUTE wall-clock deadline (`deadline_ms`,
 * CLOCK_MONOTONIC — recomputed after every read/EINTR, so a peer that keeps
 * the fd readable without completing cannot hold teardown open) expires.
 * Byte-wise reading is the point — it stops reading EXACTLY at the final
 * framed-completion boundary, so a raw byte queued immediately AFTER the
 * echo (a user key typed during exit, in the same kernel-readable window)
 * is never consumed and stays queued for the resumed shell. A bulk
 * pump_readable would slurp it into the parser in the same 4096-byte read.
 * Detach any input/GUI sinks whose owners teardown has already destroyed
 * BEFORE calling this. Returns the remaining pending count (0 = complete). */
int yetty_ywire_connection_drain_closes(struct yetty_ywire_connection *connection, int deadline_ms);

/*===========================================================================
 * SIGWINCH → viewport plumbing.
 *=========================================================================*/
struct yetty_ycore_void_result yetty_ywire_connection_set_resize_cb(
    struct yetty_ywire_connection *connection, yetty_ywire_resize_cb cb, void *user);

/* Read the controlling tty's current size (TIOCGWINSZ on the inbound fd) and
 * fire the resize callback. No-op if no callback is set. */
struct yetty_ycore_void_result yetty_ywire_connection_pickup_winsize(
    struct yetty_ywire_connection *connection);

/* Free the connection (state machine + channels). Does NOT destroy the borrowed
 * transport. NULL-safe. */
struct yetty_ycore_void_result yetty_ywire_connection_destroy(
    struct yetty_ywire_connection *connection);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWIRE_CONNECTION_H */
