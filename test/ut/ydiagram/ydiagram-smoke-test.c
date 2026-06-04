/*
 * ydiagram smoke test.
 *
 * Verifies the end-to-end pipeline: Mermaid input → graph IR → layout →
 * ydraw buffer with MSD/MSDF primitives. Asserts the expected counts of
 * nodes/edges in the IR plus a non-empty primitive byte stream in the
 * resulting buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydiagram/mermaid-parser.h>
#include <yetty/ydiagram/renderer.h>
#include <yetty/ydiagram/ydiagram.h>
#include <yetty/ydraw-core/drawable-list.h>

#define REQUIRE(cond, msg)                                                       \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);        \
            return 1;                                                            \
        }                                                                        \
    } while (0)

static const char *k_mermaid_input =
    "graph TD\n"
    "  A[Start] --> B{Decision}\n"
    "  B -->|Yes| C(Process)\n"
    "  B -->|No|  D((Done))\n"
    "  C --> D\n";

int main(void)
{
    /* 1. Detection. */
    REQUIRE(yetty_ydiagram_mermaid_can_parse(k_mermaid_input, strlen(k_mermaid_input)),
            "can_parse should accept the sample Mermaid input");
    REQUIRE(!yetty_ydiagram_mermaid_can_parse("hello world", 11),
            "can_parse should reject plain text");

    /* 2. Parse → IR. */
    struct yetty_ydiagram_graph g;
    struct yetty_ycore_void_result ir = yetty_ydiagram_graph_init(&g);
    REQUIRE(YETTY_IS_OK(ir), "graph_init");

    struct yetty_ycore_void_result pr =
        yetty_ydiagram_mermaid_parse(k_mermaid_input, strlen(k_mermaid_input), &g);
    REQUIRE(YETTY_IS_OK(pr), "mermaid_parse");
    REQUIRE(g.node_count == 4, "expected 4 nodes (A,B,C,D)");
    REQUIRE(g.edge_count == 4, "expected 4 edges");

    /* Shape assertions. */
    struct yetty_ydiagram_node *na = yetty_ydiagram_graph_find_node(&g, "A");
    struct yetty_ydiagram_node *nb = yetty_ydiagram_graph_find_node(&g, "B");
    struct yetty_ydiagram_node *nc = yetty_ydiagram_graph_find_node(&g, "C");
    struct yetty_ydiagram_node *nd = yetty_ydiagram_graph_find_node(&g, "D");
    REQUIRE(na && nb && nc && nd, "all four nodes resolvable by id");
    REQUIRE(na->shape == YETTY_YDIAGRAM_SHAPE_RECTANGLE, "A is rectangle");
    REQUIRE(nb->shape == YETTY_YDIAGRAM_SHAPE_DIAMOND, "B is diamond");
    REQUIRE(nc->shape == YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT, "C is rounded rect");
    REQUIRE(nd->shape == YETTY_YDIAGRAM_SHAPE_CIRCLE, "D is circle");

    /* 3. Layout. */
    struct yetty_ycore_void_result lr = yetty_ydiagram_layout(&g, NULL, NULL, NULL);
    REQUIRE(YETTY_IS_OK(lr), "layout");
    REQUIRE(na->layer >= 0, "A has a layer assignment");
    /* A is a source; either A and the rest have different layers. */
    REQUIRE(na->layer != nd->layer, "A and D end up on different layers");

    /* 4. Render via the top-level convenience entry. */
    struct yetty_ydiagram_buffer_result br =
        yetty_ydiagram_render_mermaid(k_mermaid_input, strlen(k_mermaid_input));
    REQUIRE(YETTY_IS_OK(br), "render_mermaid");
    REQUIRE(br.value != NULL, "buffer is non-null");

    size_t buf_size = yetty_ydraw_drawable_list_size(br.value);
    REQUIRE(buf_size > 0, "buffer has primitives");

    yetty_ydraw_drawable_list_destroy(br.value);
    yetty_ydiagram_graph_destroy(&g);

    printf("OK: ydiagram smoke test (%zu primitive bytes)\n", buf_size);
    return 0;
}
