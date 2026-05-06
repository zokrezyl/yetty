/* iframe-transport — webasm-only yetty_yconn_transport implementation
 * that ferries bytes to/from the tinyemu iframe via postMessage. The
 * iframe injects them into slirp's chr-backend, which surfaces them
 * as a synthetic inbound TCP connection inside the VM.
 *
 * Pairs with telnet-pty.c (now transport-polymorphic) so the wasm
 * telnet path is "telnet over iframe-transport" while the desktop
 * path is "telnet over tcp-transport". The protocol code sits on
 * top of either with no changes.
 *
 * Multiple terminals share one iframe + one slirp + one in-VM
 * telnetd. Each new transport instance allocates a fresh clientSid
 * (parent-side identity); the iframe maps it to a wasm-side sid
 * returned by tinyemu_session_open().
 */

#ifndef YETTY_YPLATFORM_IFRAME_TRANSPORT_H
#define YETTY_YPLATFORM_IFRAME_TRANSPORT_H

#include <yetty/ycore/conn-transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate a new iframe transport that will, on open(), tell the
 * tinyemu iframe to inject an inbound connection to (guest, port).
 * Returns NULL on out-of-memory. */
struct yetty_yconn_transport *yetty_yplatform_iframe_transport_create(uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_IFRAME_TRANSPORT_H */
