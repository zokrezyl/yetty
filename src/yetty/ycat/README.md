# ycat — MIME-dispatched content viewer

`ycat` detects the type of a byte buffer (libmagic + extension + content
sniffing) and dispatches to a handler that turns the bytes into a ydraw
buffer, which the caller wraps in a `YETTY_DCS_YDRAW_BIN` (600001) OSC
envelope for a running yetty's ydraw scrolling layer. Plain text / unknown
input passes through unchanged. It is the library behind the `ycat` CLI
(`tools/ycat`) and the `yless` pager (`tools/yless`).

## Detection

`yetty_ycat_detect(bytes, len, path)` layers three sources: libmagic (when
`YETTY_YCAT_HAS_LIBMAGIC`), the filename extension (also consulted when
libmagic only says `text/plain` — Markdown has no stable magic), and content
sniffers for formats with no MIME at all (Mermaid, `#ychart` data, Lottie
JSON, LilyPond, raw H.264 Annex-B). `yetty_ycat_type_from_mime` /
`_from_extension` / `_from_name` are exposed individually; the CLI's
`--card`/`--type` flag maps names through `yetty_ycat_type_from_name`
("card" is the historical wire keyword — the rendered unit is a figure).

## Handler registry

Handlers are keyed by `enum yetty_ycat_type` and self-register on first use;
`yetty_ycat_register_handler` lets callers override at runtime. Two shapes:

- **single-shot** — `yetty_ycat_handler_fn`: bytes → one owned buffer.
- **streaming** — `yetty_ycat_handler_streaming_fn`: emits one envelope at a
  time via a callback (envelope-local coordinates; the receiver scrolls by
  each envelope's scene height). Dispatch prefers streaming, falls back to
  single-shot.

| handler | type | delegates to | gate |
|---------|------|--------------|------|
| `handler-markdown.c` | streaming (one envelope by design) | [../ymarkdown/README.md](../ymarkdown/README.md) | always |
| `handler-pdf.c` | streaming, one envelope per page | [../ypdf/README.md](../ypdf/README.md) | always |
| `handler-image.c` | single-shot | [../yimage/README.md](../yimage/README.md) (stb decode) | always |
| `handler-svg.c` | single-shot | [../ysvg/README.md](../ysvg/README.md) | always |
| `handler-shadertoy.c` | single-shot | [../yshadertoy/README.md](../yshadertoy/README.md) serializer | always |
| `handler-mermaid.c` | single-shot | [../ydiagram/README.md](../ydiagram/README.md) | `YETTY_ENABLE_FEATURE_YDIAGRAM` |
| `handler-chart.c` | single-shot | [../ychart/README.md](../ychart/README.md) | `YETTY_ENABLE_FEATURE_YCHART` |
| `handler-video.c` | single-shot | [../yvideo/README.md](../yvideo/README.md) wire emitter | `YETTY_ENABLE_FEATURE_YVIDEO` |
| `handler-lottie.c` | single-shot (frame 0) | [../ylottie/README.md](../ylottie/README.md) | `YETTY_ENABLE_FEATURE_YLOTTIE` |
| `handler-music.c` | single-shot | [../ymusic/README.md](../ymusic/README.md) LilyPond engraving | `YETTY_ENABLE_FEATURE_YMUSIC` |
| `handler-circuit.c` | single-shot | [../ycircuit/README.md](../ycircuit/README.md) | `YETTY_ENABLE_FEATURE_YCIRCUIT` |

## Tree-sitter source highlighting

For source code, `ts-grammars.c` embeds 15 grammars' `highlights.scm`
queries at configure time (c, cpp, python, javascript, typescript, rust, go,
java, bash, json, yaml, toml, html, xml, markdown). `ts-highlight.c` paints
a per-byte colour map from the captures and emits one text span per
same-colour run (`yetty_ycat_ts_render`) — or 24-bit SGR text for any
terminal (`yetty_ycat_ts_emit_sgr`). `yetty_ycat_grammar_lookup` picks the
grammar from MIME or extension.

## Public API sketch

```c
#include <yetty/ycat/ycat.h>

enum yetty_ycat_type type = yetty_ycat_detect(bytes, len, path);
struct yetty_ycat_config config = {
    .cell_width = 10, .cell_height = 20, .width_cells = 80, .height_cells = 25 };
struct yetty_ydraw_drawable_list_result r =
    yetty_ycat_render(bytes, len, path, &config);      /* detect → render */
yetty_ycat_osc_bin_emit(r.value, stdout);              /* LZ4F + base64 envelope */

/* URLs: */
if (yetty_ycat_is_url(arg))
    yetty_ycat_fetch_url(arg, &bytes, &len, &content_type);  /* libcurl */
```

`osc.c` serialises the buffer (`yetty_ydraw_drawable_list_serialize`) and
hands it to yface for LZ4F compression + base64 + envelope framing.

## Layout of the module

| file | role |
|------|------|
| `ycat.c` | type registry, name mapping, dispatch (`yetty_ycat_render`) |
| `detect.c` | libmagic / extension / content-sniff detection |
| `handler-*.c` | per-type renderers (table above) |
| `osc.c` | `YDRAW_BIN` envelope writer |
| `fetch.c` | libcurl URL fetch |
| `ts-grammars.c` / `ts-highlight.c` | tree-sitter grammar table + highlighter |

Public header: `include/yetty/ycat/ycat.h`. Gated by
`YETTY_ENABLE_FEATURE_YCAT` (skipped on iOS).

## Consumers

- **tools/ycat** — the CLI: reads files/stdin/URLs, emits envelopes when
  inside a yetty terminal, passes bytes through otherwise (`--raw`, `--ts`,
  `--card <type>`).
- **tools/yless** — pager that reuses ycat's detection/rendering but ships
  the content once into a server-side scrollable yview figure instead of the
  scrollback.

## Related

- [../yface/README.md](../yface/README.md) — envelope encoding
- [../yterminal/README.md](../yterminal/README.md) — the receiving side
  (DCS codes, scrolling layer)
- [../yecho/README.md](../yecho/README.md) — sibling client-side emitter
