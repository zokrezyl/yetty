/* font-prim — module-internal declarations (ypaint-core only).
 *
 * The packers are called from buffer.c when wrapping FONT primitive
 * insertions; they are not part of the inter-module public API. The public
 * surface (parse, view struct, type id, handler) lives in
 * include/yetty/ydraw-core/font-prim.h.
 */
#ifndef YETTY_YDRAW_CORE_FONT_PRIM_INTERNAL_H
#define YETTY_YDRAW_CORE_FONT_PRIM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size in bytes of a packed FONT prim including the 8-byte FAM header
 * and trailing alignment padding. */
size_t yetty_ydraw_core_font_prim_size_for(uint32_t name_len, uint32_t ttf_len);

/* Pack a FONT prim into out. out must have at least
 * yetty_ydraw_core_font_prim_size_for(name_len, ttf_len) bytes. */
void yetty_ydraw_core_font_prim_write(uint8_t *out, int32_t font_id, const char *name,
                                       uint32_t name_len, const uint8_t *ttf, uint32_t ttf_len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_FONT_PRIM_INTERNAL_H */
