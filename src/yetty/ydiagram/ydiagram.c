/*
 * ydiagram.c — high-level glue: text → graph IR → layout → ydraw buffer.
 *
 * Owns nothing across calls — every entry creates a fresh graph and a
 * fresh ydraw buffer. The buffer is the only thing handed back to the
 * caller; the graph is destroyed before return.
 */

#include <yetty/ydiagram/ydiagram.h>

#include <stddef.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydiagram/mermaid-parser.h>
#include <yetty/ydiagram/renderer.h>
#include <yetty/ydraw-core/buffer.h>

struct yetty_ydiagram_buffer_result yetty_ydiagram_render_mermaid_full(
    const char *input, size_t len, const struct yetty_ydiagram_layout_params *layout_params,
    const struct yetty_ydiagram_render_options *render_options,
    yetty_ydiagram_measure_text_fn measure, void *measure_userdata)
{
    if (!input) {
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: NULL input");
    }
    if (!yetty_ydiagram_mermaid_can_parse(input, len)) {
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: input is not Mermaid");
    }

    struct yetty_ydiagram_graph graph;
    struct yetty_ycore_void_result ir = yetty_ydiagram_graph_init(&graph);
    YETTY_RETURN_IF_ERR(yetty_ydiagram_buffer, ir, "ydiagram: graph_init failed");

    struct yetty_ycore_void_result pr = yetty_ydiagram_mermaid_parse(input, len, &graph);
    if (YETTY_IS_ERR(pr)) {
        yetty_ydiagram_graph_destroy(&graph);
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: parse failed", pr);
    }

    struct yetty_ycore_void_result lr =
        yetty_ydiagram_layout(&graph, layout_params, measure, measure_userdata);
    if (YETTY_IS_ERR(lr)) {
        yetty_ydiagram_graph_destroy(&graph);
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: layout failed", lr);
    }

    struct yetty_ydraw_core_buffer_config cfg = {
        .scene_min_x = graph.min_x,
        .scene_min_y = graph.min_y,
        .scene_max_x = graph.max_x,
        .scene_max_y = graph.max_y,
    };
    struct yetty_ydraw_core_buffer_result br =
        yetty_ydraw_core_buffer_config_buffer_create(&cfg);
    if (YETTY_IS_ERR(br)) {
        yetty_ydiagram_graph_destroy(&graph);
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: buffer create failed", br);
    }

    struct yetty_ycore_void_result rr = yetty_ydiagram_render(
        &graph, br.value, render_options, measure, measure_userdata);
    if (YETTY_IS_ERR(rr)) {
        yetty_ydraw_core_buffer_destroy(br.value);
        yetty_ydiagram_graph_destroy(&graph);
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: render failed", rr);
    }

    yetty_ydiagram_graph_destroy(&graph);
    return YETTY_OK(yetty_ydiagram_buffer, br.value);
}

struct yetty_ydiagram_buffer_result yetty_ydiagram_render_mermaid(const char *input, size_t len)
{
    return yetty_ydiagram_render_mermaid_full(input, len, NULL, NULL, NULL, NULL);
}
