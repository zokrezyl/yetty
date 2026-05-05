/* util.h - Common utility functions */

#ifndef YETTY_UTIL_H
#define YETTY_UTIL_H

#include <stddef.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read entire file into buffer.
 * Caller owns the returned buffer and must free buffer.data.
 *
 * @param path File path
 * @return Result containing buffer or error
 */
struct yetty_ycore_buffer_result yetty_ycore_read_file(const char *path);

/**
 * Decode base64 data.
 *
 * @param in Input base64 data
 * @param in_len Input length
 * @param out Output buffer
 * @param out_cap Output buffer capacity
 * @return Number of bytes written to out
 */
size_t yetty_ycore_base64_decode(const char *in, size_t in_len, char *out, size_t out_cap);

/**
 * Encode raw bytes as base64. Allocates the output buffer; caller owns it
 * and must free buffer.data. The encoded string is null-terminated (size
 * excludes the terminator, capacity includes it).
 *
 * @param in  Input bytes
 * @param in_len Input length
 * @return Result containing the base64 buffer or an error
 */
struct yetty_ycore_buffer_result yetty_ycore_base64_encode(const void *in, size_t in_len);

/**
 * Parse a hex colour string into a packed RGBA uint32. Accepted formats:
 *   "#RGB"       (3 hex digits, alpha defaults to 0xFF)
 *   "#RGBA"      (4 hex digits)
 *   "#RRGGBB"    (6 hex digits, alpha defaults to 0xFF)
 *   "#RRGGBBAA"  (8 hex digits)
 * The leading '#' is optional. Output layout is the canonical yetty packed
 * RGBA (matches the WGSL `ypaint_unpack_color` and existing yplot/yecho
 * conventions): byte 0 = R, byte 1 = G, byte 2 = B, byte 3 = A. As a u32
 * that's `(A << 24) | (B << 16) | (G << 8) | R`.
 *
 * @param s   Input string (may start with '#'); NULL → fail.
 * @param out Receives the packed colour on success; untouched on failure.
 * @return 1 on success, 0 on parse failure.
 */
int yetty_ycore_parse_hex_color(const char *s, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_UTIL_H */
