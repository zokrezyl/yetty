#ifndef YETTY_YMSDF_GEN_H
#define YETTY_YMSDF_GEN_H

/*
 * ymsdf-gen - MSDF glyph CDB generation
 *
 * Generates .cdb files from TTF fonts containing MSDF glyph bitmaps.
 * Each glyph entry: msdf_glyph_header (28 bytes) + RGBA8 pixel data.
 * CPU implementation uses msdfgen library with multi-threaded generation.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * CDB glyph header (28 bytes, matches old MsdfGlyphData)
 *===========================================================================*/

struct yetty_ymsdf_gen_glyph_header {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    float bearing_x;
    float bearing_y;
    float size_x;
    float size_y;
    float advance;
    /* followed by width * height * 4 bytes RGBA8 pixel data */
};

/*=============================================================================
 * Config
 *===========================================================================*/

struct yetty_ymsdf_gen_config {
    const char *ttf_path;
    const char *output_dir;
    float font_size;   /* pixels, default 32 */
    float pixel_range; /* default 4 */
    int thread_count;  /* 0 = auto */
    int include_nerd_fonts;
    int include_cjk;
    int all_glyphs;
};

/*=============================================================================
 * API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ymsdf_gen_cpu_generate(
    const struct yetty_ymsdf_gen_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSDF_GEN_H */
