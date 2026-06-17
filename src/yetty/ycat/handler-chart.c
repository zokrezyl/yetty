/*
 * handler-chart.c — chart data (CSV / JSON / YAML) → ydraw buffer.
 *
 * Thin wrapper around yetty_ychart_render_data_full. Like the mermaid
 * handler, ycat doesn't carry a font_id-aware measure callback (the canvas's
 * default font measures at receive time), so we render without one — axis /
 * legend text falls back to the 0.6 × font_size × char_count heuristic for
 * layout. A future change should plumb the configured font through ycat_config.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ychart/ychart.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_chart(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    /* ycat is cat-like: the chart must flow inline at the cursor, not clear the
     * pane and jump to the origin. Render with clear_canvas = false. */
    struct yetty_ychart_render_options opts = yetty_ychart_default_render_options();
    opts.clear_canvas = false;
    /* Size the chart to the pane width when ycat knows it. */
    if (config && config->width_cells > 0 && config->cell_width > 0) {
        opts.width = (float)(config->width_cells * config->cell_width);
        opts.height = opts.width * 0.6f;
    }

    struct yetty_ychart_buffer_result r = yetty_ychart_render_data_full(
        (const char *)bytes, len, path_hint, YETTY_YCHART_KIND_AUTO, &opts, NULL, NULL);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, r.error.msg);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, r.value);
}
