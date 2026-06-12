/* ynet netstack — a userspace TCP/IP stack (lwIP) in the browser.
 *
 * Gives the wasm build real TCP connectivity with no VM: lwIP runs in
 * NO_SYS mode driven entirely by browser callbacks. The stack's
 * "wire" is a single WebSocket to an L2 relay (jor1k/jslinux-style):
 * each binary message is one raw ethernet frame. lwIP does ARP, DHCP
 * (auto-IP), DNS, and TCP on top; the relay bridges the frames onto a
 * real network with NAT egress.
 *
 *     yetty TCP user (e.g. ssh)
 *        └ lwip-transport (conn_transport)   [phase 3]
 *             └ lwIP tcp_pcb
 *                  └ IP / TCP / ARP  (this module)
 *                       └ ethernet frame ⇄ relay WebSocket
 *                            └ relay (wsnic / libslirp / public)
 *
 * One netstack per wasm instance (lwIP is a global singleton). The
 * owner creates it once at startup and holds the pointer; the lwIP
 * netif callbacks and the WebSocket/timer callbacks reach it via their
 * own state/userData, so no file-scope storage is needed.
 */

#ifndef YETTY_YNET_NETSTACK_H
#define YETTY_YNET_NETSTACK_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ynet_netstack;

YETTY_YRESULT_DECLARE(yetty_ynet_netstack_ptr, struct yetty_ynet_netstack *);

/**
 * Create the netstack and start bringing it up: initialises lwIP, adds
 * an ethernet netif, opens the relay WebSocket, and (once the socket
 * connects) runs DHCP. Returns immediately — the lease arrives
 * asynchronously and is logged when it lands.
 *
 * @param relay_url Full ws:// or wss:// URL of the L2 frame relay.
 * @return netstack pointer result (owned by the caller; program-lifetime).
 */
struct yetty_ynet_netstack_ptr_result yetty_ynet_netstack_create(const char *relay_url);

/**
 * Whether DHCP has bound an address (the stack is ready for connfor
 * outbound TCP). Safe to call any time; returns 0 before the lease.
 */
int yetty_ynet_netstack_is_ready(const struct yetty_ynet_netstack *netstack);

/** Tear down the relay socket, timer and netif. Handles NULL. */
void yetty_ynet_netstack_destroy(struct yetty_ynet_netstack *netstack);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YNET_NETSTACK_H */
