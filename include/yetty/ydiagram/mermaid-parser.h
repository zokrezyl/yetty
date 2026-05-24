#ifndef YETTY_YDIAGRAM_MERMAID_PARSER_H
#define YETTY_YDIAGRAM_MERMAID_PARSER_H

/*
 * mermaid-parser — Mermaid flowchart syntax → graph IR.
 *
 * Supported syntax:
 *   graph TD / TB / BT / LR / RL
 *   flowchart TD / TB / BT / LR / RL
 *   A[text]       rectangle
 *   A(text)       rounded rectangle
 *   A((text))     circle
 *   A{text}       diamond
 *   A{{text}}     hexagon
 *   A[(text)]     cylinder
 *   A([text])     stadium / pill
 *   A[/text/]     parallelogram
 *   A -->  B      arrow
 *   A ---  B      line (no arrow)
 *   A -.-> B      dashed arrow
 *   A ==>  B      thick arrow
 *   A -->|label| B
 *   subgraph id [label] ... end
 *
 * Stateless: every parse() call works on a fresh graph; the parser owns no
 * persistent state across calls.
 */

#include <stdbool.h>
#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ydiagram/graph-ir.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns true if `input` looks like Mermaid (starts with `graph` or
 * `flowchart` after leading whitespace). Cheap heuristic. */
bool yetty_ydiagram_mermaid_can_parse(const char *input, size_t len);

/* Parse `input` (UTF-8, length `len`) into `out_graph`. `out_graph` must
 * already be initialized via yetty_ydiagram_graph_init; the parser appends
 * nodes/edges/clusters. */
struct yetty_ycore_void_result yetty_ydiagram_mermaid_parse(const char *input, size_t len,
                                                            struct yetty_ydiagram_graph *out_graph);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDIAGRAM_MERMAID_PARSER_H */
