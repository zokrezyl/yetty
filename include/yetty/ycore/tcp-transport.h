/* tcp-transport — yetty_yconn_transport implementation backed by the
 * platform event loop's TCP client API (libuv on desktop / iOS / etc).
 *
 * One transport instance binds to one (host, port, event_loop) tuple
 * and represents one client-side TCP connection lifecycle. Used by
 * the desktop telnet PTY factory.
 */

#ifndef YETTY_YCORE_TCP_TRANSPORT_H
#define YETTY_YCORE_TCP_TRANSPORT_H

#include <yetty/ycore/conn-transport.h>
#include <yetty/ycore/event-loop.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate a new TCP transport. Returns NULL on out-of-memory or on
 * invalid arguments. The transport stores its own copy of `host`. */
struct yetty_yconn_transport *yetty_ycore_tcp_transport_create(
    const char *host, uint16_t port,
    struct yetty_yplatform_event_loop *event_loop);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_TCP_TRANSPORT_H */
