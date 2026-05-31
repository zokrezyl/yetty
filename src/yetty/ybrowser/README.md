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
| `ybrowser-layout.c`    | Box vector → laid-out coordinates (block flow, inline wrap, flex-row even split) |
| `ybrowser-paint.c`     | Box vector → ydraw primitives (ysdf rects, text spans, `<img>` images) |
| `ybrowser-css-vars.c`  | CSS custom-property scanner + `var(--foo[, fallback])` resolver |
| `ybrowser-js.c`        | QuickJS lifecycle, `<script>` walker, microtask drain |
| `ybrowser-js-dom.c`    | DOM API for the JS runtime (Element/Document, querySelector, events, …) |
| `ybrowser-js-web.c`    | Web-platform stubs (fetch/XHR, timers, navigator, location, storage, …) |
| `ybrowser-internal.h`  | Box-vector types, runtime struct, internal API |
| `ybrowser.h`           | Public header |

## Backends

- **HTML parsing:** lexbor (shared with `ylexbor`).
- **CSS cascade:** libcss (the distinguishing choice; `ylexbor` uses lexbor's CSS).
- **JavaScript:** QuickJS-NG (shared with `ylexbor`).

## Scope

Like `ylexbor`, this is **not** a full browser — no real font shaping, no
float/grid/position, no SVG raster — but enough to render mainstream static
documents with text, color, backgrounds, padding, borders, images, and a
JavaScript runtime that boots simple SPA shells.
