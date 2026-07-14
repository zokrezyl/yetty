# ybrowser — HTML/CSS/JS rendering with the libcss cascade

`ybrowser` is a fork of [`ylexbor`](../ylexbor/README.md) that swaps the **CSS
engine**. It keeps lexbor for HTML parsing and QuickJS-NG for scripting, but runs
the CSS cascade through **libcss** (the NetSurf CSS engine) instead of lexbor's
native CSS. Everything else — the box-build → layout → paint pipeline that turns
a document into a [ydraw](../ydraw/README.md) primitive stream — mirrors
`ylexbor`.

`ylexbor` is the going-forward, fully permissive engine; `ybrowser` is the
libcss-backed variant kept alongside it for comparison and for documents whose
cascade libcss handles better.

## Relationship to ylexbor — read that README first

`ybrowser` deliberately stays a close sibling of `ylexbor`:

- **Same pipeline, same file roles** — the `ybrowser-*` files mirror the
  `ylexbor-*` files one-for-one. See [`ylexbor/README.md`](../ylexbor/README.md)
  for the box/layout/paint/JS detail; only the CSS layer differs here.
- **Symbols are intentionally kept close to `yetty_ylexbor_*`.** Most functions
  still carry the `yetty_ylexbor_` prefix so the fork stays diffable against
  `ylexbor`; only the libcss-specific code uses `yetty_ybrowser_*`. This is by
  design — do not "tidy" the prefixes into uniform `yetty_ybrowser_*`, it would
  destroy the diff against the upstream sibling.

## File layout

| File | Role |
|---|---|
| `ybrowser.c`           | Document lifecycle: create/destroy, load HTML, drive CSS load, kick off box-build/layout/paint |
| `ybrowser-libcss.c/.h` | **The divergence** — libcss integration: stylesheet load, selection context, computed-style lookup feeding the box walk |
| `ybrowser-box.c`       | DOM walk → flat box vector, reading computed style from libcss |
| `ybrowser-layout.c`    | Box vector → laid-out coordinates (block flow, inline wrap, spec flex sizing + wrap/reverse/order, grid track sizing + placement, absolute/fixed positioning) |
| `ybrowser-paint.c`     | Box vector → ydraw primitives (ysdf rects, text spans, `<img>` raster + data-URI images, inline/replaced SVG via ysvg) |
| `ybrowser-css-vars.c`  | The **libcss-gap compensation layer** (largest file in the engine): `var(--foo[, fallback])` resolver plus every pre-parse CSS rewriter and side-table scanner — see the dedicated section below |
| `ybrowser-js.c`        | QuickJS lifecycle, `<script>` walker, microtask drain |
| `ybrowser-js-dom.c`    | DOM API for the JS runtime (Element/Document, querySelector, events, …) |
| `ybrowser-js-web.c`    | Web-platform stubs (fetch/XHR, timers, navigator, location, storage, …) **and the entire network loader** — the `yetty_ybrowser_loader_*` API, curl-multi scheduler thread, share handle, HTTP cache, cookies live here, not in a separate file |
| `ybrowser-cache-disk.c`| Persistent disk tier of the RFC 9111 HTTP cache (`$XDG_CACHE_HOME/yetty`); the loader owns freshness decisions |
| `ybrowser-internal.h`  | Box-vector types, runtime struct, side-table rule structs, the `yl_size_source` provenance enum, internal API — the best single file to read |
| `ybrowser.h`           | Public header (includes a "Known gaps" block — keep it current) |

## Load pipeline (`yetty_ylexbor_load_html`)

1. **Teardown** — destroy the JS runtime first (timers/listeners hold raw
   pointers into the old DOM), clear boxes/arena/side tables, bump
   `fetch_generation` to invalidate in-flight async image jobs.
2. **Parse** — `lxb_html_document_parse` (lexbor). MediaWiki pages get an
   injected float-helper quirk sheet.
3. **Style** — fetch `<link rel=stylesheet>` + inline `<style>`; per sheet:
   side-table scanners run on the raw source, then the pre-parse rewriters,
   then `css_stylesheet_append_data` into the libcss select context. libcss
   owns the cascade; lexbor's own CSS is never initialized (perf).
4. **Script** — inline + external `<script>` synchronously through QuickJS
   unless `YBROWSER_NO_JS` / deferred; DOM mutations set `dom_dirty`.
5. **Box build** — DOM + libcss computed styles → flat box vector, applying
   the side tables (grid, transforms, aspect, keywords, opacity fold-down).
6. **Layout** — block flow, flex solver, grid, tables, abspos; every final
   size is stamped with a `yl_size_source` provenance value (surfaced by
   `--dump-boxes` as `ws=`/`hs=` — this is what the Chrome-delta tooling
   keys its mechanism clusters on).
7. **Iframes** — each `<iframe>` renders in a child engine (depth-capped).
8. **Paint** — composite stacking-key sort (shared with hit-testing), then
   ydraw primitives; images decode via optional libwebp/libpng/turbojpeg;
   inline SVG merges through ysvg. Landed images ship as per-group deltas.

## The libcss-gap compensation layer

libcss (0.9.x) predates large parts of modern CSS. The engine compensates
with two mechanisms, both in `ybrowser-css-vars.c`, and this layer is by
volume the largest part of the engine — when a CSS feature "doesn't work",
look here before suspecting the cascade:

**Pre-parse textual rewriters** (run on the stylesheet source before libcss
parses it; wired in `ybrowser-libcss.c`):
- `var()` substitution (custom properties — text substitution, not
  cascade-scoped),
- Media Queries L4 range syntax → classic min-/max- form,
- `:is()` / `:where()` / compound-`:not()` desugar to L3 selector lists,
- coarse `calc(<pct> ± <len>)` simplification,
- `flex:` shorthand expansion.

**Selector-matched side tables** (regex scanners storing per-selector rules,
re-applied at box-build using lexbor's native L4-capable matcher): grid
templates / spans / content caps, flex gaps, precise calc lengths,
width keywords (`fit-content` &c.), `aspect-ratio` (+ the
`::after{padding-bottom}` frame idiom), L4-selector `display:none`,
`-webkit-line-clamp`, class-based `transform: translate*` (translate only),
element-scoped `height: var()`.

The structural cost: two style paths that can disagree, side tables use
last-match-wins instead of real specificity, and each new CSS feature needs
another scanner. Unifying this into one cascade is tracked in the
Chrome-parity process epic, #516.

## Tool flags & env vars (testing)

`tools/ybrowser/main.c`: `--dump-boxes` (TSV box tree + `ws=`/`hs=` size
provenance — the interface all geometry oracles consume), `--dump-geo`
(dom-path keyed rects), `--dump-dom` (serialized post-JS DOM, the engine-side
analogue of Chrome's --dump-dom), `--dump-wpt` (check-layout-th
expected-vs-actual),
`--once`, `--interactive`, `--no-ui`, `--record <file>`, `-w`/`-H`,
`--font-size`, `--osc`/`--raw`.

Env: `YBROWSER_PROFILE` (stderr load-timeline profiler), `YBROWSER_NO_JS`,
`YBROWSER_NO_IFRAMES`, `YBROWSER_JS_CONSOLE`, `YBROWSER_SYNC_NAV`,
`YETTY_USER_AGENT`, `YLEXBOR_BOOT_BUDGET_MS`,
`YBROWSER_JS_BYTECODE_CACHE=0` (disable the QuickJS bytecode compile cache —
sources ≥ 16 KB are normally content-hash-keyed on the loader's disk cache so
warm loads skip parse+compile; see `ybrowser-js.c`).

## Testing

The full test/oracle stack (C contract tests, the hand WPT suite, the
whole-page Chrome-parity anchor suite with its delta comparator, render
smoke, upstream WPT) is mapped in **`test/ybrowser/README.md`** — read that
before investigating any layout divergence; the comparator's mechanism
clusters usually name the offending code path directly.

## Backends

- **HTML parsing:** lexbor (shared with `ylexbor`).
- **CSS cascade:** libcss (the distinguishing choice; `ylexbor` uses lexbor's CSS).
- **JavaScript:** QuickJS-NG (shared with `ylexbor`).

## Scope

Like `ylexbor`, this is **not** a full browser, but the layout and resource
surface is well past "static documents". What works:

- **Layout** — block flow and inline wrap; flexbox with spec flex base sizes,
  the freeze/redistribute shrink loop, per-line wrapping, `wrap-reverse` /
  `row-reverse` / `column-reverse`, `order` and `align-self`; CSS Grid with
  track sizing, span placement, growable implicit rows and default item
  stretch; `position: absolute`/`fixed` with static-position and
  containing-block resolution; left/right floats.
- **CSS** — the libcss cascade plus a `var()` resolver that honours
  element-local and ancestor inline definitions and global-rule specificity;
  `@import` is fetched and applied. Some properties libcss in this tree does
  not expand (grid templates/placement, flex gaps, a few var-driven metrics)
  are supplied by selector-matched, `@media`-gated side tables rather than the
  native cascade — see #482 for the conformance caveats.
- **Content** — text, color, backgrounds, padding, borders, raster and
  data-URI images, and inline/replaced/`<img>` **SVG** rendered as ydraw
  vectors through `ysvg` (viewBox, `preserveAspectRatio`, `currentColor`).
- **Networking** — a single loader owns the curl-multi scheduler, a
  memory+disk HTTP cache (Cache-Control/Age/Expires, ETag/`304` revalidation,
  `Vary`, request coalescing), per-resource-type request headers, cookies, and
  generation-based cancellation on navigation. JS `fetch()`/XHR run async off
  the worker pool with method/headers/body.
- **JavaScript** — QuickJS-NG with timers, microtasks, a DOM API, and enough
  of the web platform to boot mainstream SPA shells.

Still missing: real font shaping (metrics are approximated, which is the main
source of anchor-suite wrap drift); full CSS Grid conformance (intrinsic track
sizing, dense flow, named lines) and `display: flow-root`; and `<canvas>`
rendering — `getContext` returns null, tracked under the canvas epic #463.
