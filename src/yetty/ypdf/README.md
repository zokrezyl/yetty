# ypdf — PDF → ydraw buffer via pdfio

`ypdf` renders a PDF document into a ydraw buffer of text spans, SDF box
primitives, and line segments — no rasterisation. It is pure C on top of
[pdfio](https://www.msweet.org/pdfio/), FreeType (glyph metrics for advance
measurement) and optionally fontconfig (system-font substitution when a
referenced font is not embedded). Consumers are ycat's PDF handler and the
ygui `ypdf` widget.

## How it works

Two layers, one per source file:

**`pdf-content-parser.c`** is a stateful interpreter over PDF graphics/text
operators (`BT`/`ET`, `Td`/`TD`/`Tm`/`T*`, `Tj`/`TJ`/`'`/`"`, `Tc`/`Tw`/`Tz`/
`TL`/`Ts`, `re`, `m`/`l`/`h`, `S`/`f`/`B`, colour ops). It emits three event
kinds through caller callbacks sharing one `user_data`:

- `text_emit` — decoded text fragment with effective size, rotation, and a
  `yetty_ypdf_text_state` snapshot (spacing, scaling, leading, font tag,
  non-stroking fill colour). The callback returns the horizontal advance in
  text-space units so the parser can move the text matrix.
- `rect_paint` — an axis-aligned rectangle painted via `re` + `S`/`f`/`B`.
- `line_paint` — a single segment from a general path.

**`pdf-renderer.c`** drives it in two passes. Pass 1 walks pages reading
MediaBoxes only, computing max page width and accumulated height so the
buffer is created with final scene bounds. Pass 2, per page: extract embedded
TTF fonts (`FontFile2` / `FontFile3`) and register them as FONT prims, parse
the ToUnicode CMap (CID → Unicode remap for Identity-H text), then run the
content parser with callbacks that flip PDF bottom-left Y to screen top-down
Y, add text spans, and measure advances via `yetty_font_raster_font`. Boxes
become SDF Box (+ 4× Segment for the stroked case); lines become Segment.

Everything is scaled by `YETTY_YPDF_RENDER_SCALE` (1.5) — a uniform zoom
knob that enlarges the page, text included, without reflowing. Reported page
dimensions stay in unscaled PDF points.

## Streaming mode — one envelope per page

`yetty_ypdf_render_pdf_streaming` invokes a callback synchronously per page
with an envelope whose coordinates are envelope-local (page origin at y=0);
the receiver scrolls by each envelope's `scene_max_y`. Fonts are
content-addressed by FNV1a64 of the TTF bytes: the first envelope referencing
a font ships the full bytes, every later one ships a hash-only FONT prim
(`ttf_len = 0`, 16-hex name). The receiver's on-disk MSDF cache is keyed by
the same hash, so font payload crosses the wire once per document.

## Public API

```c
#include <yetty/ypdf/ypdf.h>

struct yetty_ypdf_render_result r = yetty_ypdf_render_pdf(pdf);   /* pdfio file */
/* r.value.buffer is an owned ydraw buffer; free with
 * yetty_ydraw_drawable_list_destroy. page_count / total_height /
 * max_width describe the document in PDF points. */

struct yetty_ypdf_stream_render_result s =
    yetty_ypdf_render_pdf_streaming(pdf, on_page, user_data);
```

The content parser is usable stand-alone via
`yetty_ypdf_content_parser_callbacks_content_parser_create` /
`yetty_ypdf_content_parser_parse_stream` (`pdf-content-parser.h`).

## Layout of the module

| file | role |
|------|------|
| `pdf-content-parser.c` | operator interpreter → text/rect/line callbacks |
| `pdf-renderer.c` | scene-bounds pass, font extraction/CMap, span + SDF emission, streaming envelopes |

Public headers: `include/yetty/ypdf/ypdf.h`, `pdf-content-parser.h`.
Build-gated by `YETTY_ENABLE_FEATURE_YPDF`.

## Consumers

- **ycat** — `handler-pdf.c` registers the streaming handler (spills stdin /
  URL bytes to a temp file since pdfio opens by path). See
  [../ycat/README.md](../ycat/README.md).
- **ygui** — the `ygui:ypdf` widget (`../ygui/widgets/ypdf.c`) renders a PDF
  into a `ydraw_embed`.
- **tests** — `test/ut/ypdf/` pins text positions against MuPDF's `mutool`
  output (run as a CLI ground-truth tool at authoring/test time; never
  linked). See `test/ut/ypdf/README.md`.

## Related

- [../ydraw-list/README.md](../ydraw-list/README.md) — the drawable-list
  buffer this module fills
- [../yfont/README.md](../yfont/README.md) — raster-font metrics used for
  advance measurement
- [../ymarkdown/README.md](../ymarkdown/README.md) — sibling renderer with
  the same buffer-producing shape
