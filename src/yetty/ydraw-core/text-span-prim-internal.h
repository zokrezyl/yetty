/* text-span-prim — module-internal declarations (ydraw-core only).
 *
 * These functions are called only from within the ydraw-core module
 * (text-span-prim.c implementation, buffer.c packing wrappers). The public
 * surface (parse, view struct, type id, handler) lives in
 * include/yetty/ydraw-core/text-span-prim.h.
 */
#ifndef YETTY_YDRAW_CORE_TEXT_SPAN_PRIM_INTERNAL_H
#define YETTY_YDRAW_CORE_TEXT_SPAN_PRIM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t yetty_ydraw_text_span_prim_size_for(uint32_t text_len);

void yetty_ydraw_text_span_prim_write(uint8_t *out, float x, float y, float font_size,
                                            float rotation, uint32_t color, uint32_t layer,
                                            int32_t font_id, const char *text, uint32_t text_len);

/* Like _write, plus PDF Tc/Tw spacing. Tc adds per-codepoint, Tw per
 * U+0020 only. Both in display pixels at the current font_size. */
void yetty_ydraw_text_span_prim_write_full(uint8_t *out, float x, float y, float font_size,
                                                 float rotation, uint32_t color, uint32_t layer,
                                                 int32_t font_id, const char *text,
                                                 uint32_t text_len, float char_spacing,
                                                 float word_spacing);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_TEXT_SPAN_PRIM_INTERNAL_H */
