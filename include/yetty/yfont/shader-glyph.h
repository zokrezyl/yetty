#ifndef YETTY_YFONT_SHADER_GLYPH_H
#define YETTY_YFONT_SHADER_GLYPH_H

/*
 * Name -> codepoint lookup for shader glyphs.
 *
 * Shader glyphs are per-cell procedural fragment shaders (see
 * src/yetty/yfont/glyph-shaders/0xNN-name.wgsl and the dispatcher
 * spliced into shader-glyph-layer.wgsl). Server-side, yterm only deals
 * in codepoints — there is no name->codepoint table at run time.
 *
 * Clients (yecho, future tools) that translate user-friendly tokens like
 * `@spinner` into UTF-8 bytes need to map "spinner" -> U+E000 + local_id.
 * That table is generated at configure time from the .wgsl filenames and
 * exposed here.
 *
 * Codepoint allocation matches the BMP PUA layout consumed by
 * yterm/text-layer.c::resolve_glyph:
 *
 *     codepoint = YETTY_YFONT_SHADER_GLYPH_PUA_BASE + local_id   (= U+E000 + N)
 *
 * The lookup table is sorted by name; lookup is binary search.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* uint32_result */

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YFONT_SHADER_GLYPH_PUA_BASE 0x0000E000u

struct yetty_yfont_shader_glyph_entry {
    const char *name;     /* NUL-terminated, owned by the static table */
    uint32_t codepoint;   /* PUA_BASE + local_id */
};

/* Pointer to the static, sorted-by-name table. Lifetime = program. */
const struct yetty_yfont_shader_glyph_entry *yetty_yfont_shader_glyph_table(void);

/* Number of entries in the table above. */
size_t yetty_yfont_shader_glyph_count(void);

/* Codepoint for `name`. ERR("unknown shader glyph") if not found. */
struct uint32_result yetty_yfont_shader_glyph_codepoint(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_SHADER_GLYPH_H */
