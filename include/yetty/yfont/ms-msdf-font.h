#ifndef YETTY_YFONT_MS_MSDF_FONT_H
#define YETTY_YFONT_MS_MSDF_FONT_H

#include <yetty/yfont/ms-font.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create monospace MSDF font from a .cdb file.
 *
 * font_size is the visual height of the glyph extent in pixels. The cell
 * size is derived as font_size * (1 + top + bottom) high and
 * font_size/hw_ratio * (1 + left + right) wide.
 */
struct yetty_font_ms_font_result yetty_yfont_ms_msdf_font_create(
    const char *cdb_path, const char *shader_path, float font_size,
    struct yetty_yfont_ms_padding padding);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MS_MSDF_FONT_H */
