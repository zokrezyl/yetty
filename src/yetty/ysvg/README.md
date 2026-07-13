# ysvg — SVG (Tiny 1.2) renderer → ydraw primitives

`ysvg` renders a static SVG document into a ydraw drawable list of SDF
shapes, MSDF text spans, and embedded [`yimage`](../yimage/README.md)
records — vectors stay vectors end to end, no rasterization. Scope is the
SVG Tiny 1.2 static-graphics subset: no scripting, no SMIL animation, no
DOM/timing model. Dependencies: yxml (SAX XML), `ycore`,
[`ydraw-core`](../ydraw-core/README.md), [`ysdf`](../ysdf/README.md), and
the GPU-less `yetty_yimage_core` for `<image>`.

## Pipeline

1. **Parse** (`ysvg-parse.c`) — yxml SAX events → a `yetty_ysvg_doc` node
   tree; one root doc owns every node, attribute string, and path segment.
2. **Cascade** (`ysvg-style.c` + `ysvg-css.c`) — resolved style per element,
   ascending priority: inherited parent → presentation attributes → author
   CSS from `<style>` (specificity-ordered) → inline `style="…"`, with
   `!important` re-applied last.
3. **Geometry** (`ysvg-attrs.c`, `ysvg-path.c`) — attribute mini-languages
   (lengths, colors, transforms, `viewBox`, `points`) and the path `d`
   flattener: all commands (`M L H V C S Q T A Z`, absolute + relative),
   adaptive Bézier subdivision, arcs via cubic conversion.
4. **Paint** (`ysvg-paint.c`, `ysvg-fill.c`) — walks the tree composing the
   transform stack and emits primitives; geometry is transformed at emit
   time since SDF primitives carry shape parameters, not a transform.

Element mapping: `rect` → SDF box/rounded box (4 segments under
rotation/skew), `circle`/`ellipse` → SDF circle/ellipse, `line` → segment,
`polyline`/`polygon`/`path` → flattened segments, filled shapes →
ear-clipping triangulation into SDF triangles (`ysvg-fill.c`; holes fill
solid, capped at 4096 vertices), `text`/`tspan` → MSDF text entries,
`g`/`a`/`svg`/`use`/`switch` → recurse (`<use>` depth-capped). Gradients are
approximated by the mean stop colour (following `xlink:href` stop
inheritance); `<image>` is served by an embedder-supplied resolver and
serialized as a yimage composite record.

CSS selector support: descendant/child/adjacent/general-sibling combinators,
attribute selectors, structural pseudo-classes, `!important`. Ignored:
`@media` bodies, pseudo-elements, `:nth-*` expressions; dynamic
pseudo-classes parse but never match.

## Public API (`include/yetty/ysvg/ysvg.h`)

```c
struct yetty_ysvg_render_config config = {
    .cell_width = 10, .cell_height = 20,      /* default font-size seed */
    .width_cells = 80, .height_cells = 25,    /* scene-bounds fallback */
    .image_resolver = {resolve_fn, userdata}, /* NULL resolve skips <image> */
    .collect_link_regions = 1,                /* record <a> click rects */
};
struct yetty_ysvg_render_result render_res =
    yetty_ysvg_render(content, content_len, args, args_len, &config);
/* render_res.value.buffer  — owned drawable list
 * render_res.value.scene_width/height
 * render_res.value.links[] — owned; free with yetty_ysvg_links_free */
```

`args` is a ymarkdown-style flag string: `--font-size=<f>`,
`--line-spacing=<f>`, `--bg=#RRGGBB[AA]`. Scene bounds come from `viewBox`,
else `width`/`height`, else the config's cell dimensions.

## File map

| file | role |
|------|------|
| `ysvg.c` | public entry: args, scene bounds, background, pipeline glue |
| `ysvg-parse.c` | yxml SAX → node tree |
| `ysvg-attrs.c` | attribute mini-language parsers (slices, no NUL needed) |
| `ysvg-style.c` | cascaded style resolution, paint-string parser |
| `ysvg-css.c` | `<style>` CSS: tokenizer, selector engine, specificity |
| `ysvg-path.c` | path `d` / `points` flattener → subpath polylines |
| `ysvg-fill.c` | ear-clipping triangulation for filled polygons |
| `ysvg-paint.c` | tree walk → SDF/MSDF/yimage emission, `<use>`, gradients, links |
| `ysvg-internal.h` | element/enum/doc/node/style types shared by the TUs |

## Consumers

- [`ycat`](../ycat/README.md) — `handler-svg.c` renders `.svg` files inline.
- [`ybrowser`](../ybrowser/README.md) — SVG images render once into a
  drawable list, then merge transformed into the page scene
  (`ybrowser-paint.c`); the browser supplies the `<image>` resolver and
  consumes the `<a>` link regions.
- Unit tests in `test/ut/ysvg/svg-test.c`.
- [`ylottie`](../ylottie/README.md) mirrors ysvg's node-tree and transform
  conventions (no code dependency).
