# yecho — text + glyphs + styled blocks → ydraw buffer

`yecho` parses a small inline markup grammar — plain text, `@name` shader
glyphs, and `{attrs: content}` styled blocks — and renders it into a
ydraw buffer. It is strictly client-side (never linked into the yetty
process); the `tools/yecho` CLI is its frontend, emitting the same
`YETTY_DCS_YDRAW_BIN` envelope ycat uses.

## Grammar

```
@name                     shader glyph by name (yfont shader-glyph registry)
{attrs: content}          styled text block
{plot; w=400; h=200: f=sin(x); g=cos(x)}       yplot figure
{video; src=clip.h264; w=640; h=360: }         yvideo figure (feature-gated)
\{ \} \@ \\               escaped literals
```

Styled-block attributes (semicolon-separated `key=value`): `color=#RRGGBB`,
`bg=#RRGGBB` (SDF box behind the run), `style=bold|italic|underline`
(recorded on the span but not yet rendered — no font-style mapping on the
wire), `font-size=N`. Plot blocks take `w`/`h`, `xrange=lo..hi`,
`yrange=lo..hi`, `nogrid|noaxes|nolabels`; their content is yexpr plot
syntax compiled to yfsvm bytecode and serialised as a yplot complex prim.
Blocks are not recursive — content is plain text.

Glyph names resolve through `yetty_yfont_shader_glyph_codepoint()` (PUA
range `U+100000 + local_id`); unknown names land in the document's error
list and render as a literal `[?name]` placeholder.

## Parse / render split

`yetty_yecho_parse` walks the input character by character into an opaque
`yetty_yecho_doc` of spans (TEXT / GLYPH / BLOCK) plus a diagnostics list —
inspectable via `yetty_yecho_doc_span_count` / `_span` / `_error`. The
renderer lays spans out left-to-right from a `(2, 2)` origin, advancing
`~0.6 * font_size` per codepoint (same proportional approximation as
ymarkdown) and `font_size * line_spacing` per newline.

## Public API sketch

```c
#include <yetty/yecho/yecho.h>

struct yetty_yecho_render_config config = {
    .cell_width = 10, .cell_height = 20, .width_cells = 80,
    .font_size = 0 /* → cell_height */, .line_spacing = 0 /* → 1.2 */ };
struct yetty_ydraw_drawable_list_result r =
    yetty_yecho_render_string(input, len, &config);   /* parse + render */
yetty_yecho_dcs_bin_emit(r.value, stdout);
yetty_ydraw_drawable_list_destroy(r.value);
```

## Layout of the module

| file | role |
|------|------|
| `yecho.c` | the whole parser + renderer (spans, attrs, glyph resolution, plot/video complex emission, DCS emit) |

Public header: `include/yetty/yecho/yecho.h`. Deps: `ycore`, `ydraw-core`,
`ysdf`, `yfont_shader_glyph`, `yface`, `yplot_core` (pulls yfsvm + yexpr;
no GPU), optionally `yvideo_core`. Gated by `YETTY_ENABLE_FEATURE_YECHO`.

## Consumers

- **tools/yecho** — the CLI. Always renders the input and emits the DCS
  envelope (a yetty renders it; every other terminal discards it — it does
  not probe TERM_PROGRAM). `--list-glyphs` dumps the shader-glyph registry.

## Related

- [../ycat/README.md](../ycat/README.md) — sibling emitter with the same
  wire format
- [../yfont/README.md](../yfont/README.md) — shader-glyph registry
- [../yplot/README.md](../yplot/README.md) — the plot complex embedded by
  `{plot: …}` blocks
