#ifndef YETTY_YTERM_SHADER_GLYPH_LAYER_H
#define YETTY_YTERM_SHADER_GLYPH_LAYER_H

#include <stdint.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shader-glyph layer — animated procedural glyphs.
 *
 * Cells whose glyph_index falls in the shader-glyph range are rendered by
 * this layer (animated, per-frame fragment-shader procedurals) instead of
 * the text-layer's font. The text-layer skips font sampling for those
 * cells so the two layers compose without overdraw.
 *
 * Allocation scheme — no bit-pattern reservation, just a high-water mark.
 * Regular fonts (raster, msdf) hand out indices from 0 upward. The
 * shader-glyph "font" hands out indices from UINT32_MAX downward. The
 * test for "this is a shader cell" is simply glyph_index >= BASE, where
 * BASE is set so high (top half of u32) that no real font ever reaches it.
 *
 *   local_id 0 -> glyph_index 0xFFFFFFFF
 *   local_id 1 -> glyph_index 0xFFFFFFFE
 *   ...
 *
 * The PUA range U+100000..U+100FFF (Supplementary PUA-B) is mapped 1:1
 * to local_id 0..4095 by the text-layer's glyph resolver (see
 * resolve_glyph in text-layer.c). Print a PUA codepoint and you get an
 * animated shader glyph at that cell.
 *
 * Why PUA-B and not PUA-A or BMP PUA: verified against the bundled
 * DejaVuSansM Nerd Font (13,718 codepoints in cmap) —
 *   BMP PUA  (U+E000..U+F8FF)   : 3,488 codepoints used (Powerline +
 *                                  Codicons + most icon blocks)
 *   PUA-A    (U+F0000..U+FFFFD) : 6,896 codepoints used (Material Design
 *                                  Icons U+F0001..U+F1AF0)
 *   PUA-B    (U+100000..U+10FFFD): 0 codepoints used — empty
 * Earlier moves into BMP PUA and PUA-A both collided: a Nerd Font icon
 * whose codepoint matched a yetty shader local-id pinned shader-glyph
 * `is_empty` at 0 and ran the 60 Hz animation timer indefinitely.
 */

#define YETTY_SHADER_GLYPH_BASE 0x80000000u
#define YETTY_SHADER_GLYPH_PUA_BASE 0x00100000u
#define YETTY_SHADER_GLYPH_PUA_END 0x00101000u /* exclusive — 4096-slot window */

static inline int yetty_shader_glyph_is(uint32_t glyph_index)
{
    return glyph_index >= YETTY_SHADER_GLYPH_BASE;
}

static inline uint32_t yetty_shader_glyph_id_from_local(uint32_t local_id)
{
    return 0xFFFFFFFFu - local_id;
}

static inline uint32_t yetty_shader_glyph_local_id(uint32_t glyph_index)
{
    return 0xFFFFFFFFu - glyph_index;
}

static inline int yetty_shader_glyph_codepoint_in_range(uint32_t cp)
{
    return cp >= YETTY_SHADER_GLYPH_PUA_BASE && cp < YETTY_SHADER_GLYPH_PUA_END;
}

static inline uint32_t yetty_shader_glyph_id_from_codepoint(uint32_t cp)
{
    return yetty_shader_glyph_id_from_local(cp - YETTY_SHADER_GLYPH_PUA_BASE);
}

/* Inverse of yetty_shader_glyph_id_from_codepoint — recover the PUA
 * codepoint that produced this shader-glyph id. Used by selection
 * extraction so a PUA cell on the clipboard round-trips to itself. */
static inline uint32_t yetty_shader_glyph_codepoint_from_id(uint32_t glyph_index)
{
    return YETTY_SHADER_GLYPH_PUA_BASE + yetty_shader_glyph_local_id(glyph_index);
}

/*
 * Create the shader-glyph layer.
 *
 * The layer reads cells from `text_layer` (must outlive this layer) and
 * renders animated procedurals at every cell in the shader-glyph range.
 * It self-clocks animation and re-requests render after each frame so
 * the animation loop runs at the screen frame rate.
 */
struct yetty_yterm_terminal_layer_result yetty_yterm_shader_glyph_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height,
    struct yetty_yrender_terminal_layer *text_layer, const struct yetty_context *context,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_SHADER_GLYPH_LAYER_H */
