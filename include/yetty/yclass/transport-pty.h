/* yclass pty transport — the single owner of a PTY's byte stream.
 *
 * Sibling of transport-fd / transport-dcs, but richer: where those are plain
 * blocking byte movers, this one owns the fd pair, the terminal raw-mode, and a
 * reactor seam (fd / readiness / pump) so a host event loop can drive it
 * without a blocking recv. It is the byte-transport primitive the
 * yetty_ywire_connection multiplexer rides; the connection forwards fd()/pump()
 * straight down to it.
 *
 * Despite the name, the transport is fd-AGNOSTIC and doubles as the generic
 * reactor backend for any byte-stream fd pair — pipe, socketpair, unix/tcp
 * socket. Every tty-specific step self-disables on a non-tty fd (raw-mode
 * termios are skipped, a 0-byte read means real EOF instead of "VMIN=0, nothing
 * buffered"), leaving exactly the queue/pump/want_write contract. This is what
 * makes the side-channel endpoint (create_from_env below) the same code path
 * as the in-band single-PTY mux.
 *
 * Single reader, single writer: only this object calls read()/write() on the
 * fds, which is what makes "two readers tearing frames on a PTY" structurally
 * impossible once consumers ride channels above the connection.
 *
 * The fds are BORROWED — the caller keeps ownership and closes them after
 * destroy. destroy restores any raw-mode / O_NONBLOCK changes this object made.
 */

#ifndef YCLASS_TRANSPORT_PTY_H
#define YCLASS_TRANSPORT_PTY_H

#include <yetty/yclass/transport.h>
#include <yetty/yclass/transport-reactor.h>
#include <yetty/ycore/result.h>

#include <stddef.h>

struct yetty_yclass_transport_pty;
YETTY_YRESULT_DECLARE(yetty_yclass_transport_pty_ptr, struct yetty_yclass_transport_pty *);

/* fd_in carries inbound bytes (a tool over a PTY passes STDIN_FILENO); fd_out
 * carries outbound (STDOUT_FILENO). Both borrowed. */
struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create(int fd_in,
                                                                               int fd_out);

/* Endpoint selection with the side-channel escape hatch (the SSH "multiplex
 * only when constrained to one connection" model): when the embedder/host
 * passed a dedicated fd pair via YETTY_YWIRE_SIDE_CHANNEL ("<fd_in>,<fd_out>",
 * fds inherited open across exec — the LISTEN_FDS convention), the transport
 * rides that pair and the terminal byte stream is left alone; otherwise it
 * falls back to the given fds (in-band single-PTY muxing). A set-but-malformed
 * or stale-fd variable is a configuration error and fails loudly rather than
 * silently degrading to in-band. */
#define YETTY_YWIRE_SIDE_CHANNEL_ENV "YETTY_YWIRE_SIDE_CHANNEL"
struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create_from_env(
    int fallback_fd_in, int fallback_fd_out);

/* The embedded base vtable (send = queue, recv = recv_blocking, flush =
 * flush_blocking) for code that wants a plain yetty_yclass_transport. */
struct yetty_yclass_transport *yetty_yclass_transport_pty_base(
    struct yetty_yclass_transport_pty *transport);

/* A borrowed reactor capability view (transport-reactor.h) — the loop-drivable
 * fd-owner ops the yetty_ywire_connection multiplexer rides. Pass the result by
 * value to yetty_ywire_connection_create(). */
struct yetty_yclass_transport_reactor yetty_yclass_transport_pty_reactor(
    struct yetty_yclass_transport_pty *transport);

/*===========================================================================
 * Reactor seam — register fd() with a host loop, call the pumps on readiness.
 *=========================================================================*/

/* The readable fd to watch (fd_in), or -1. */
int yetty_yclass_transport_pty_fd(struct yetty_yclass_transport_pty *transport);
/* The writable fd (fd_out), or -1. */
int yetty_yclass_transport_pty_out_fd(struct yetty_yclass_transport_pty *transport);
/* Non-zero when outbound bytes are queued and pump_writable still has work —
 * arm the loop's writable interest while this is true. */
int yetty_yclass_transport_pty_want_write(struct yetty_yclass_transport_pty *transport);
/* Non-zero once fd_in reached real end-of-file (peer hangup / closed pipe). */
int yetty_yclass_transport_pty_is_eof(struct yetty_yclass_transport_pty *transport);

/* Put fd_in into terminal raw mode (no echo, VMIN=0/VTIME=0) and fd_out into
 * O_NONBLOCK. Idempotent. The echo-loop fix (cooked TTY echoing our OSC writes
 * back) and the non-blocking writer both depend on this; restored at destroy.
 * On a non-tty fd_in the raw-mode step is skipped (only O_NONBLOCK applies). */
struct yetty_ycore_void_result yetty_yclass_transport_pty_enable_raw_mode(
    struct yetty_yclass_transport_pty *transport);

/* One non-blocking read of whatever is currently available on fd_in into buf.
 * Returns 0 when nothing is ready right now (raw-tty VMIN=0) OR at EOF — the
 * caller distinguishes via is_eof(). Intended to be called once per readable
 * event from the host loop. */
struct yetty_ycore_size_result yetty_yclass_transport_pty_read_available(
    struct yetty_yclass_transport_pty *transport, void *buf, size_t max);

/* Blocking read for the synchronous attach handshake (before the host loop
 * owns the fd): poll(fd_in) up to an internal timeout, then read. Returns 0 on
 * EOF / timeout. */
struct yetty_ycore_size_result yetty_yclass_transport_pty_recv_blocking(
    struct yetty_yclass_transport_pty *transport, void *buf, size_t max);

/* Append bytes to the ordered outbound queue (whole frames, in order). Nothing
 * is written until a pump_writable / flush_blocking. */
struct yetty_ycore_void_result yetty_yclass_transport_pty_queue(
    struct yetty_yclass_transport_pty *transport, const void *bytes, size_t len);

/* Drain the outbound queue with non-blocking writes. Returns bytes written this
 * call; stops (partial) on EAGAIN so the loop can re-arm writable interest. */
struct yetty_ycore_size_result yetty_yclass_transport_pty_pump_writable(
    struct yetty_yclass_transport_pty *transport);

/* Drain the outbound queue to completion, blocking on writability as needed.
 * Used for the pre-loop handshake / simple synchronous callers. */
struct yetty_ycore_void_result yetty_yclass_transport_pty_flush_blocking(
    struct yetty_yclass_transport_pty *transport);

/* Restore raw-mode / O_NONBLOCK, free state. Does NOT close the borrowed fds.
 * NULL-safe. */
struct yetty_ycore_void_result yetty_yclass_transport_pty_destroy(
    struct yetty_yclass_transport_pty *transport);

#endif /* YCLASS_TRANSPORT_PTY_H */
