/* yclass transport — byte-level send/recv vtable.
 *
 * The yclass RPC layer talks to the wire through one of these. Each op
 * is byte-level (NOT message-level): yclass owns its own framing (u32
 * length prefixes embedded in the protocol).
 *
 * Contract:
 *   send(bytes, len)       — Best-effort write. May return short. The
 *                            session loops until len bytes are out.
 *                            Blocking allowed; coroutine-based impls
 *                            (e.g. wire_statemachine) yield internally.
 *   recv(buf,   max)       — Best-effort read. May return short. The
 *                            session loops until the wire-level prefix
 *                            says enough bytes are in hand.
 *                            Blocking allowed; same coro contract.
 *   destroy(transport)     — Free transport-owned state. The session
 *                            calls this on destroy; transports are
 *                            session-owned.
 *
 * Both send and recv may suspend the calling coroutine. The yclass
 * skel is invoked only AFTER the full request frame has been received
 * (i.e. the recv loop in the session has assembled header + body),
 * so partially-arrived frames never trigger a dispatch. */

#ifndef YCLASS_TRANSPORT_H
#define YCLASS_TRANSPORT_H

#include <yetty/ycore/result.h>

#include <stddef.h>

struct yetty_yclass_transport;

struct yetty_yclass_transport_ops {
    struct yetty_ycore_size_result (*send)(struct yetty_yclass_transport *t, const void *bytes,
                                           size_t len);
    struct yetty_ycore_size_result (*recv)(struct yetty_yclass_transport *t, void *buf, size_t max);
    struct yetty_ycore_void_result (*destroy)(struct yetty_yclass_transport *t);
    /* Optional. Force any buffered outbound bytes onto the wire. NULL means
     * send() writes through immediately (no buffering). Pipelined calls need
     * this: they return before any recv(), so a buffering transport (DCS,
     * which coalesces a call's sends into one envelope on the next recv)
     * would otherwise never emit the frame. */
    struct yetty_ycore_void_result (*flush)(struct yetty_yclass_transport *t);
    /* Optional. Non-blocking read of already-buffered inbound bytes; returns
     * 0 when nothing is available right now — never waits, never pumps the
     * peer. Presence of this op declares the transport ASYNC-capable: its
     * inbound bytes arrive through an external event loop's pump (the ywire
     * connection under libuv), so blocking recv() after the attach handshake
     * is forbidden, and the session pipelines void calls instead — sending
     * without waiting and draining completions from here (see
     * yetty_yclass_rpc_call_void / yetty_yclass_rpc_session_pump). NULL
     * means the transport is synchronous (fd / dcs / pty): every call keeps
     * the lockstep request/response shape. */
    struct yetty_ycore_size_result (*recv_available)(struct yetty_yclass_transport *t, void *buf,
                                                     size_t max);
};

struct yetty_yclass_transport {
    const struct yetty_yclass_transport_ops *ops;
};

/* Result wrapper for a transport handle. Lives on the base transport header so
 * consumers (the ywire channel adapter, session bootstrap) can name it without
 * pulling a concrete transport backend (fd / dcs / pty). */
YETTY_YRESULT_DECLARE(yetty_yclass_transport_ptr, struct yetty_yclass_transport *);

/* The DEFAULT transport for a tool running inside a hosting yetty — zero
 * configuration: yclass-RPC frames as DCS envelopes on the terminal byte
 * stream, responses read from STDIN, requests written to STDOUT,
 * compression on, the protocol's own DCS code. Pass the result straight
 * to yetty_yclass_rpc_session_create(), which takes ownership.
 *
 * Event-loop apps on the multiplexed connection use the RPC channel's
 * transport instead (yetty_ywire_channel_transport) — same session API,
 * pipelined calls. This default is the blocking in-band path. */
struct yetty_yclass_transport_ptr_result yetty_yclass_transport_default_create(void);

#endif /* YCLASS_TRANSPORT_H */
