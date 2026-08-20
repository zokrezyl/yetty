# ymarkdown — Markdown → ydraw buffer

`ymarkdown` renders Markdown text into a ydraw buffer of text spans and SDF
primitives. Pure C, depending only on `ycore`, `ydraw-list`, and `ysdf`.
Consumed by ycat's markdown handler, the ygui `ymarkdown` widget, and the
`yai` tool.

## Supported constructs

Block level:

- headers (`#`..`######`)
- bullet lists (`-`, `*`, `+`) and ordered lists (`1.`, `2)`)
- blockquotes (`>`, nestable) with an accent gutter bar
- fenced code blocks (``` ``` ``` or `~~~`) on a shared background panel
- horizontal rules (`---`, `***`, `___`)
- GFM tables (`| a | b |` + `|---|:--:|`) with per-column alignment and grid

Inline: bold, italic, bold+italic, inline code (tight background box),
strikethrough, and links (rendered as accent-coloured link text; the URL is
not emitted).

## Layout model

The renderer is byte-oriented: text advances `0.6 * font_size` per byte
(proportional approximation — multi-byte UTF-8 over-counts width slightly),
lines advance by `font_size * line_spacing` (default 1.4), layout starts at
`(2, 2)`. Because a text span's y is the glyph baseline, the per-line cursor
is treated as the line top and the baseline pushed down by `0.8 *
font_size`. Colours follow the brand palette (mint accent for links/quote
bars, teal table borders, off-white body text) packed as ydraw's
`0xAABBGGRR`.

## Public API

```c
#include <yetty/ymarkdown/ymarkdown.h>

struct yetty_ymarkdown_render_config config = {
    .cell_width = 10, .cell_height = 20,
    .width_cells = 80, .height_cells = 25,
};
struct yetty_ymarkdown_render_result r =
    yetty_ymarkdown_render(content, len, args, args_len, &config);
/* r.value.buffer owned by caller — yetty_ydraw_drawable_list_destroy.
 * args honours --font-size=<float> and --line-spacing=<float>. */
```

A streaming variant, `yetty_ymarkdown_render_streaming`, ships one envelope
per screen-height tile (`cell_height * height_cells` budget, whole lines
only, envelope-local y). Note that ycat's markdown handler deliberately does
**not** use it — the terminal's scrolling-canvas receiver stacks envelopes by
glyph-content rows, which mangles chunk boundaries, so `handler-markdown.c`
renders the whole document as a single envelope (see the comment there).

## Layout of the module

| file | role |
|------|------|
| `ymarkdown.c` | the whole parser + renderer (block scanner, inline scanner, table/code/quote layout, streaming tiler) |

Public header: `include/yetty/ymarkdown/ymarkdown.h`. Build-gated by
`YETTY_ENABLE_FEATURE_YMARKDOWN`.

## Status

Ported from the C++ POC card renderer, minus the Card/GPU lifecycle. Text
metrics are the flat 0.6-advance approximation (no font shaping); constructs
outside the list above render as plain paragraph text.

## Consumers

- **ycat** — `handler-markdown.c` (see [../ycat/README.md](../ycat/README.md))
- **ygui** — the `ygui:ymarkdown` widget (`../ygui/widgets/ymarkdown.c`)
  re-renders on rect change into a `ydraw_embed`
- **yai** — the yai tool's `render.c` frames the output as a `YDRAW_BIN`
  OSC envelope
- **tests** — `test/ut/ymarkdown/ymarkdown-smoke-test.c`

## Related

- [../ydraw-list/README.md](../ydraw-list/README.md) — drawable-list buffer
- [../ychart/README.md](../ychart/README.md) /
  [../ydiagram/README.md](../ydiagram/README.md) — sibling text-to-buffer
  renderers
