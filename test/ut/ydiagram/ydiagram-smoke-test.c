/*
 * ydiagram golden test (#421 — promoted from a smoke test).
 *
 * Exercises the end-to-end pipeline (Mermaid input → graph IR → layout →
 * ydraw buffer) and pins:
 *   - the parsed IR structure (node/edge counts, per-node shape),
 *   - layer assignment after layout,
 *   - a non-empty primitive stream, AND
 *   - byte-exact determinism: the same input renders to identical bytes twice.
 * No GPU, no fonts, no network — the layout (Sugiyama) and MSD/MSDF emission
 * are purely geometric and deterministic.
 */

#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydiagram/mermaid-parser.h>
#include <yetty/ydiagram/renderer.h>
#include <yetty/ydiagram/ydiagram.h>
#include <yetty/ydraw-core/drawable-list.h>

#include "ytest.h"

#include <stddef.h>
#include <string.h>

static const char *k_mermaid_input = "graph TD\n"
                                     "  A[Start] --> B{Decision}\n"
                                     "  B -->|Yes| C(Process)\n"
                                     "  B -->|No|  D((Done))\n"
                                     "  C --> D\n";

/*---------------------------------------------------------------------------
 * Parse → IR → layout: structure and shapes are pinned exactly.
 *-------------------------------------------------------------------------*/
static void test_pipeline(struct ytest *test)
{
    YTEST_CHECK(test, yetty_ydiagram_mermaid_can_parse(k_mermaid_input, strlen(k_mermaid_input)));
    YTEST_CHECK(test, !yetty_ydiagram_mermaid_can_parse("hello world", 11));

    struct yetty_ydiagram_graph g;
    YTEST_REQUIRE_OK(test, yetty_ydiagram_graph_init(&g));
    YTEST_REQUIRE_OK(test,
                     yetty_ydiagram_mermaid_parse(k_mermaid_input, strlen(k_mermaid_input), &g));

    YTEST_CHECK_EQ_INT(test, g.node_count, 4); /* A,B,C,D */
    YTEST_CHECK_EQ_INT(test, g.edge_count, 4);

    struct yetty_ydiagram_node *na = yetty_ydiagram_graph_find_node(&g, "A");
    struct yetty_ydiagram_node *nb = yetty_ydiagram_graph_find_node(&g, "B");
    struct yetty_ydiagram_node *nc = yetty_ydiagram_graph_find_node(&g, "C");
    struct yetty_ydiagram_node *nd = yetty_ydiagram_graph_find_node(&g, "D");
    YTEST_REQUIRE_NOT_NULL(test, na);
    YTEST_REQUIRE_NOT_NULL(test, nb);
    YTEST_REQUIRE_NOT_NULL(test, nc);
    YTEST_REQUIRE_NOT_NULL(test, nd);
    YTEST_CHECK_EQ_INT(test, na->shape, YETTY_YDIAGRAM_SHAPE_RECTANGLE);
    YTEST_CHECK_EQ_INT(test, nb->shape, YETTY_YDIAGRAM_SHAPE_DIAMOND);
    YTEST_CHECK_EQ_INT(test, nc->shape, YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT);
    YTEST_CHECK_EQ_INT(test, nd->shape, YETTY_YDIAGRAM_SHAPE_CIRCLE);

    YTEST_REQUIRE_OK(test, yetty_ydiagram_layout(&g, NULL, NULL, NULL));
    YTEST_CHECK(test, na->layer >= 0);
    YTEST_CHECK(test, na->layer != nd->layer); /* source and sink separate layers */

    yetty_ydiagram_graph_destroy(&g);
}

/*---------------------------------------------------------------------------
 * The top-level render produces a non-empty, byte-identical buffer twice.
 *-------------------------------------------------------------------------*/
static void test_render_deterministic(struct ytest *test)
{
    struct yetty_ydiagram_buffer_result a =
        yetty_ydiagram_render_mermaid(k_mermaid_input, strlen(k_mermaid_input));
    struct yetty_ydiagram_buffer_result b =
        yetty_ydiagram_render_mermaid(k_mermaid_input, strlen(k_mermaid_input));
    YTEST_REQUIRE_OK(test, a);
    YTEST_REQUIRE_OK(test, b);
    YTEST_REQUIRE_NOT_NULL(test, a.value);
    YTEST_REQUIRE_NOT_NULL(test, b.value);

    size_t sa = yetty_ydraw_drawable_list_size(a.value);
    size_t sb = yetty_ydraw_drawable_list_size(b.value);
    YTEST_CHECK(test, sa > 0);
    YTEST_REQUIRE_EQ_SIZE(test, sa, sb);
    YTEST_CHECK_MEM_EQ(test, yetty_ydraw_drawable_list_data(a.value),
                       yetty_ydraw_drawable_list_data(b.value), sa);

    yetty_ydraw_drawable_list_destroy(a.value);
    yetty_ydraw_drawable_list_destroy(b.value);
}

int main(void)
{
    struct ytest test = ytest_begin("ydiagram_golden");
    YTEST_RUN(&test, test_pipeline);
    YTEST_RUN(&test, test_render_deterministic);
    return ytest_end(&test);
}
