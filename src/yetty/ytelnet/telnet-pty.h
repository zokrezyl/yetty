/* Telnet PTY — telnet protocol over a polymorphic byte transport. */

#ifndef YETTY_TELNET_PTY_H
#define YETTY_TELNET_PTY_H

#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ytransport/conn-transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yevent_event_loop;

/**
 * Create a telnet PTY on top of an arbitrary byte transport.
 *
 * The PTY takes ownership of `transport` (it will call
 * transport->ops->destroy() when the PTY is destroyed). The transport
 * holds the endpoint binding (host+port for TCP, sessionId for the
 * webasm iframe transport, etc.); telnet-pty itself only sees the
 * transport's open/send/close ops + its callbacks.
 *
 * Connect runs asynchronously: telnet_pty_create returns immediately
 * after kicking off transport->open(); the connection completes when
 * the underlying transport fires on_connect.
 *
 * @param transport Byte transport (TCP shim or iframe-postMessage). Owned.
 * @return PTY result
 */
struct yetty_yplatform_pty_ptr_result yetty_ytelnet_telnet_pty_create(
    struct yetty_ytransport_conn_transport *transport);

/**
 * Convenience helper for the TCP case: builds a tcp_transport from
 * (host, port, event_loop) and feeds it to telnet_pty_create. Same
 * effect as the pre-refactor yetty_ytelnet_telnet_pty_create
 * signature; minimizes the diff at the existing call sites
 * (unix-pty-factory.c, ios/tinyemu-pty.c, windows/conpty.c, …).
 */
struct yetty_yplatform_pty_ptr_result yetty_ytelnet_telnet_pty_create_tcp(
    const char *host, uint16_t port, struct yetty_yevent_event_loop *event_loop);

/**
 * Convenience factory for the desktop telnet path: builds a TCP
 * transport from (host, port, event_loop_at_create_pty_time) and
 * passes it to telnet_pty_create. host is copied.
 *
 * The event_loop is supplied later by the caller of
 * factory->ops->create_pty(factory, event_loop) — same shape as
 * before the transport refactor, so existing callers don't change.
 */
struct yetty_yplatform_pty_factory_ptr_result yetty_ytelnet_telnet_pty_factory_create(const char *host,
                                                                                  uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_TELNET_PTY_H */
