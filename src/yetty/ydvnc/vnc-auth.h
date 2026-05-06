#ifndef YETTY_YDVNC_VNC_AUTH_H
#define YETTY_YDVNC_VNC_AUTH_H

/*
 * Legacy VNC authentication (security type 2): server sends 16 random bytes,
 * client returns those 16 bytes encrypted with DES-ECB. The DES key is the
 * password, truncated/zero-padded to 8 bytes, with each byte's bits reversed
 * (a quirk inherited from the original AT&T VNC implementation).
 */

#include <stdint.h>

void yetty_ydvnc_vnc_auth_response(const char *password,
                                   const uint8_t challenge[16],
                                   uint8_t response[16]);

#endif /* YETTY_YDVNC_VNC_AUTH_H */
