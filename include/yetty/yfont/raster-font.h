#ifndef YETTY_YFONT_RASTER_FONT_H
#define YETTY_YFONT_RASTER_FONT_H

/*
 * raster-font - Non-monospace raster (FreeType-backed) font
 *
 * Implements yetty_font_font. Used by client libraries (e.g. ypdf) that need:
 *   - font metrics derived from raw TTF data (no pre-generated CDB)
 *   - proportional glyph advances
 *   - lightweight alternative to the MSDF atlas pipeline
 *
 * Two constructors:
 *   - create_from_file : load TTF from a file path
 *   - create_from_data : load TTF from an in-memory byte range (e.g. fonts
 *                        embedded inside a PDF)
 *
 * Both take an optional shader_path. When shader_path is NULL, the font is
 * in "metrics-only" mode: get_advance / measure_text work without any atlas
 * allocation or GPU resource setup, and get_gpu_resource_set returns an error.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/yfont/font.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load from TTF file path.
 * base_size: pixel size the atlas rasterizes at; measurement is base-size
 * independent (scales with the requested font_size). */
struct yetty_font_font_result yetty_font_raster_font_create_from_file(const char *ttf_path,
                                                                      const char *shader_path,
                                                                      float base_size);

/* Load from TTF bytes held in memory. name is used only for diagnostics. */
struct yetty_font_font_result yetty_font_raster_font_create_from_data(const uint8_t *ttf_data,
                                                                      size_t ttf_size,
                                                                      const char *name,
                                                                      const char *shader_path,
                                                                      float base_size);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_RASTER_FONT_H */
