/*
 * ychart.c — high-level glue: data bytes → chart IR → ydraw buffer.
 *
 * Owns nothing across calls — every entry builds a fresh chart and a fresh
 * ydraw buffer. The buffer is the only thing handed back to the caller; the
 * chart IR is destroyed before return.
 */

#include <yetty/ychart/ychart.h>

#include <stddef.h>

#include <yetty/ychart/chart-ir.h>
#include <yetty/ychart/data-parser.h>
#include <yetty/ychart/renderer.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

/* Pick a chart kind from the data shape when neither the document nor the
 * caller specified one. Specialised kinds (pie/donut/radar/treemap) are only
 * reached via an explicit request — auto stays in the safe, general families. */
static enum yetty_ychart_kind auto_kind(const struct yetty_ychart_chart *chart)
{
    if (chart->flow_count > 0) {
        return YETTY_YCHART_KIND_SANKEY;
    }
    if (chart->series_count <= 1) {
        return YETTY_YCHART_KIND_COLUMN;
    }
    return chart->category_count > 12 ? YETTY_YCHART_KIND_LINE : YETTY_YCHART_KIND_COLUMN;
}

struct yetty_ychart_buffer_result yetty_ychart_render_data_full(
    const char *input, size_t len, const char *path, enum yetty_ychart_kind kind_override,
    const struct yetty_ychart_render_options *render_options,
    yetty_ychart_measure_text_fn measure, void *measure_userdata)
{
    if (!input) {
        return YETTY_ERR(yetty_ychart_buffer, "ychart: NULL input");
    }

    struct yetty_ychart_chart chart;
    struct yetty_ycore_void_result init = yetty_ychart_chart_init(&chart);
    YETTY_RETURN_IF_ERR(yetty_ychart_buffer, init, "ychart: chart init failed");

    struct yetty_ycore_void_result parsed = yetty_ychart_parse(input, len, path, &chart);
    if (YETTY_IS_ERR(parsed)) {
        yetty_ychart_chart_destroy(&chart);
        return YETTY_ERR(yetty_ychart_buffer, "ychart: parse failed", parsed);
    }

    if (chart.category_count == 0 && chart.flow_count == 0) {
        yetty_ychart_chart_destroy(&chart);
        return YETTY_ERR(yetty_ychart_buffer, "ychart: no chartable data found");
    }

    /* Resolve the kind: explicit override wins, then the document's own kind,
     * then a shape-based guess. */
    if (kind_override != YETTY_YCHART_KIND_AUTO) {
        chart.kind = kind_override;
    } else if (chart.kind == YETTY_YCHART_KIND_AUTO) {
        chart.kind = auto_kind(&chart);
    }

    struct yetty_ydraw_drawable_list_result buf =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(buf)) {
        yetty_ychart_chart_destroy(&chart);
        return YETTY_ERR(yetty_ychart_buffer, "ychart: buffer create failed", buf);
    }

    struct yetty_ycore_void_result rendered =
        yetty_ychart_render(&chart, buf.value, render_options, measure, measure_userdata);
    if (YETTY_IS_ERR(rendered)) {
        yetty_ydraw_drawable_list_destroy(buf.value);
        yetty_ychart_chart_destroy(&chart);
        return YETTY_ERR(yetty_ychart_buffer, "ychart: render failed", rendered);
    }

    yetty_ychart_chart_destroy(&chart);
    return YETTY_OK(yetty_ychart_buffer, buf.value);
}

struct yetty_ychart_buffer_result yetty_ychart_render_data(const char *input, size_t len)
{
    return yetty_ychart_render_data_full(input, len, NULL, YETTY_YCHART_KIND_AUTO, NULL, NULL,
                                          NULL);
}
