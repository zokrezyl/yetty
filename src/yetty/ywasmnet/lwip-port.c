/* lwIP NO_SYS port glue for emscripten.
 *
 * lwIP needs two things from the platform that aren't part of its
 * portable core: a millisecond time base for its timers, and a
 * randomness source (TCP ISN, DHCP xid, DNS txid). Both come from
 * emscripten intrinsics here. Declared by the port's arch/cc.h
 * (lwip_port_rand) and lwip/sys.h (sys_now); referenced by the lwIP
 * static lib and resolved at final link. */

#include "lwip/sys.h"

#include <emscripten/emscripten.h>

/* Monotonic-ish millisecond clock for lwIP's timeout wheel. */
u32_t sys_now(void)
{
    return (u32_t)emscripten_get_now();
}

unsigned int lwip_port_rand(void)
{
    /* emscripten_random() is [0,1); scale to the full 32-bit range. */
    return (unsigned int)(emscripten_random() * 4294967296.0);
}
