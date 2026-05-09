/*
 * handler-markdown.c - markdown → ypaint buffer.
 *
 * Thin wrapper around yetty_ymarkdown_render: re-packs the ycat config into
 * a ymarkdown config and lifts the result into the ycat handler return type.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ymarkdown/ymarkdown.h>

struct yetty_ypaint_core_buffer_result yetty_ycat_handler_markdown(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;

    struct yetty_ymarkdown_render_config mdcfg = {0};
    if (config) {
        mdcfg.cell_width = config->cell_width;
        mdcfg.cell_height = config->cell_height;
        mdcfg.width_cells = config->width_cells;
        mdcfg.height_cells = config->height_cells;
    }

    struct yetty_ymarkdown_render_result r =
        yetty_ymarkdown_render((const char *)bytes, len, NULL, 0, &mdcfg);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ypaint_core_buffer, r.error.msg);
    }
    return YETTY_OK(yetty_ypaint_core_buffer, r.value.buffer);
}
