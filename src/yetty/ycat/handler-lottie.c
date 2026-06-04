/*
 * handler-lottie.c - Lottie (Bodymovin) → ydraw buffer.
 *
 * Thin wrapper around yetty_ylottie_render: re-packs the ycat config into a
 * ylottie config and lifts the result into the ycat handler return type.
 * Renders a single frame (the composition in-point) into a static buffer.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ylottie/ylottie.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_lottie(const uint8_t *bytes, size_t len,
                                                              const char *path_hint,
                                                              const struct yetty_ycat_config
                                                                  *config)
{
    (void)path_hint;

    struct yetty_ylottie_render_config lcfg = {0};
    if (config) {
        lcfg.cell_width = config->cell_width;
        lcfg.cell_height = config->cell_height;
        lcfg.width_cells = config->width_cells;
        lcfg.height_cells = config->height_cells;
    }

    struct yetty_ylottie_render_result r =
        yetty_ylottie_render((const char *)bytes, len, NULL, 0, &lcfg);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, r.error.msg);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, r.value.buffer);
}
