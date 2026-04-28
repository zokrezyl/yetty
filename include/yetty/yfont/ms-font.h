#ifndef YETTY_YFONT_MS_FONT_H
#define YETTY_YFONT_MS_FONT_H

/*
 * yetty_font_ms_font - Monospace font interface
 *
 * Separate from yetty_font_font (non-monospace).
 * Used by terminal text-layer. Exposes get_cell_size.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_font_ms_font;

/* Cell padding around the glyph, expressed as fractions of the natural glyph
 * dimensions at the requested font size. Applies to any monospace font
 * implementation (CDB/MSDF, raster).
 *
 * Cell dimensions are derived from these:
 *   cell_height = font_size       * (1 + top  + bottom)
 *   cell_width  = font_size / hwr * (1 + left + right)
 *
 * Defaults of 0 give a tight cell (cell exactly wraps the glyph), which is
 * the natural size and avoids the "glyph too small in cell" feel.
 */
struct yetty_font_ms_padding {
    float left;   /* fraction of glyph width  */
    float right;  /* fraction of glyph width  */
    float top;    /* fraction of glyph height */
    float bottom; /* fraction of glyph height */
};

/* Font style */
enum yetty_font_ms_style {
    YETTY_YFONT_MS_STYLE_REGULAR = 0,
    YETTY_YFONT_MS_STYLE_BOLD = 1,
    YETTY_YFONT_MS_STYLE_ITALIC = 2,
    YETTY_YFONT_MS_STYLE_BOLD_ITALIC = 3,
};

YETTY_YRESULT_DECLARE(yetty_font_ms_font, struct yetty_font_ms_font *);

struct yetty_font_ms_font_ops {
    void (*destroy)(struct yetty_font_ms_font *self);

    /* Cell size — the fixed cell dimensions for the grid */
    struct pixel_size_result (*get_cell_size)(const struct yetty_font_ms_font *self);

    /* Change the cell size. Implementations should re-rasterize the atlas
	 * (raster) or update the requested render size (MSDF) so glyphs scale
	 * together with the cell. Used by ZOOM_CELL_SIZE. */
    struct yetty_ycore_void_result (*set_cell_size)(struct yetty_font_ms_font *self,
                                                    struct pixel_size cell_size);

    /* Glyph lookup */
    struct uint32_result (*get_glyph_index)(struct yetty_font_ms_font *self, uint32_t codepoint);
    struct uint32_result (*get_glyph_index_styled)(struct yetty_font_ms_font *self,
                                                   uint32_t codepoint,
                                                   enum yetty_font_ms_style style);

    /* Resize — changes font size, recalculates cell size */
    struct yetty_ycore_void_result (*resize)(struct yetty_font_ms_font *self, float font_size);

    /* Glyph loading */
    struct yetty_ycore_void_result (*load_glyphs)(struct yetty_font_ms_font *self,
                                                  const uint32_t *codepoints, size_t count);
    struct yetty_ycore_void_result (*load_basic_latin)(struct yetty_font_ms_font *self);

    /* Dirty tracking */
    int (*is_dirty)(const struct yetty_font_ms_font *self);

    /* GPU resources — clears dirty internally */
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        struct yetty_font_ms_font *self);
};

struct yetty_font_ms_font {
    const struct yetty_font_ms_font_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MS_FONT_H */
