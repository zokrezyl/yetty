#ifndef YETTY_YVTERM_SHADER_GLYPH_CODEPOINT_H
#define YETTY_YVTERM_SHADER_GLYPH_CODEPOINT_H

/*
 * Shader-glyph codepoint <-> glyph_index encoding.
 *
 * A shader glyph is an animated procedural fragment shader rendered per
 * cell by the shader-glyph figure (see yvterm/shader-glyph-figure.c). The
 * text-layer marks a cell as a shader glyph by storing a glyph_index in the
 * top of the u32 range; the figure scans for those cells and draws them.
 *
 * These are free inline helpers (not yclass methods), shared between the
 * figure and the text-layer's glyph resolver, so the encoding lives in one
 * place. PUA range U+100000..U+100FFF (Supplementary PUA-B) maps 1:1 to
 * local_id 0..4095.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Inverse of _id_from_codepoint — recover the PUA codepoint that produced
 * this shader-glyph id. Used by selection extraction so a PUA cell on the
 * clipboard round-trips to itself. */
static inline uint32_t yetty_shader_glyph_codepoint_from_id(uint32_t glyph_index)
{
    return YETTY_SHADER_GLYPH_PUA_BASE + yetty_shader_glyph_local_id(glyph_index);
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_SHADER_GLYPH_CODEPOINT_H */
