#ifndef YETTY_YDRAW_CORE_TEXT_SPAN_PRIM_H
#define YETTY_YDRAW_CORE_TEXT_SPAN_PRIM_H

/*
 * text-drawable-list - drawable-list entry carrying a UTF-8 text run.
 *
 * Sits in the same drawable-list tier as font-resource. The canvas expands a
 * TEXT_SPAN into glyph SDF prims at add_buffer time, after fonts have
 * been materialized.
 *
 * Wire layout (little-endian, 4-byte aligned):
 *   u32 type            (= YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST)
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
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ydraw-core/drawable-list-registry.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST 0x40000002u

struct yetty_ydraw_text_drawable_list_view {
    float x, y;
    float font_size;
    float rotation;
    uint32_t color;
    uint32_t layer;
    int32_t font_id;
    const char *text; /* NOT NUL-terminated, len in text_len */
    uint32_t text_len;
    /* PDF text-state spacing parameters that the producer (ypdf) read off
     * the content stream. The canvas applies them per-character (Tc) and
     * per-space (Tw) when expanding the span into glyph prims, so a Tj
     * that contains internal spaces lays out at the same positions a
     * reference PDF renderer would compute.
     *
     * Both values are in DISPLAY PIXELS (already multiplied by font_size
     * and any horizontal scaling) — the canvas adds them straight to its
     * cursor without further conversion.
     *
     * Producers that don't care leave them 0 (the buffer_add_text wrapper
     * defaults both to 0). Older buffers without these trailing fields
     * parse with both values defaulting to 0 — the prim payload size
     * tells the parser whether the trailing fields are present. */
    float char_spacing;
    float word_spacing;
};

int yetty_ydraw_text_drawable_list_parse(
    const uint32_t *prim, struct yetty_ydraw_text_drawable_list_view *out YETTY_ANNOT_OUT);

struct yetty_ydraw_drawable_list_entry_ops_ptr_result yetty_ydraw_text_drawable_list_handler(
    uint32_t drawable_type);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_TEXT_SPAN_PRIM_H */
