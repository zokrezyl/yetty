#ifndef YETTY_YPAINT_CORE_TEXT_SPAN_PRIM_H
#define YETTY_YPAINT_CORE_TEXT_SPAN_PRIM_H

/*
 * text-span-prim - flyweight primitive carrying a UTF-8 text run.
 *
 * Sits in the same flyweight tier as font-prim. The canvas expands a
 * TEXT_SPAN into glyph SDF prims at add_buffer time, after fonts have
 * been materialized.
 *
 * Wire layout (little-endian, 4-byte aligned):
 *   u32 type            (= YETTY_YPAINT_TYPE_TEXT_SPAN)
 *   u32 payload_size    (bytes of payload, padded to 4)
 *   f32 x, y, font_size, rotation
 *   u32 color           (RGBA, R in low byte)
 *   u32 layer
 *   i32 font_id         (must match a FONT prim's font_id, or -1 = default)
 *   u32 text_len
 *   u8  text[text_len]  (UTF-8)
 *   u8  pad[0..3]
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ypaint-core/flyweight.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YPAINT_TYPE_TEXT_SPAN 0x40000002u

struct yetty_ypaint_text_span_prim_view {
    float x, y;
    float font_size;
    float rotation;
    uint32_t color;
    uint32_t layer;
    int32_t font_id;
    const char *text; /* NOT NUL-terminated, len in text_len */
    uint32_t text_len;
};

size_t yetty_ypaint_text_span_prim_size_for(uint32_t text_len);

void yetty_ypaint_text_span_prim_write(uint8_t *out, float x, float y, float font_size,
                                       float rotation, uint32_t color, uint32_t layer,
                                       int32_t font_id, const char *text, uint32_t text_len);

int yetty_ypaint_text_span_prim_parse(const uint32_t *prim,
                                      struct yetty_ypaint_text_span_prim_view *out);

struct yetty_ypaint_prim_base_ops_ptr_result yetty_ypaint_text_span_prim_handler(
    uint32_t prim_type);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPAINT_CORE_TEXT_SPAN_PRIM_H */
