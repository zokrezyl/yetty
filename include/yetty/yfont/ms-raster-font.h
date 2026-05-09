#ifndef YETTY_YFONT_MS_RASTER_FONT_H
#define YETTY_YFONT_MS_RASTER_FONT_H

#include <yetty/yfont/ms-font.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/* Create monospace raster font from config */
struct yetty_font_ms_font_result yetty_yfont_ms_raster_font_create(
    struct yetty_yconfig_config *config, float cell_width, float cell_height);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_MS_RASTER_FONT_H */
