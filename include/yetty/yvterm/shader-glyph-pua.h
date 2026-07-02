/*
 * shader-glyph-pua.h — PUA-codepoint ↔ shader-glyph-id mapping helpers.
 *
 * Hand-written (not codegen output). The animated procedural "shader glyphs"
 * occupy the high glyph-index range and are addressed by Supplementary
 * Private-Use-Area-B codepoints (U+100000..U+100FFF). These small inline
 * helpers convert between a cell's glyph_index, a shader local-id, and the
 * PUA codepoint — shared by the vterm text renderer (glyph resolver) and the
 * shader-glyph layer (shader-glyph-layer.c). Kept in its own leaf header with
 * no dependency on any class API.
 */
#ifndef YETTY_YVTERM_SHADER_GLYPH_PUA_H
#define YETTY_YVTERM_SHADER_GLYPH_PUA_H

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

#endif /* YETTY_YVTERM_SHADER_GLYPH_PUA_H */
