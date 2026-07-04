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

struct yetty_ywire_channel;
struct yetty_ywire_connection;

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
