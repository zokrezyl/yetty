/* lwip-transport — yetty_ytransport_conn_transport over an lwIP TCP
 * connection (the in-browser netstack, src/yetty/ynet/).
 *
 * This is what makes the netstack useful: a single outbound TCP
 * connection to a real host:port, carried by lwIP over the relay
 * WebSocket. Many of these multiplex over the one netif. Because it
 * implements the same conn_transport contract as tcp-transport,
 * websocket-transport and iframe-transport, the existing telnet-pty
 * and ssh-websocket-pty sit on top unchanged — so "telnet/ssh to any
 * host through the browser's own TCP stack, no per-host bridge".
 *
 *     telnet-pty / ssh-websocket-pty
 *        └ conn_transport (this)
 *             └ lwIP tcp_pcb  (DNS-resolves host, tcp_connect)
 *                  └ IP / TCP over the ynet netstack
 *                       └ ethernet frames ⇄ relay WebSocket
 *
 * The netstack (lwip_init + netif + DHCP) must be up first; this
 * transport uses lwIP's global API (tcp_*, dns_*, netif_default) and
 * waits for DHCP to bind before dialing.
 */

#ifndef YETTY_YTRANSPORT_LWIP_TRANSPORT_H
#define YETTY_YTRANSPORT_LWIP_TRANSPORT_H

#include <yetty/ytransport/conn-transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate a transport that, on open(), DNS-resolves `host` (a name or
 * dotted-quad) and opens a TCP connection to (host, port) over the
 * in-browser lwIP netstack. `host` is copied. Returns NULL on OOM. */
struct yetty_ytransport_conn_transport *yetty_ytransport_lwip_transport_create(const char *host,
                                                                               uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTRANSPORT_LWIP_TRANSPORT_H */
