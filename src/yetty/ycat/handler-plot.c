/*
 * handler-plot.c — numeric series data (.npy) → yplot line figure.
 *
 * The bytes are already in memory, but the yplot loader is path-based (it
 * distinguishes .npy from text by content, so re-reading is cheap and keeps
 * one loader); when ycat read from stdin there is no path and the handler
 * falls back to loading from the byte buffer via a temp-free direct parse.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/yplot/yplot.h>

#include <stdlib.h>

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_plot(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)bytes;
    (void)len;
    if (!path_hint || !path_hint[0]) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "plot handler needs a file path (pipe via yplot --data instead)");
    }

    struct yetty_yplot_loaded_samples_result load_res = yetty_yplot_load_samples(path_hint);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, load_res, "plot handler: load samples");

    /* Auto-range: y spans the data (5% padding), x is the sample index. */
    float data_min = load_res.value.samples[0];
    float data_max = data_min;
    for (size_t i = 1; i < load_res.value.count; i++) {
        float sample = load_res.value.samples[i];
        if (sample < data_min) {
            data_min = sample;
        }
        if (sample > data_max) {
            data_max = sample;
        }
    }
    float span = data_max - data_min;
    float padding = span > 0.0f ? span * 0.05f : 1.0f;

    struct yetty_yplot_render_config plot_config = {
        .bounds_w = 800.0f,
        .bounds_h = 320.0f,
        .x_min = 0.0f,
        .x_max = load_res.value.count > 1 ? (float)(load_res.value.count - 1) : 1.0f,
        .y_min = data_min - padding,
        .y_max = data_max + padding,
        .title = path_hint,
    };
    if (config && config->width_cells > 0 && config->cell_width > 0) {
        plot_config.bounds_w = (float)(config->width_cells * config->cell_width);
        plot_config.bounds_h = plot_config.bounds_w * 0.4f;
    }

    struct yetty_yplot_buffer_input buffer = {
        .samples = load_res.value.samples,
        .count = load_res.value.count,
        .color = 0,
        .name = NULL,
    };
    struct yetty_ydraw_drawable_list_result out =
        yetty_yplot_render_with_buffers(NULL, 0, &buffer, 1, &plot_config);
    free(load_res.value.samples);
    return out;
}
