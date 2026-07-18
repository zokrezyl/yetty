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

/* Generator is opaque here — declared in <yetty/ymsdf/generator.h>. Taken as a
 * bare pointer and invoked only through its ops vtable, so this stays a
 * header-only dependency (no link to yetty_ymsdf, no WebGPU pulled into the
 * GPU-less yfont clients). */
struct yetty_ymsdf_generator;

/* Resolve a usable MSDF glyph CDB for a font face, generating it on the GPU
 * when neither a shipped nor a cached atlas exists. The face file stem is
 * `<name><style_suffix>` (e.g. name "DejaVuSansMNerdFontMono", style_suffix
 * "-Regular" / "-Bold" / "-Oblique"). Resolution order:
 *
 *   1. Installed  <fonts_dir>/../msdf-fonts/<stem>.cdb — used silently.
 *   2. Cached     <cache_dir>/msdf-fonts/<stem>.cdb — used silently (a previous
 *      run already generated it — no warning on reuse).
 *   3. Generated  the cache CDB is built from <fonts_dir>/<stem>.ttf with
 *      `generator`. This case logs one warning (neither location had a CDB)
 *      followed by one info (the CDB was generated into the cache).
 *
 * On success writes the usable CDB path into cdb_path_out and returns OK.
 * Returns an error (without warning) when no CDB can be produced — no source
 * TTF, no generator, empty cache dir, or the generation itself failed; the
 * caller decides how to surface that. `style_suffix` may be "" (bare <name>).
 * `cache_dir` and `generator` may be NULL/empty, in which case only the
 * installed path is considered. */
struct yetty_ycore_void_result yetty_yfont_msdf_resolve_cdb(
    struct yetty_ymsdf_generator *generator, const char *fonts_dir, const char *cache_dir,
    const char *name, const char *style_suffix, char *cdb_path_out, size_t cdb_path_cap);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MSDF_FONT_H */
