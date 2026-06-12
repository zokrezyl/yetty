/* websocket-pty — raw shell bytes over a message-framed byte transport.
 *
 * The "websocket" session mode: the server end of the connection IS a
 * PTY (tools/websocket-pty-server/websocket-pty-server.py spawns a
 * shell per connection), so unlike telnet there is no in-band option
 * negotiation. Resize travels as a tiny control message.
 *
 * Wire protocol (every transport send()/on_data is one whole
 * WebSocket message — the transport must preserve message
 * boundaries, which the websocket transport does):
 *
 *   client → server, first payload byte is the message type:
 *     0x00  input    — remaining bytes are keyboard/paste data
 *     0x01  resize   — 8 payload bytes, all uint16 big-endian:
 *                      cols, rows, pixel_width, pixel_height
 *
 *   server → client: the entire message is raw PTY output (no type
 *   byte — the server only ever sends data).
 */

#ifndef YETTY_YPTY_WEBSOCKET_PTY_H
#define YETTY_YPTY_WEBSOCKET_PTY_H

#include <yetty/yplatform/pty.h>
#include <yetty/ytransport/conn-transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message type bytes (client → server). */
enum yetty_ypty_websocket_pty_message_type {
    YETTY_YPTY_WEBSOCKET_PTY_MSG_INPUT = 0x00,
    YETTY_YPTY_WEBSOCKET_PTY_MSG_RESIZE = 0x01,
};

/**
 * Create a raw PTY on top of a message-framed byte transport.
 *
 * Takes ownership of `transport` (destroyed on every failure path and
 * on PTY destroy). Connect runs asynchronously — this returns
 * immediately after kicking off transport->open(); bytes flow once
 * the transport fires on_connect.
 *
 * @param transport Message-boundary-preserving transport (websocket). Owned.
 * @return PTY result
 */
struct yetty_yplatform_pty_ptr_result yetty_ypty_websocket_pty_create(
    struct yetty_ytransport_conn_transport *transport);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPTY_WEBSOCKET_PTY_H */
