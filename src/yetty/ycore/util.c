/* util.c - Common utility functions */

#include <yetty/ycore/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base64 decode table */
static const signed char b64_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

size_t yetty_ycore_base64_decode(const char *in, size_t in_len, char *out, size_t out_cap)
{
    size_t out_len = 0;
    int val = 0, valb = -8;

    for (size_t i = 0; i < in_len && out_len < out_cap; i++) {
        int c = b64_table[(unsigned char)in[i]];
        if (c == -1) {
            break;
        }
        val = (val << 6) + c;
        valb += 6;
        if (valb >= 0) {
            out[out_len++] = (char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return out_len;
}

/* Standard base64 alphabet */
static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct yetty_ycore_buffer_result yetty_ycore_base64_encode(const void *in, size_t in_len)
{
    const uint8_t *src;
    size_t out_len;
    size_t capacity;
    char *out;
    size_t i;
    size_t o;
    size_t rem;
    struct yetty_ycore_buffer buffer = {0};

    if (in_len > 0 && !in) {
        return YETTY_ERR(yetty_ycore_buffer, "input is NULL");
    }

    /* 3 bytes -> 4 base64 chars, rounded up; +1 for null terminator */
    out_len = ((in_len + 2) / 3) * 4;
    capacity = out_len + 1;

    out = malloc(capacity);
    if (!out) {
        return YETTY_ERR(yetty_ycore_buffer, "malloc failed");
    }

    src = (const uint8_t *)in;
    o = 0;
    for (i = 0; i + 3 <= in_len; i += 3) {
        uint32_t triple =
            ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | (uint32_t)src[i + 2];
        out[o++] = b64_alphabet[(triple >> 18) & 0x3F];
        out[o++] = b64_alphabet[(triple >> 12) & 0x3F];
        out[o++] = b64_alphabet[(triple >> 6) & 0x3F];
        out[o++] = b64_alphabet[triple & 0x3F];
    }

    rem = in_len - i;
    if (rem == 1) {
        uint32_t triple = (uint32_t)src[i] << 16;
        out[o++] = b64_alphabet[(triple >> 18) & 0x3F];
        out[o++] = b64_alphabet[(triple >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t triple = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
        out[o++] = b64_alphabet[(triple >> 18) & 0x3F];
        out[o++] = b64_alphabet[(triple >> 12) & 0x3F];
        out[o++] = b64_alphabet[(triple >> 6) & 0x3F];
        out[o++] = '=';
    }

    out[o] = '\0';

    buffer.data = (uint8_t *)out;
    buffer.size = o;
    buffer.capacity = capacity;
    return YETTY_OK(yetty_ycore_buffer, buffer);
}

struct yetty_ycore_buffer_result yetty_ycore_read_file(const char *path)
{
    FILE *file;
    long len;
    uint8_t *data;
    size_t read_len;
    struct yetty_ycore_buffer buffer = {0};

    if (!path) {
        return YETTY_ERR(yetty_ycore_buffer, "path is NULL");
    }

    file = fopen(path, "rb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_buffer, "failed to open file");
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_buffer, "fseek failed");
    }

    len = ftell(file);
    if (len < 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_buffer, "ftell failed");
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_buffer, "fseek failed");
    }

    data = malloc((size_t)len + 1);
    if (!data) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_buffer, "malloc failed");
    }

    read_len = fread(data, 1, (size_t)len, file);
    fclose(file);

    if (read_len != (size_t)len) {
        free(data);
        return YETTY_ERR(yetty_ycore_buffer, "fread incomplete");
    }

    data[len] = '\0';

    buffer.data = data;
    buffer.size = (size_t)len;
    buffer.capacity = (size_t)len + 1;

    return YETTY_OK(yetty_ycore_buffer, buffer);
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

int yetty_ycore_parse_hex_color(const char *s, uint32_t *out)
{
    if (!s || !out) {
        return 0;
    }
    if (*s == '#') {
        s++;
    }

    size_t len = strlen(s);
    int per_channel; /* 1 = short form, 2 = long form */
    int has_alpha;
    switch (len) {
    case 3:
        per_channel = 1;
        has_alpha = 0;
        break;
    case 4:
        per_channel = 1;
        has_alpha = 1;
        break;
    case 6:
        per_channel = 2;
        has_alpha = 0;
        break;
    case 8:
        per_channel = 2;
        has_alpha = 1;
        break;
    default:
        return 0;
    }

    int n_channels = has_alpha ? 4 : 3;
    int byte[4] = {0, 0, 0, 0xFF};
    for (int c = 0; c < n_channels; c++) {
        int b = 0;
        for (int d = 0; d < per_channel; d++) {
            int n = hex_nibble(s[c * per_channel + d]);
            if (n < 0) {
                return 0;
            }
            b = (b << 4) | n;
        }
        if (per_channel == 1) {
            b = (b << 4) | b; /* "f" → 0xff */
        }
        byte[c] = b;
    }

    /* Pack as the canonical yetty layout: byte 0 = R, byte 1 = G,
     * byte 2 = B, byte 3 = A. As a u32 on little-endian:
     *   (A << 24) | (B << 16) | (G << 8) | R
     * Matches WGSL `ypaint_unpack_color` and the existing yplot/yecho
     * conventions. */
    *out = ((uint32_t)byte[3] << 24) | ((uint32_t)byte[2] << 16) | ((uint32_t)byte[1] << 8) |
           (uint32_t)byte[0];
    return 1;
}
