#ifndef YETTY_YFONT_MSDF_FONT_H
#define YETTY_YFONT_MSDF_FONT_H

#include <yetty/yfont/font.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create non-monospace MSDF font from .cdb file.
 * Can be used at any font size — scaling handled by shader.
 *
 * `namespace` — unique identifier for this font instance, used as the
 * shader-namespace prefix when the binder merges this font's wgsl into a
 * layer shader. Must be unique among all msdf-font instances attached to
 * the same layer (otherwise the merged WGSL has duplicate uniform fields,
 * helper functions and `<ns>_buffer_offset` constants and won't compile).
 *
 * The natural choice is the content-addressed name the caller already
 * uses to cache the CDB on disk (e.g. the FNV1a-64 hex of the TTF bytes
 * for PDF-embedded fonts, or the font-family name for the default font).
 * Same content → same namespace → same merged-shader cache key.
 *
 * NULL is allowed only when no other msdf-font shares the layer.
 */
struct yetty_font_font_result yetty_yfont_msdf_font_create(const char *cdb_path,
                                                           const char *shader_path,
                                                           const char *namespace);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MSDF_FONT_H */
