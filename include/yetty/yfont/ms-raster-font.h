#ifndef YETTY_YFONT_MS_RASTER_FONT_H
#define YETTY_YFONT_MS_RASTER_FONT_H

#include <yetty/yfont/ms-font.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/* Create monospace raster font from config (uses the global font/family) */
struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create(
    struct yetty_yconfig_config *config, float cell_width, float cell_height);

/* Create monospace raster font for an explicit font name, resolved as
 * <paths/fonts>/<name>{-Regular,-Bold,-Oblique,-BoldOblique}.ttf. Used by the
 * terminal's config range faces (CJK/emoji), which name their font per range
 * instead of using the global family. */
struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create_named(
    struct yetty_yconfig_config *config, const char *font_name, float cell_width,
    float cell_height);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MS_RASTER_FONT_H */
