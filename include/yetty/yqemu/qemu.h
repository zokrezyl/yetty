/* QEMU - Start QEMU RISC-V VM */

#ifndef YETTY_QEMU_H
#define YETTY_QEMU_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yplatform/process.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-side TCP port that QEMU's user-mode (slirp) NAT forwards to the
 * guest's in-VM telnet daemon (alpine-extended ships busybox telnetd on
 * tcp/23). yetty connects here as a normal telnet client; no serial
 * chardev plumbing involved.
 *
 * Why not the chardev/virtconsole route: on the MSYS2/CLANG64 QEMU 11
 * build, libuv's tcp_client connecting to a `socket,server=on` chardev
 * silently kills QEMU ~2 s after connect, with no stderr and no kernel
 * panic. Slirp hostfwd → in-guest telnetd is unaffected (and matches
 * what tools/qemu.bat exercises). */
#define QEMU_TELNET_PORT 2423

/**
 * Spawn QEMU with the alpine-extended rootfs and a slirp hostfwd from
 * the given host port to the guest's telnetd (tcp/23). Returns when the
 * process is launched — the in-guest telnetd takes a few seconds to
 * come up; pair with yetty_yqemu_qemu_wait_ready().
 *
 * @param host_port Host TCP port to forward to guest:23.
 * @return Opaque process handle, or YPROCESS_INVALID on error.
 */
struct yetty_yplatform_yprocess *yetty_yqemu_qemu_start(uint16_t host_port);

/**
 * Stop QEMU process. Frees the handle.
 */
void yetty_yqemu_qemu_stop(struct yetty_yplatform_yprocess *proc);

/**
 * Wait for QEMU telnet to be ready.
 *
 * @param port Telnet port
 * @param timeout_ms Timeout in milliseconds
 */
struct yetty_ycore_void_result yetty_yqemu_qemu_wait_ready(uint16_t port, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_QEMU_H */
