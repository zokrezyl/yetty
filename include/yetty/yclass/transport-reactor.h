/* Transport reactor capability — a SEPARATE interface from the byte-stream
 * transport vtable (transport.h).
 *
 * The base yetty_yclass_transport is a synchronous byte stream for RPC-style
 * users (send / recv / flush). This is a different capability: an
 * event-loop-drivable fd owner. A host loop watches fd()/out_fd() and calls the
 * pumps on readiness instead of blocking. Only fd-backed transports
 * (transport-pty) expose it; plain byte transports (fd / dcs) do not, and never
 * learn about operations that only make sense for loop-driven fd owners.
 *
 * The yetty_ywire_connection multiplexer is built over this capability alone, so
 * `ywire` stays abstract over the concrete PTY implementation and never links
 * the yclass library (no yclass <-> ywire cycle).
 */

#ifndef YCLASS_TRANSPORT_REACTOR_H
#define YCLASS_TRANSPORT_REACTOR_H

#include <yetty/ycore/result.h>

#include <stddef.h>

/* `userdata` is the concrete transport pointer; every op receives it. */
struct yetty_yclass_transport_reactor_ops {
    /* The readable fd to register with the loop, or -1. */
    int (*fd)(void *transport);
    /* The writable fd (for writable-readiness), or -1. */
    int (*out_fd)(void *transport);
    /* Non-zero while queued outbound bytes still await pump_writable(). */
    int (*want_write)(void *transport);
    /* Non-zero once the inbound fd reached real end-of-file. */
    int (*is_eof)(void *transport);
    /* One non-blocking read of currently-available inbound bytes (0 = nothing
     * ready right now, or EOF — distinguish via is_eof). */
    struct yetty_ycore_size_result (*read_available)(void *transport, void *buf, size_t max);
    /* Append bytes to the ordered outbound queue (whole frames, in order),
     * without writing yet. */
    struct yetty_ycore_void_result (*queue)(void *transport, const void *bytes, size_t len);
    /* Drain the outbound queue with non-blocking writes; returns bytes written
     * this call (stops partial on EAGAIN so the loop can re-arm). */
    struct yetty_ycore_size_result (*pump_writable)(void *transport);
    /* Put the inbound fd in terminal raw mode + the outbound fd in O_NONBLOCK;
     * restored when the underlying transport is destroyed. */
    struct yetty_ycore_void_result (*enable_raw_mode)(void *transport);
};

/* A borrowed view onto a reactor-capable transport. Passed by value; the
 * underlying transport is owned by whoever created it. */
struct yetty_yclass_transport_reactor {
    void *userdata;
    const struct yetty_yclass_transport_reactor_ops *ops;
};

#endif /* YCLASS_TRANSPORT_REACTOR_H */
