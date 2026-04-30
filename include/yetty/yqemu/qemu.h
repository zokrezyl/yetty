/* QEMU - Start QEMU RISC-V VM */

#ifndef YETTY_QEMU_H
#define YETTY_QEMU_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yplatform/process.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QEMU_TELNET_PORT 23000

/**
 * Start QEMU process with telnet serial on specified port.
 *
 * @param port Telnet port for serial console
 * @return Opaque process handle, or YPROCESS_INVALID on error.
 */
yprocess_t *qemu_start(uint16_t port);

/**
 * Stop QEMU process. Frees the handle.
 */
void qemu_stop(yprocess_t *proc);

/**
 * Wait for QEMU telnet to be ready.
 *
 * @param port Telnet port
 * @param timeout_ms Timeout in milliseconds
 */
struct yetty_ycore_void_result qemu_wait_ready(uint16_t port, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_QEMU_H */
