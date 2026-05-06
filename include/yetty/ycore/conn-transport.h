/* yetty_yconn_transport — polymorphic transport for byte-stream PTYs.
 *
 * Pulls the network/IPC details out of the application-level protocol
 * code (currently ytelnet/telnet-pty.c). A "transport" represents a
 * single client-side connection lifecycle: you `open()` it once, the
 * platform fires the supplied callbacks asynchronously (on_connect →
 * on_data* → on_disconnect), and you finally `send()` / `close()`
 * via the connection handle the on_connect callback hands you.
 *
 * Two backends today:
 *
 *   - tcp-transport (src/yetty/ycore/tcp-transport.c) — wraps the
 *     event loop's TCP client API (libuv on desktop). Used by the
 *     desktop / iOS / Android telnet factory.
 *
 *   - iframe-transport (src/yetty/yplatform/webasm/iframe-transport.c
 *     — coming next) — uses postMessage to talk to the tinyemu iframe
 *     wasm, which then injects the bytes into slirp's TCP machinery
 *     so multiple yetty terminals can share one in-VM telnetd. No
 *     real sockets involved.
 *
 * The same telnet-pty.c sits on top of either; the protocol code
 * (NAWS, IAC, options) is transport-agnostic.
 */

#ifndef YETTY_YCORE_CONN_TRANSPORT_H
#define YETTY_YCORE_CONN_TRANSPORT_H

#include <yetty/ycore/event-loop.h>   /* yetty_ycore_conn, client_callbacks */
#include <yetty/ycore/result.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconn_transport;

struct yetty_yconn_transport_ops {
    /* Begin opening the connection. Returns 0 on success (the connect
     * is in flight; callbacks will fire later) or -1 on synchronous
     * setup failure. The transport keeps a *copy* of `cb` for the
     * lifetime of the connection. */
    int (*open)(struct yetty_yconn_transport *self,
                const struct yetty_ycore_client_callbacks *cb);

    /* Send `len` bytes on the connection. Same return contract as
     * the event-loop's tcp_send: ok with bytes_sent on success,
     * err on failure (e.g. not yet connected). */
    struct yetty_ycore_size_result (*send)(struct yetty_yconn_transport *self,
                                           struct yetty_ycore_conn *conn,
                                           const void *data, size_t len);

    /* Close + tear down the in-flight connection. on_disconnect may
     * fire as a side-effect (transport's choice). After this returns
     * the conn handle is invalid; subsequent callbacks (if any) must
     * be ignored by the caller. */
    struct yetty_ycore_void_result (*close)(struct yetty_yconn_transport *self,
                                            struct yetty_ycore_conn *conn);

    /* Free the transport itself. Implies close() if open() succeeded
     * and the connection is still live. */
    void (*destroy)(struct yetty_yconn_transport *self);
};

struct yetty_yconn_transport {
    const struct yetty_yconn_transport_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_CONN_TRANSPORT_H */
