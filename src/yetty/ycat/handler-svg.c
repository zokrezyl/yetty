/*
 * handler-svg.c - SVG → ydraw buffer.
 *
 * Thin wrapper around yetty_ysvg_render: re-packs the ycat config into
 * an ysvg config and lifts the result into the ycat handler return type.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ysvg/ysvg.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_svg(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;

    struct yetty_ysvg_render_config svgcfg = {0};
    if (config) {
        svgcfg.cell_width = config->cell_width;
        svgcfg.cell_height = config->cell_height;
        svgcfg.width_cells = config->width_cells;
        svgcfg.height_cells = config->height_cells;
    }

    struct yetty_ysvg_render_result r =
        yetty_ysvg_render((const char *)bytes, len, NULL, 0, &svgcfg);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, r.error.msg);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, r.value.buffer);
}
