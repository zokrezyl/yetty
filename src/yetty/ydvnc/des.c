/*
 * des.c — single-block DES (ECB), used only for the legacy VNC challenge
 * response. Bytes-of-bits form for clarity over speed; one VNC handshake
 * encrypts 16 bytes total, so performance does not matter.
 *
 * Tables follow FIPS 46-3 with 1-indexed bit positions (table[i]=k means
 * output bit i+1 is taken from input bit k).
 */

#include "des.h"

#include <string.h>
#include <stdint.h>

/* Initial / Final permutations */
static const uint8_t IP[64] = {
    58, 50, 42, 34, 26, 18, 10,  2,
    60, 52, 44, 36, 28, 20, 12,  4,
    62, 54, 46, 38, 30, 22, 14,  6,
    64, 56, 48, 40, 32, 24, 16,  8,
    57, 49, 41, 33, 25, 17,  9,  1,
    59, 51, 43, 35, 27, 19, 11,  3,
    61, 53, 45, 37, 29, 21, 13,  5,
    63, 55, 47, 39, 31, 23, 15,  7,
};

static const uint8_t FP[64] = {
    40,  8, 48, 16, 56, 24, 64, 32,
    39,  7, 47, 15, 55, 23, 63, 31,
    38,  6, 46, 14, 54, 22, 62, 30,
    37,  5, 45, 13, 53, 21, 61, 29,
    36,  4, 44, 12, 52, 20, 60, 28,
    35,  3, 43, 11, 51, 19, 59, 27,
    34,  2, 42, 10, 50, 18, 58, 26,
    33,  1, 41,  9, 49, 17, 57, 25,
};

/* Expansion (32 -> 48) and round-permutation (32 -> 32) */
static const uint8_t E[48] = {
    32,  1,  2,  3,  4,  5,
     4,  5,  6,  7,  8,  9,
     8,  9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32,  1,
};

static const uint8_t P[32] = {
    16,  7, 20, 21, 29, 12, 28, 17,
     1, 15, 23, 26,  5, 18, 31, 10,
     2,  8, 24, 14, 32, 27,  3,  9,
    19, 13, 30,  6, 22, 11,  4, 25,
};

/* Key permutations */
static const uint8_t PC1[56] = {
    57, 49, 41, 33, 25, 17,  9,
     1, 58, 50, 42, 34, 26, 18,
    10,  2, 59, 51, 43, 35, 27,
    19, 11,  3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,
     7, 62, 54, 46, 38, 30, 22,
    14,  6, 61, 53, 45, 37, 29,
    21, 13,  5, 28, 20, 12,  4,
};

static const uint8_t PC2[48] = {
    14, 17, 11, 24,  1,  5,
     3, 28, 15,  6, 21, 10,
    23, 19, 12,  4, 26,  8,
    16,  7, 27, 20, 13,  2,
    41, 52, 31, 37, 47, 55,
    30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53,
    46, 42, 50, 36, 29, 32,
};

static const uint8_t SHIFTS[16] = {
    1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1,
};

/* 8 S-boxes, 4 rows x 16 cols each */
static const uint8_t S[8][64] = {
    /* S1 */ {
        14, 4,13, 1, 2,15,11, 8, 3,10, 6,12, 5, 9, 0, 7,
         0,15, 7, 4,14, 2,13, 1,10, 6,12,11, 9, 5, 3, 8,
         4, 1,14, 8,13, 6, 2,11,15,12, 9, 7, 3,10, 5, 0,
        15,12, 8, 2, 4, 9, 1, 7, 5,11, 3,14,10, 0, 6,13,
    },
    /* S2 */ {
        15, 1, 8,14, 6,11, 3, 4, 9, 7, 2,13,12, 0, 5,10,
         3,13, 4, 7,15, 2, 8,14,12, 0, 1,10, 6, 9,11, 5,
         0,14, 7,11,10, 4,13, 1, 5, 8,12, 6, 9, 3, 2,15,
        13, 8,10, 1, 3,15, 4, 2,11, 6, 7,12, 0, 5,14, 9,
    },
    /* S3 */ {
        10, 0, 9,14, 6, 3,15, 5, 1,13,12, 7,11, 4, 2, 8,
        13, 7, 0, 9, 3, 4, 6,10, 2, 8, 5,14,12,11,15, 1,
        13, 6, 4, 9, 8,15, 3, 0,11, 1, 2,12, 5,10,14, 7,
         1,10,13, 0, 6, 9, 8, 7, 4,15,14, 3,11, 5, 2,12,
    },
    /* S4 */ {
         7,13,14, 3, 0, 6, 9,10, 1, 2, 8, 5,11,12, 4,15,
        13, 8,11, 5, 6,15, 0, 3, 4, 7, 2,12, 1,10,14, 9,
        10, 6, 9, 0,12,11, 7,13,15, 1, 3,14, 5, 2, 8, 4,
         3,15, 0, 6,10, 1,13, 8, 9, 4, 5,11,12, 7, 2,14,
    },
    /* S5 */ {
         2,12, 4, 1, 7,10,11, 6, 8, 5, 3,15,13, 0,14, 9,
        14,11, 2,12, 4, 7,13, 1, 5, 0,15,10, 3, 9, 8, 6,
         4, 2, 1,11,10,13, 7, 8,15, 9,12, 5, 6, 3, 0,14,
        11, 8,12, 7, 1,14, 2,13, 6,15, 0, 9,10, 4, 5, 3,
    },
    /* S6 */ {
        12, 1,10,15, 9, 2, 6, 8, 0,13, 3, 4,14, 7, 5,11,
        10,15, 4, 2, 7,12, 9, 5, 6, 1,13,14, 0,11, 3, 8,
         9,14,15, 5, 2, 8,12, 3, 7, 0, 4,10, 1,13,11, 6,
         4, 3, 2,12, 9, 5,15,10,11,14, 1, 7, 6, 0, 8,13,
    },
    /* S7 */ {
         4,11, 2,14,15, 0, 8,13, 3,12, 9, 7, 5,10, 6, 1,
        13, 0,11, 7, 4, 9, 1,10,14, 3, 5,12, 2,15, 8, 6,
         1, 4,11,13,12, 3, 7,14,10,15, 6, 8, 0, 5, 9, 2,
         6,11,13, 8, 1, 4,10, 7, 9, 5, 0,15,14, 2, 3,12,
    },
    /* S8 */ {
        13, 2, 8, 4, 6,15,11, 1,10, 9, 3,14, 5, 0,12, 7,
         1,15,13, 8,10, 3, 7, 4,12, 5, 6,11, 0,14, 9, 2,
         7,11, 4, 1, 9,12,14, 2, 0, 6,10,13,15, 3, 5, 8,
         2, 1,14, 7, 4,10, 8,13,15,12, 9, 0, 3, 5, 6,11,
    },
};

/*===========================================================================
 * Helpers
 *===========================================================================*/

static void bytes_to_bits(const uint8_t *in, uint8_t *bits, int nbytes)
{
    for (int i = 0; i < nbytes; i++) {
        for (int b = 0; b < 8; b++) {
            bits[i * 8 + b] = (uint8_t)((in[i] >> (7 - b)) & 1u);
        }
    }
}

static void bits_to_bytes(const uint8_t *bits, uint8_t *out, int nbits)
{
    int nbytes = nbits / 8;
    for (int i = 0; i < nbytes; i++) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) {
            v = (uint8_t)((v << 1) | bits[i * 8 + b]);
        }
        out[i] = v;
    }
}

static void permute(const uint8_t *in, uint8_t *out, const uint8_t *table, int n_out)
{
    for (int i = 0; i < n_out; i++) {
        out[i] = in[table[i] - 1];
    }
}

static void rotate_left_28(uint8_t *half)
{
    uint8_t t = half[0];
    for (int i = 0; i < 27; i++) {
        half[i] = half[i + 1];
    }
    half[27] = t;
}

static void compute_subkeys(const uint8_t key_bits[64], uint8_t subkeys[16][48])
{
    uint8_t cd[56];
    permute(key_bits, cd, PC1, 56);

    uint8_t c[28], d[28];
    memcpy(c, cd, 28);
    memcpy(d, cd + 28, 28);

    for (int round = 0; round < 16; round++) {
        for (int s = 0; s < SHIFTS[round]; s++) {
            rotate_left_28(c);
            rotate_left_28(d);
        }
        uint8_t cd2[56];
        memcpy(cd2, c, 28);
        memcpy(cd2 + 28, d, 28);
        permute(cd2, subkeys[round], PC2, 48);
    }
}

static void f_function(const uint8_t r[32], const uint8_t k[48], uint8_t out[32])
{
    uint8_t expanded[48];
    permute(r, expanded, E, 48);

    for (int i = 0; i < 48; i++) {
        expanded[i] ^= k[i];
    }

    uint8_t s_out[32];
    for (int box = 0; box < 8; box++) {
        const uint8_t *in6 = expanded + box * 6;
        int row = (in6[0] << 1) | in6[5];
        int col = (in6[1] << 3) | (in6[2] << 2) | (in6[3] << 1) | in6[4];
        uint8_t v = S[box][row * 16 + col];
        s_out[box * 4 + 0] = (uint8_t)((v >> 3) & 1u);
        s_out[box * 4 + 1] = (uint8_t)((v >> 2) & 1u);
        s_out[box * 4 + 2] = (uint8_t)((v >> 1) & 1u);
        s_out[box * 4 + 3] = (uint8_t)((v >> 0) & 1u);
    }

    permute(s_out, out, P, 32);
}

void yetty_ydvnc_des_encrypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8])
{
    uint8_t key_bits[64];
    uint8_t in_bits[64];
    bytes_to_bits(key, key_bits, 8);
    bytes_to_bits(in, in_bits, 8);

    uint8_t subkeys[16][48];
    compute_subkeys(key_bits, subkeys);

    uint8_t lr[64];
    permute(in_bits, lr, IP, 64);

    uint8_t l[32], r[32];
    memcpy(l, lr, 32);
    memcpy(r, lr + 32, 32);

    for (int round = 0; round < 16; round++) {
        uint8_t f_out[32];
        f_function(r, subkeys[round], f_out);
        uint8_t new_r[32];
        for (int i = 0; i < 32; i++) {
            new_r[i] = (uint8_t)(l[i] ^ f_out[i]);
        }
        memcpy(l, r, 32);
        memcpy(r, new_r, 32);
    }

    /* The post-round halves are concatenated (R, L) before the final
     * permutation — see FIPS 46-3 §3. */
    uint8_t rl[64];
    memcpy(rl, r, 32);
    memcpy(rl + 32, l, 32);

    uint8_t out_bits[64];
    permute(rl, out_bits, FP, 64);
    bits_to_bytes(out_bits, out, 64);
}
