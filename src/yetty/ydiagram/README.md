# ydiagram — Mermaid text → ydraw buffer

`ydiagram` parses Mermaid diagram source into a graph IR, lays it out with a
Sugiyama layered layout, and renders SDF shapes plus MSDF text spans into a
ydraw drawable list — the same way [`ychart`](../ychart/README.md) turns
tabular data into one. Pure C; depends only on `ycore`,
[`ydraw-list`](../ydraw-list/README.md), and [`ysdf`](../ysdf/README.md).

## Diagram families

`yetty_ydiagram_detect` sniffs the first non-blank, non-comment line's
keyword and dispatches:

| header keyword | parser | path |
|---|---|---|
| `graph` / `flowchart` (TD/TB/BT/LR/RL) | `mermaid-parser.c` | graph IR → layout → renderer |
| `stateDiagram` / `stateDiagram-v2` | `state-parser.c` | graph IR → layout → renderer |
| `classDiagram` | `class-parser.c` (record nodes + UML arrows) | graph IR → layout → renderer |
| `erDiagram` | `er-parser.c` (record nodes + crow's-foot terminals) | graph IR → layout → renderer |
| `sequenceDiagram` | `sequence.c` | own temporal layout, renders straight to a buffer |

Flowchart syntax covers the common node shapes (`A[rect]`, `A(rounded)`,
`A((circle))`, `A{diamond}`, `A{{hexagon}}`, `A[(cylinder)]`, `A([stadium])`,
`A[/parallelogram/]`), edge styles (`-->`, `---`, `-.->`, `==>`, edge
labels), `subgraph … end` clusters, and node styling: `classDef` (including
`default`), `class`, `style`, and the `A[x]:::name` shorthand map `fill` /
`stroke` / `color` / `stroke-width` onto node styles (hex colors or common
CSS names; directives may appear before or after the nodes they reference).
`linkStyle` is consumed but not yet mapped onto edge styles. Sequence
diagrams support
participants/actors, solid/dashed and open/filled arrows, self-messages, and
notes; `loop/alt/opt/par/else/end` blocks are accepted (inner messages
render, frames are future work).

## Layout (`sugiyama-layout.c`)

Seven phases: cycle removal (reverse back-edges) → layer assignment (longest
path) → dummy nodes for long edges → crossing reduction (barycenter,
alternating sweeps) → positioning → edge routing (straight lines) →
direction transform (TB/BT/LR/RL). Node sizes come from a caller-supplied
text-measure callback; without one, a crude `0.6 * font_size * char_count`
fallback is used.

## Public API (`include/yetty/ydiagram/`)

One-shot:

```c
struct yetty_ydiagram_buffer_result buffer_res =
    yetty_ydiagram_render_mermaid(input, len);
/* or, with layout/render options + a real text measurer: */
buffer_res = yetty_ydiagram_render_mermaid_full(input, len, &layout_params,
                                                &render_options, measure_fn,
                                                measure_userdata);
/* buffer_res.value is owned; free with yetty_ydraw_drawable_list_destroy */
```

Or compose the layers: `yetty_ydiagram_graph_init` + a parser (`graph-ir.h`,
`mermaid-parser.h`, `diagrams.h`), then `yetty_ydiagram_layout`
(`layout.h`), then `yetty_ydiagram_render` (`renderer.h`).
`render_options.clear_canvas` picks between "full redraw, replace the pane"
(prepends `CMD_ZERO`; default — what a re-rendering widget wants) and
cat-like inline flow at the cursor (what ycat wants).

## File map

| file | role |
|------|------|
| `graph-ir.c` | IR lifecycle: nodes, edges, clusters, styles (dynamic arrays) |
| `diagram-detect.c` | header-keyword sniff → family dispatch |
| `mermaid-parser.c` | flowchart/graph syntax → IR |
| `state-parser.c` / `class-parser.c` / `er-parser.c` | other families → IR |
| `sequence.c` | sequenceDiagram: own layout, straight to buffer |
| `sugiyama-layout.c` | layered layout, all seven phases |
| `renderer.c` | laid-out IR → SDF shapes, arrowheads, dashes, MSDF labels |
| `ydiagram.c` | high-level glue (detect → parse → layout → render) |

## Consumers

- **CLI** — `tools/ydiagram` emits a DCS `YDRAW_BIN` envelope (or a raw
  buffer with `-o`); it measures text with a real MSDF font so boxes fit.
- [`ycat`](../ycat/README.md) — `.mmd`/Mermaid content detected in
  `detect.c`, rendered by `handler-mermaid.c`.
- [`ygui`](../ygui/README.md) — the `widgets/ydiagram.c` figure widget.
- `tools/ygreeter`, `tools/yhello`; smoke test in `test/ut/ydiagram/`.

## Status

Mermaid-only today: the headers mention DOT/Graphviz as a later input, but
no DOT parser exists. Sequence-diagram block frames (`loop`/`alt` boxes) are
not drawn yet.
