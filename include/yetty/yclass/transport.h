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
     * send() writes through immediately (no buffering). One-way calls need
     * this: they never recv(), so a buffering transport (DCS, which coalesces
     * a call's sends into one envelope on the next recv) would otherwise never
     * emit the frame. */
    struct yetty_ycore_void_result (*flush)(struct yetty_yclass_transport *t);
};

struct yetty_yclass_transport {
    const struct yetty_yclass_transport_ops *ops;
};

#endif /* YCLASS_TRANSPORT_H */
