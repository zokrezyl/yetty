/* Telnet PTY - TCP/Telnet as PTY backend */

#ifndef YETTY_TELNET_PTY_H
#define YETTY_TELNET_PTY_H

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_event_loop;

/**
 * Create a telnet PTY connected to the specified host:port.
 * Connect runs asynchronously on the supplied event loop.
 *
 * @param host Hostname or IP address
 * @param port TCP port number
 * @param event_loop Loop on which the libuv TCP client and timer run
 * @return PTY result
 */
struct yetty_yplatform_pty_result telnet_pty_create(const char *host, uint16_t port,
                                                    struct yetty_ycore_event_loop *event_loop);

/**
 * Create a telnet PTY factory.
 *
 * The factory is created without an event loop; create_pty receives the
 * event loop from its caller and forwards it to telnet_pty_create.
 *
 * @param host Hostname or IP address
 * @param port TCP port number
 * @return Factory result
 */
struct yetty_yplatform_pty_factory_result telnet_pty_factory_create(const char *host,
                                                                    uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_TELNET_PTY_H */
