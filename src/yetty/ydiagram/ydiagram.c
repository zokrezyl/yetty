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
#include <yetty/ydiagram/diagrams.h>
#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydiagram/mermaid-parser.h>
#include <yetty/ydiagram/renderer.h>
#include <yetty/ydraw-core/drawable-list.h>

struct yetty_ydiagram_buffer_result yetty_ydiagram_render_mermaid_full(
    const char *input, size_t len, const struct yetty_ydiagram_layout_params *layout_params,
    const struct yetty_ydiagram_render_options *render_options,
    yetty_ydiagram_measure_text_fn measure, void *measure_userdata)
{
    if (!input) {
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: NULL input");
    }

    enum yetty_ydiagram_kind kind = yetty_ydiagram_detect(input, len);

    /* Whether to clear the canvas + reset the cursor (full redraw) or flow
     * inline at the cursor (cat-like). Mirrors the render-options flag; the
     * default matches default_render_options() (clear = true). */
    bool clear_canvas = render_options ? render_options->clear_canvas : true;

    /* Sequence diagrams are not a layered graph — they own their layout and
     * render straight into a buffer. */
    if (kind == YETTY_YDIAGRAM_KIND_SEQUENCE) {
        struct yetty_ydiagram_seq_buffer_result sr =
            yetty_ydiagram_sequence_render(input, len, measure, measure_userdata, clear_canvas);
        if (YETTY_IS_ERR(sr)) {
            return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: sequence render failed", sr);
        }
        return YETTY_OK(yetty_ydiagram_buffer, sr.value);
    }

    struct yetty_ydiagram_graph graph;
    struct yetty_ycore_void_result ir = yetty_ydiagram_graph_init(&graph);
    YETTY_RETURN_IF_ERR(yetty_ydiagram_buffer, ir, "ydiagram: graph_init failed");

    struct yetty_ycore_void_result pr;
    switch (kind) {
    case YETTY_YDIAGRAM_KIND_FLOWCHART:
        pr = yetty_ydiagram_mermaid_parse(input, len, &graph);
        break;
    case YETTY_YDIAGRAM_KIND_STATE:
        pr = yetty_ydiagram_state_parse(input, len, &graph);
        break;
    case YETTY_YDIAGRAM_KIND_CLASS:
        pr = yetty_ydiagram_class_parse(input, len, &graph);
        break;
    case YETTY_YDIAGRAM_KIND_ER:
        pr = yetty_ydiagram_er_parse(input, len, &graph);
        break;
    default:
        yetty_ydiagram_graph_destroy(&graph);
        return YETTY_ERR(yetty_ydiagram_buffer,
                         "ydiagram: unrecognized diagram type (expected graph/flowchart, "
                         "stateDiagram, classDiagram, erDiagram, or sequenceDiagram)");
    }
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

    struct yetty_ydraw_drawable_list_config cfg = {
        .scene_min_x = graph.min_x,
        .scene_min_y = graph.min_y,
        .scene_max_x = graph.max_x,
        .scene_max_y = graph.max_y,
    };
    struct yetty_ydraw_drawable_list_result br = yetty_ydraw_drawable_list_config_buffer_create(&cfg);
    if (YETTY_IS_ERR(br)) {
        yetty_ydiagram_graph_destroy(&graph);
        return YETTY_ERR(yetty_ydiagram_buffer, "ydiagram: buffer create failed", br);
    }

    struct yetty_ycore_void_result rr =
        yetty_ydiagram_render(&graph, br.value, render_options, measure, measure_userdata);
    if (YETTY_IS_ERR(rr)) {
        yetty_ydraw_drawable_list_destroy(br.value);
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
