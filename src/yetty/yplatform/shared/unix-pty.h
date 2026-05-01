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

/* Telnet PTY - TCP telnet connection (libuv-driven, async connect) */
struct yetty_yplatform_event_loop;
struct yetty_yplatform_pty_result yetty_ytelnet_telnet_pty_create(const char *host, uint16_t port,
                                                    struct yetty_yplatform_event_loop *event_loop);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_UNIX_PTY_H */
