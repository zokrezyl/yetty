#ifndef YETTY_YDIAGRAM_DIAGRAMS_H
#define YETTY_YDIAGRAM_DIAGRAMS_H

/*
 * diagrams — diagram-family dispatch for the Mermaid-style front end.
 *
 * `graph` / `flowchart` keep their own parser (mermaid-parser.h). The other
 * Mermaid diagram families are detected by their header keyword and routed to
 * the parser here:
 *
 *   stateDiagram / stateDiagram-v2 → state_parse   (graph IR, reuses layout)
 *   classDiagram                   → class_parse   (record nodes + UML arrows)
 *   erDiagram                      → er_parse       (record nodes + crow's-foot)
 *   sequenceDiagram                → sequence_render (own layout → ydraw buffer)
 *
 * state/class/er fill the shared graph IR and flow through the Sugiyama layout
 * + renderer; sequence diagrams are not a layered graph, so they render
 * straight to a ydraw buffer.
 */

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydraw-list/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_ydiagram_kind {
    YETTY_YDIAGRAM_KIND_FLOWCHART,
    YETTY_YDIAGRAM_KIND_STATE,
    YETTY_YDIAGRAM_KIND_CLASS,
    YETTY_YDIAGRAM_KIND_ER,
    YETTY_YDIAGRAM_KIND_SEQUENCE,
    YETTY_YDIAGRAM_KIND_UNKNOWN,
};

/* Sniff the first non-blank, non-comment line's keyword. */
enum yetty_ydiagram_kind yetty_ydiagram_detect(const char *input, size_t len);

/* Parsers that fill the shared graph IR (caller inits the graph). */
struct yetty_ycore_void_result yetty_ydiagram_state_parse(const char *input, size_t len,
                                                          struct yetty_ydiagram_graph *out_graph);
struct yetty_ycore_void_result yetty_ydiagram_class_parse(const char *input, size_t len,
                                                          struct yetty_ydiagram_graph *out_graph);
struct yetty_ycore_void_result yetty_ydiagram_er_parse(const char *input, size_t len,
                                                       struct yetty_ydiagram_graph *out_graph);

/* Sequence diagrams own their layout; render straight into a fresh buffer.
 * `clear_canvas` mirrors the render-options flag: true prepends a CMD_ZERO
 * (full redraw, jump to pane origin); false is cat-like inline at the cursor. */
YETTY_YRESULT_DECLARE(yetty_ydiagram_seq_buffer, struct yetty_ydraw_drawable_list *);
struct yetty_ydiagram_seq_buffer_result yetty_ydiagram_sequence_render(
    const char *input, size_t len, yetty_ydiagram_measure_text_fn measure, void *measure_userdata,
    bool clear_canvas);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDIAGRAM_DIAGRAMS_H */
