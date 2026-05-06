/* Unix PTY - PTY implementations for Unix platforms */

#ifndef YETTY_UNIX_PTY_H
#define YETTY_UNIX_PTY_H

#include <yetty/platform/pty-factory.h>
#include <yetty/yconfig/config.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fork PTY - native forkpty based */
struct yetty_yplatform_pty_result yetty_yplatform_fork_pty_create(struct yetty_yconfig_config *config);

/* TinyEMU PTY - RISC-V VM */
struct yetty_yplatform_pty_result yetty_yplatform_tinyemu_pty_create(struct yetty_yconfig_config *config);

/* Telnet PTY — TCP telnet (libuv-driven, async connect). After the
 * transport-polymorphic refactor (ytelnet/telnet-pty.h), the bare
 * yetty_ytelnet_telnet_pty_create takes a transport. The TCP
 * convenience helper kept here matches the pre-refactor signature so
 * the existing forwarders below don't change. */
#include <yetty/ycore/event-loop.h>
struct yetty_yplatform_pty_result yetty_ytelnet_telnet_pty_create_tcp(
    const char *host, uint16_t port,
    struct yetty_yplatform_event_loop *event_loop);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_UNIX_PTY_H */
