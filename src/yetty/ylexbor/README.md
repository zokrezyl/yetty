# ylexbor — HTML/CSS/JS rendering on top of lexbor

`ylexbor` is yetty's permissively-licensed Web rendering layer. It takes HTML
(plus optional CSS and JavaScript) and produces a [ydraw] primitive stream
that yetty's GPU-side painter consumes. Unlike `ynetsurf` (which links the
GPL'd NetSurf), `ylexbor` uses only Apache-2.0 [lexbor] for parsing/cascade
and MIT [QuickJS-NG] for scripting, so it can be linked directly into the
main yetty binary without license-conflict gymnastics.

The module is ~8,000 lines of C. It is **not** a full browser — there is no
real font shaping, no flexbox solver beyond an even split, no float / grid /
position, and no SVG raster. What it does is enough to render Wikipedia,
news.google.com, and most static documentation sites with text, colors,
backgrounds, padding, borders, centering, images, and a usable JavaScript
runtime that boots single-page-app shells.

## File layout

| File | Role |
|---|---|
| `ylexbor.c`            | Document lifecycle: create/destroy, load HTML, drive CSS load, kick off box-build/layout/paint, set base URL |
| `ylexbor-box.c`        | DOM walk → flat box vector. Reads computed style, resolves CSS lengths/colors/keywords, decides display/flex/none |
| `ylexbor-layout.c`     | Box vector → laid-out coordinates. Block-flow vertical stacking, inline text wrapping, flex-row even split |
| `ylexbor-paint.c`      | Box vector → ydraw primitives. ysdf rects for backgrounds + borders, text spans for inline runs, yimage prims for `<img>` |
| `ylexbor-css-vars.c`   | CSS custom-property scanner + `var(--foo[, fallback])` resolver — see [CSS extras](#css-extras-vs-lexbor) below |
| `ylexbor-js.c`         | QuickJS lifecycle, `<script>` walker, exception reporting, drain microtasks |
| `ylexbor-js-dom.c`     | DOM API for the JS runtime: Element/Document/CharacterData wrappers, querySelector, addEventListener, classList, attributes, innerHTML, etc. |
| `ylexbor-js-web.c`     | Web platform stubs: `fetch`/XHR (libcurl), timers, `data:` URIs, navigator, location, history, Storage, Event constructors |
| `ylexbor-internal.h`   | Box-vector types, runtime struct, internal API |

## Pipeline

```
              load_html(html_bytes)
                       │
                       ▼
   ┌──────────────────────────────────────────┐
   │  lexbor: parse HTML → DOM tree           │
   │          parse <style> + external CSS    │
   │          cascade rules onto elements     │
   └──────────────────────────────────────────┘
                       │
                       ▼
   ┌──────────────────────────────────────────┐
   │  ylexbor-css-vars: scan custom props     │
   │                    pre-resolve var(...)  │
   └──────────────────────────────────────────┘
                       │
                       ▼
   ┌──────────────────────────────────────────┐
   │  QuickJS: run inline + external scripts  │
   │          DOM API mutates lexbor tree     │
   │          fetch/setTimeout/Promises pump  │
   └──────────────────────────────────────────┘
                       │
                       ▼
   ┌──────────────────────────────────────────┐
   │  ylexbor-box.c: DOM walk → box vector    │
   │       reads display/width/margin/padding │
   │       /border/text-align/color/bg/font   │
   └──────────────────────────────────────────┘
                       │
                       ▼
   ┌──────────────────────────────────────────┐
   │  ylexbor-layout.c: place boxes           │
   │       block stacking, inline wrap, flex  │
   └──────────────────────────────────────────┘
                       │
                       ▼
   ┌──────────────────────────────────────────┐
   │  ylexbor-paint.c: emit ydraw primitives │
   │       ysdf box  ← bg, borders            │
   │       text span ← inline text            │
   │       yimage    ← <img>                  │
   └──────────────────────────────────────────┘
                       │
                       ▼
              yetty_ydraw_core_buffer
                  (ready for GPU)
```

## CSS extras (vs. lexbor)

Lexbor parses CSS and applies the cascade — but the API only exposes the
*serialized declaration list* (`lxb_dom_element_style_serialize_str`).
ylexbor sits on top and:

### 1. CSS custom properties + `var()`

Lexbor's cascade attaches custom-property *identifiers* but doesn't evaluate
`var(--text)` references. We pre-scan every stylesheet for declarations that
look like `--name: value;`, store them in a global table, and resolve
`var(--name [, fallback])` recursively (depth-32 cycle guard) before parsing
colors/lengths in the box pass. Without this, every site that uses CSS
variables (which is all of them) renders with empty/default colors.

Source: `ylexbor-css-vars.c::yetty_ylexbor_css_vars_{scan,resolve}`.

### 2. Per-property readers

`find_inline_decl()` linearly scans a serialized declaration list for one
property. Built on top:

| Helper | Purpose |
|---|---|
| `parse_css_length(s, len, font_size, pct_basis, *out_px)` | px / em / rem / `<num>%` (against `pct_basis`) |
| `parse_css_color(s, len, *out)` | `#rgb` / `#rrggbb` / `#rrggbbaa` / `rgb()` / `rgba()` / named colors / `transparent` |
| `read_inline_color(r, style, len, key, klen, *out)` | reads + `var()`-resolves a color property |
| `read_inline_length(style, len, key, klen, font_size, pct_basis, *out_px)` | reads a length property |
| `read_inline_keyword(style, len, key, klen, choices)` | matches an enum-like keyword (`auto`, `center`, …) |
| `read_inline_box_lengths(style, len, prop, plen, font_size, pct_basis, *t, *r, *b, *l)` | shorthand `<prop>: A B? C? D?` + per-side overrides |

### 3. Properties consumed at box-build

| Property | Effect on box |
|---|---|
| `display: none` / `hidden` attr | element + descendants skipped |
| `display: flex; flex-direction: row\|column` | parent's `layout_mode` |
| `width` / `max-width` / `min-width` | clamps `child_w` in layout |
| `height` | pins `c->h` (overrides content-derived) |
| `margin: T R B L` (incl. `auto`) | per-side, with `_auto` flags for centering |
| `padding: T R B L` | content-area inset |
| `border-{top,right,bottom,left}-width` (longhand) | per-side rect at paint |
| `border: <w> <style> <color>` (shorthand) | tokenized; first numeric = width, last colour token = colour |
| `border-radius` | corner radius on the bg ysdf box |
| `border-color` / per-side colours | falls back to `currentColor` |
| `color` | fg, inherited |
| `background-color` / `background` | bg fill |
| `text-align: left\|center\|right\|justify` | shifts each laid-out line within content area; inherited |

`display:none` checks computed style **and** the `hidden` HTML attribute
**and** inline `style=`, so dropdowns / aria-hidden menus get culled.

### 4. Anything we don't model

The following are parsed-but-ignored: `position` (all values), `float`,
`flex-grow`/`flex-basis`/`flex-wrap`, `grid-*`, `transform`, `transition`,
`box-shadow`, `font-family` (we have one font), `letter-spacing`,
`word-spacing`, pseudo-elements with content. Lexbor's cascade still
returns them; we just don't consume them yet. None of this is a lexbor
limitation — the bottleneck is in our box/layout passes.

## JavaScript

`ylexbor-js.c` initialises a QuickJS-NG runtime per document, walks the
DOM for `<script>` blocks (inline + `src=`), and evaluates each one in
the global scope. Microtasks drain after every eval; setTimeout/setInterval
deadlines are pumped externally via `yetty_ylexbor_pump_timers()` until the
boot budget expires.

### DOM surface (`ylexbor-js-dom.c`)

Implemented enough to boot SPA shells. Highlights:

- **Element** — `id`, `className`, `tagName`, `localName`, `nodeName`,
  `nodeType`, `nodeValue`, `parentNode`, `parentElement`, `firstChild`,
  `lastChild`, `nextSibling`, `previousSibling`, `children`, `childNodes`,
  `ownerDocument`, IDL `src`/`href`/`name`/`value`/etc. with URL resolution
- **Element methods** — `getAttribute`/`setAttribute`/`removeAttribute`/
  `hasAttribute`/`toggleAttribute` (with XML name validation +
  HTML-doc lowercasing), `querySelector(All)`, `getElementsByTagName/
  ClassName/Name`, `closest`, `matches`, `contains`, `appendChild`,
  `removeChild`, `insertBefore`, `replaceChild`, `before`/`after`/
  `prepend`/`append`/`replaceWith`/`remove` (with cycle guards + auto
  detach-from-old-parent), `innerHTML` (get + set), `outerHTML`,
  `textContent`, `cloneNode(deep)` (lexbor native), `addEventListener`/
  `removeEventListener`/`dispatchEvent`
- **classList** (DOMTokenList) — `add`/`remove`/`toggle`/`replace`/
  `contains`/`item`/`supports`/`length`/`value` with empty-string and
  whitespace-token validation
- **CharacterData** (Text/Comment/PI) — `data` (get + set), `length`,
  `appendData`/`insertData`/`deleteData`/`replaceData`/`substringData`
  with bounds checking. **Workaround**: lexbor's
  `lxb_dom_character_data_replace` ignores its `offset`/`count` args
  (just blits), so `chardata_splice` does the offset math manually
  before calling lexbor's full-overwrite primitive.
- **Document** — `getElementById`, `createElement(NS)` (with XML QName
  validation + namespace consistency: prefix-without-NS → NamespaceError,
  `xml`/`xmlns` mismatch detected), `createTextNode`, `createComment`,
  `createDocumentFragment`, `documentElement`, `body`, `head`, `title`,
  `URL`/`location`/`domain`/`referrer`/`characterSet`/`compatMode`/
  `defaultView`, `implementation.{hasFeature, createDocument,
  createDocumentType, createHTMLDocument}`
- **Wrapper identity cache** — `wrap_cache_lookup`/`_insert` keys
  JSValue wrappers by `lxb_dom_node_t*` so `el.parentNode === parent`,
  `node.ownerDocument === doc`, etc. round-trip correctly. Without this,
  every property access mints a fresh wrapper and `===` comparisons
  always fail.

### Web platform (`ylexbor-js-web.c`)

- `fetch()` / `XMLHttpRequest` — synchronous libcurl behind an
  immediately-resolved Promise (good enough for boot paths that
  `await fetch(...)` once)
- `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval`,
  `queueMicrotask`, `requestAnimationFrame` (deferred 16ms)
- `data:` URI decoder (base64 + URL-encoded)
- Standard globals stubbed: `window`/`self`/`top`/`parent`/`frames`,
  `navigator`, `location`, `history`, `localStorage`, `sessionStorage`,
  `crypto.randomUUID`, `console.log/warn/error/info/debug`,
  `MutationObserver` (no-op), `IntersectionObserver` (no-op),
  `ResizeObserver` (no-op), `PerformanceObserver` (no-op)
- Event constructors: `Event`, `UIEvent`, `MouseEvent`, `PointerEvent`,
  `WheelEvent`, `KeyboardEvent`, `TouchEvent`, `CustomEvent`,
  `MessageEvent`, `BeforeUnloadEvent`, `FocusEvent`, `InputEvent`,
  `DragEvent`, `MessageChannel`, `EventTarget`, `Headers`, `Request`,
  `Response`, `Blob`, `File`, `DOMException`, `DOMParser`,
  `TextEncoder`/`Decoder`, ~60 `HTMLxxxElement` constructor stubs

### What's intentionally absent

No iframes (no `iframe.contentDocument`), no `<canvas>` rendering, no
WebGL/WebGPU, no Worker/SharedWorker, no IndexedDB, no Service Worker,
no full prototype chain (`obj instanceof HTMLDivElement` returns false
even for divs — every Element shares one JS class).

## Integration with yetty ydraw

The output of the pipeline is a `yetty_ydraw_core_buffer*` populated by
`yetty_ylexbor_paint`. Per-box mapping:

| Box kind | ydraw emission |
|---|---|
| `YL_BOX_BLOCK` with `bg.a > 0`     | `yetty_ysdf_add_box(buf, z, bg, 0, 0, &box)` with `corner_radius = border_radius` |
| `YL_BOX_BLOCK` with any border > 0 | up to 4 thin `ysdf_box` rects (top / right / bottom / left) of `border_color` |
| `YL_BOX_INLINE_TEXT`               | `yetty_ydraw_core_buffer_add_text(buf, x, baseline_y, &txt, font_size, fg, z, font_id, rotation)` |
| `YL_BOX_INLINE_IMAGE`              | `yetty_yimage_uniforms_serialize` + `yetty_ydraw_core_buffer_add_prim(buf, prim_buf, len)` (or grey ysdf placeholder if decode failed) |

The paint pass tracks the maximum `(x+w, y+h)` of every emitted prim and
calls `yetty_ydraw_core_buffer_set_scene_bounds` before returning.
Without this, the GPU rasterizer culls every prim that falls outside the
default `(0,0)–(0,0)` scene rectangle — a "no image displayed" symptom we
hit on every page until it was fixed.

### Image cache

Decoded image pixels live in a per-document hash (open-addressed,
4096 buckets) keyed by absolute URL. `yetty_ylexbor_img_cache_get_or_load`:
1. Hit → return cached entry
2. Miss → fetch via `yetty_ylexbor_http_get_referer` (file:// /
   data: URI / http(s):// via libcurl with browser UA + Referer +
   Sec-Fetch-Dest: image)
3. Sniff format from magic bytes (PNG signature, JPEG SOI, GIF, RIFF/WEBP,
   `<svg`/`<?xml`)
4. Dispatch: **libpng** for PNG, **libjpeg-turbo** for JPEG, **stb_image**
   for everything else (GIF/BMP/HDR/etc.), SVG-XML to a sized
   checker-pattern placeholder
5. Cache RGBA8 pixels + dimensions

The same decoded buffer is re-serialized into a fresh yimage prim on every
paint call (ydraw takes ownership of the prim bytes via `add_prim`).

### URL picking

`yetty_ylexbor_img_pick_url` walks `<img>` attributes in this priority
order to handle modern lazy-loading patterns:

```
1. data-src / data-original / data-lazy-src       (lazy-load)
2. first URL of srcset (whitespace-terminated)    (responsive)
3. plain src                                      (default)
```

Plus a placeholder check: if `src` is a `data:` URI shorter than 200
bytes (almost always a 1×1 placeholder) and a `data-*` attribute is
present, the lazy attr wins. The `srcset` parser **must not** split on
`,` because real-world URLs contain commas (Google's `gstatic.com/
faviconV2?...&fallback_opts=TYPE,SIZE,URL`).

## Logging

All diagnostic output goes through `ydebug` from `<yetty/ytrace/ytrace.h>`.
Each call site is a registered trace point:

```
[12:35:22.729] [debug] ylexbor-paint.c:565 (yetty_ylexbor_paint): paint total boxes=12
[12:35:22.729] [debug] ylexbor-paint.c:583 (yetty_ylexbor_paint): paint block i=3 xy=240,32 wh=800x239 bg=f4f4f4ff
```

- Default: trace points are off → `ydebug` is a runtime no-op
- `YTRACE_DEFAULT_ON=yes` — enable everything globally
- `yinfo` build (`-DYTRACE_C_ENABLE_DEBUG=0`) — `ydebug` compiles to
  `((void)0)`, zero size and zero runtime cost

## Build dependencies

| Lib | Why |
|---|---|
| **lexbor** (Apache-2.0) | HTML/CSS parser + cascade |
| **QuickJS-NG** (MIT)    | optional; `<script>` execution. When absent, ylexbor-js* compile to no-op stubs |
| **libcurl** (MIT)       | optional; `fetch()`/XHR/external `<script src=>` |
| **libpng** (libpng)     | optional; PNG decode (libpng's simplified `png_image_*` API). Falls back to stb_image's PNG decoder |
| **libjpeg-turbo** (BSD) | optional; JPEG decode (`tjDecompress2`). Falls back to stb_image |
| **stb_image** (MIT)     | always; GIF/BMP/HDR/PSD plus the libpng/libjpeg fallback path |

`yetty_ydraw_core` and `yetty_ysdf` are local yetty libs (the GPU side).

## What was added on top of lexbor

Lexbor handles HTML parsing, CSS parsing, and selector matching very
well. What we added (and what could in principle live in lexbor):

### Likely candidates for upstream

These are **bug-fixes / spec compliance** improvements that are useful
to any lexbor user:

1. **`lxb_dom_character_data_replace` should honour `offset` and `count`.**
   Right now it ignores them and just overwrites `data.data[0..len]`,
   which violates the DOM spec for `appendData`/`insertData`/`deleteData`/
   `replaceData`. We work around it in `chardata_splice` by computing the
   final string ourselves and calling `replace` with `offset=count=0`.
   *Upstream fix*: implement spec-correct splicing in
   `source/lexbor/dom/interfaces/character_data.c`.

2. **`var(--name [, fallback])` resolution.** Lexbor parses the value
   token but never substitutes. Our `ylexbor-css-vars.c` is ~280 lines
   and could be ported to live alongside lexbor's CSS module. The
   tricky part is that custom-property *values* aren't surfaced through
   lexbor's current API — making the cascade emit resolved values would
   need API additions, not just internal changes.

3. **Border shorthand → longhand expansion.** Lexbor preserves the raw
   `border: 2px solid #336` in computed-style serialization rather than
   expanding to `border-top-width: 2px; border-top-style: solid;
   border-top-color: #336; …`. CSS 2.1 §8.5.4 specifies the expansion;
   we tokenize it ourselves in `ylexbor-box.c`. Upstreaming would mean
   the cascade emits 12 longhand declarations per `border:` shorthand.

4. **`<img>` cycle guard in `lxb_dom_node_insert_*`.** Calling
   `lxb_dom_node_insert_child(parent, child)` when `child` is `parent`
   or one of its ancestors creates an infinite loop in the next tree
   walk. Spec says `HierarchyRequestError`. We pre-check in
   `js_el_appendChild`; lexbor could add the check in `dom/interfaces/
   node.c::lxb_dom_node_insert_*`.

5. **Pre-detach on insert.** Lexbor's `insert_child` does not unlink
   the child from its old parent first, leaving stale next/prev sibling
   pointers and corrupting the old parent's chain. Our
   `js_el_appendChild` calls `lxb_dom_node_remove(child)` first.
   Per-spec, that's part of the Insert algorithm (step 1.3).

### Likely *not* useful upstream

These are yetty-specific glue — they don't fit lexbor's "parser only"
charter:

- The flat box vector (`yetty_ylexbor_box_vec`), block-flow layout,
  inline text wrapping, flex-row split — that's a layout engine, and
  lexbor explicitly stops at the cascade
- ydraw emission — lexbor has no notion of GPU buffers
- The QuickJS DOM bindings — lexbor doesn't take a position on JS
  engines, and tying it to QuickJS would split the user base
- The image cache, lazy-load attribute picker, browser-shaped fetch —
  application-level concerns

A possible structure: lexbor stays as it is (HTML+CSS+selectors), and a
small layout-engine companion library could grow alongside, taking
lexbor as an input. ylexbor's `box.c` + `layout.c` are a 1.5 KLOC
starting point.

## Status

| Capability | State |
|---|---|
| HTML parsing                                        | lexbor |
| CSS rule matching + cascade                         | lexbor |
| CSS custom properties / `var()`                     | this module |
| Box generation + block flow                         | this module |
| Inline text wrapping at word boundaries             | this module |
| Flex-row even split                                 | this module |
| `display: none`, `max-width`, `margin: auto`, text-align, border, padding | this module |
| Image decode (PNG/JPEG/GIF/BMP/SVG-placeholder)     | this module |
| JavaScript (QuickJS-NG): DOM, fetch, timers         | this module |
| Wrapper-identity-correct `===` checks               | this module |
| Floats / position / grid / shadow / transforms      | not implemented |
| Real flex (grow/basis/wrap/justify-content)         | not implemented |
| Iframes / cross-document JS / Worker                | not implemented |
| Real font shaping (FreeType/HarfBuzz)               | not implemented (uniform glyph width) |
| WPT pass rate (`dom/nodes`)                         | 33% — see `test/integration/ylexbor/` |

[lexbor]: https://github.com/lexbor/lexbor
[QuickJS-NG]: https://github.com/quickjs-ng/quickjs
[ydraw]: ../ydraw-core/
