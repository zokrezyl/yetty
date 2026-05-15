/*
 * handler-image.c - image bytes (PNG/JPG/...) → ypaint buffer.
 *
 * Thin glue: forwards the raw encoded bytes to yetty_yimage_render, which
 * handles the stb_image decode and the wire-format serialization for the
 * yimage complex primitive. Display bounds default to the source pixel
 * dimensions, but a non-zero `width_cells * cell_width` (or height) from
 * the caller's config pins them instead.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/yimage/yimage.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ydraw_core_buffer_result yetty_ycat_handler_image(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint; /* yimage decodes from in-memory bytes; no need for a path. */

    if (!bytes || len == 0) {
        return YETTY_ERR(yetty_ydraw_core_buffer, "ycat image: no bytes");
    }

    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = 0.0f,
        .bounds_h = 0.0f,
    };
    if (config) {
        if (config->width_cells > 0 && config->cell_width > 0) {
            cfg.bounds_w = (float)(config->width_cells * config->cell_width);
        }
        if (config->height_cells > 0 && config->cell_height > 0) {
            cfg.bounds_h = (float)(config->height_cells * config->cell_height);
        }
    }

    return yetty_yimage_render(bytes, len, &cfg);
}
