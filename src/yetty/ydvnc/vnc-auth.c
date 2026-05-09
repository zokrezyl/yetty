#include "vnc-auth.h"

#include <stdint.h>
#include <string.h>

#include "des.h"

static uint8_t reverse_byte(uint8_t b)
{
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    return b;
}

void yetty_ydvnc_vnc_auth_response(const char *password, const uint8_t challenge[16],
                                   uint8_t response[16])
{
    uint8_t key[8] = {0};
    if (password) {
        size_t plen = strlen(password);
        if (plen > 8) {
            plen = 8;
        }
        memcpy(key, password, plen);
    }
    for (int i = 0; i < 8; i++) {
        key[i] = reverse_byte(key[i]);
    }

    yetty_ydvnc_des_encrypt(key, challenge, response);
    yetty_ydvnc_des_encrypt(key, challenge + 8, response + 8);
}
