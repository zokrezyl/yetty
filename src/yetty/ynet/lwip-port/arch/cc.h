/* lwIP architecture shim for the emscripten/wasm build.
 *
 * lwIP requires each port to supply arch/cc.h. emscripten is a
 * little-endian, freestanding-ish C target — the stdint/stddef types
 * and <errno.h> are all we need; the byte-order helpers lwIP ships
 * cover the rest. Randomness and the time base come from port.c
 * (lwip_port_rand / sys_now) over emscripten intrinsics. */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>

#define LWIP_ERRNO_INCLUDE <errno.h>

unsigned int lwip_port_rand(void);
#define LWIP_RAND() (lwip_port_rand())

#endif /* LWIP_ARCH_CC_H */
