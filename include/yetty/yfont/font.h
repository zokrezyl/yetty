#ifndef YETTY_YFONT_FONT_H
#define YETTY_YFONT_FONT_H

/*
 * yetty_font_font - Non-monospace font interface
 *
 * Used by ypaint text spans. No fixed cell size.
 * Each text span specifies its own font size — shader scales.
 * Font provides gpu_resource_set (atlas + glyph metadata).
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_font;

/* Font style */
enum yetty_yfont_style {
    YETTY_YFONT_STYLE_REGULAR = 0,
    YETTY_YFONT_STYLE_BOLD = 1,
    YETTY_YFONT_STYLE_ITALIC = 2,
    YETTY_YFONT_STYLE_BOLD_ITALIC = 3,
};

YETTY_YRESULT_DECLARE(yetty_font_font, struct yetty_ydraw_font *);

/* Font ops */
struct yetty_yfont_font_ops {
    void (*destroy)(struct yetty_ydraw_font *self);

    /* Glyph lookup — loads on demand, returns glyph index */
    struct uint32_result (*get_glyph_index)(struct yetty_ydraw_font *self, uint32_t codepoint);
    struct uint32_result (*get_glyph_index_styled)(struct yetty_ydraw_font *self,
                                                   uint32_t codepoint,
                                                   enum yetty_yfont_style style);
    /* Inverse of get_glyph_index — given an atlas glyph index the font
     * previously handed out, recover the codepoint that produced it.
     * Used by selection / clipboard so glyph prims (which store
     * glyph_index, not codepoint) can be reconstructed back into UTF-8.
     * Returns an error result for an unknown glyph_index. */
    struct uint32_result (*get_codepoint)(struct yetty_ydraw_font *self, uint32_t glyph_index);

    /* Glyph loading */
    struct yetty_ycore_void_result (*load_glyphs)(struct yetty_ydraw_font *self,
                                                  const uint32_t *codepoints, size_t count);
    struct yetty_ycore_void_result (*load_basic_latin)(struct yetty_ydraw_font *self);

    /* Horizontal advance for a single codepoint at the given pixel size.
	 * Must not require glyph rasterization, atlas placement, or shader.
	 * Units: pixels at the requested font_size. */
    struct float_result (*get_advance)(struct yetty_ydraw_font *self, uint32_t codepoint,
                                       float font_size);

    /* Width of a UTF-8 byte range at the given pixel size.
	 * Must not require glyph rasterization, atlas placement, or shader.
	 * Implementations may override to handle kerning/shaping; the default
	 * behaviour is to sum get_advance() over codepoints. */
    struct float_result (*measure_text)(struct yetty_ydraw_font *self, const char *utf8,
                                        size_t len, float font_size);

    /* Base size the CDB was generated at */
    float (*get_base_size)(const struct yetty_ydraw_font *self);

    /* Dirty tracking */
    int (*is_dirty)(const struct yetty_ydraw_font *self);

    /* GPU resources — clears dirty internally */
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        struct yetty_ydraw_font *self);
};

/* Font base */
struct yetty_ydraw_font {
    const struct yetty_yfont_font_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_FONT_H */
