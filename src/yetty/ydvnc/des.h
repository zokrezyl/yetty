#ifndef YETTY_YDVNC_DES_H
#define YETTY_YDVNC_DES_H

/*
 * Single-block DES (ECB) — 8-byte key, 8-byte input, 8-byte output.
 * Used solely for the legacy VNC challenge/response. Not for general use.
 */

#include <stdint.h>

void yetty_ydvnc_des_encrypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]);

#endif /* YETTY_YDVNC_DES_H */
