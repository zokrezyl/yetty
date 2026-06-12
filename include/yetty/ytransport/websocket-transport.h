/* websocket-transport — webasm-only yetty_ytransport_conn_transport
 * implementation over a browser WebSocket (emscripten/websocket.h).
 *
 * One transport instance = one WebSocket connection to a ws:// or
 * wss:// endpoint. The browser delivers each WebSocket message as a
 * whole frame, so — unlike a TCP byte stream — message boundaries are
 * preserved end to end: every send() becomes exactly one binary
 * WebSocket message and every inbound message produces exactly one
 * on_data callback. Protocol layers that rely on message framing
 * (ypty/websocket-pty.c) may sit only on transports with this
 * property; byte-stream protocols (ytelnet/telnet-pty.c) work on any
 * transport.
 *
 * Pairs with:
 *   - telnet-pty (telnet over websocket — the server side is a plain
 *     websocket↔TCP bridge such as tools/telnet-websocket.sh), or
 *   - websocket-pty (raw shell bytes + resize control messages — the
 *     server side is tools/websocket-pty-server/websocket-pty-server.py).
 */

#ifndef YETTY_YTRANSPORT_WEBSOCKET_TRANSPORT_H
#define YETTY_YTRANSPORT_WEBSOCKET_TRANSPORT_H

#include <yetty/ytransport/conn-transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate a websocket transport bound to `url` (a full ws:// or
 * wss:// URL — copied). The actual connect starts on open().
 * Returns NULL on out-of-memory or empty url. */
struct yetty_ytransport_conn_transport *yetty_ytransport_websocket_transport_create(
    const char *url);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTRANSPORT_WEBSOCKET_TRANSPORT_H */
