# ygui2 — the drawable-contract widget toolkit

ygui2 is a widget toolkit that renders **exclusively through the yvterm
drawable contract** (`src/yetty/yvterm/drawable-use-cases.md`): the widget
tree projects onto the wire group tree, every frame is a DCS drawable
envelope, and steady-state updates are incremental — offsets move widgets,
addressed reopens repaint them, complexes stream. There is no figure
container, no RPC session, and no GPU dependency on the producer side; a
ygui2 app is a plain PTY client. The design rationale and the full plan
live in [strategy.md](strategy.md).

## Module layout

| File | Role |
|---|---|
| `widget.c` | Base class `ygui2:widget` — tree links, wire node id, rect, flex layout params, absolute placement, scroll offset, clip flag, dirty classes, visibility/focusable/dismiss flags, paint/input virtuals |
| `framework.c` | `ygui2:framework` — widget instantiation, viewport + theme, flex layout pass, the emit pipeline (insert / offsets / incremental / stream), envelope shipping, input parsing (OSC client-input + CSI keys), hit-testing, focus, capture, overlay |
| `widgets/*.c` | The widget catalog (below) |
| `api-stub.c` | Permanent empty TU so `yetty_api_ygui2` exists before the first codegen |

The annotated `.c` files are the only hand-written layer; public headers
(`include/yetty/api/ygui2/…`) and glue (`src/yetty/gen/{impl,api}/ygui2/…`)
are codegen outputs (`make codegen`), never edited by hand.

## Wire model (the one-paragraph version)

The framework mints wire node ids for non-transparent widgets (root group
is id 1; the overlay root is id 2; children count up from 3). Every
minted widget's group separates repaintable chrome from retained content:

```
GROUP(w)                    containment — stable, offset = position
  GROUP(skin_w) { prims }   widget_paint: repaintable chrome only
  [NODE_ID + complex]       widget_paint_retained: hosted runtime
  child widget groups...
```

The first emit ships ONE envelope: `RESERVE(viewport rows)` + the whole
tree as nested `GROUP` records (paint in widget-local coordinates) + one
`GROUP_FIELD_OFFSET` update per nonzero-origin group. A clean frame ships
ZERO bytes. After that, per dirty class: a moved widget is one offset
update (~20 B), a restyled/resized widget is one addressed SKIN-subgroup
reopen (`CMD_PATH` incl. the widget + `GROUP(skin)`) that never touches
children or hosted runtimes, a structural change (membership, or an
intentional retained replacement such as `set_record`) reopens the widget
group, and a complex payload is one `CMD_PATH` + `UPDATE` envelope
(`framework_stream_update`, addressed at the containment path) with no
repaint at all. Scroll viewports emit a `GROUP_FIELD_CLIP` rect, mount
children beneath an owned content group and scroll it with ONE offset
update. Containment depth accepts 7 minted levels (the deepest skin
subgroup is the 8th nested group — yvterm's full ingest stack). The
wire-cost model is pinned headless in `test/ut/ygui2/wire-test.c`.

## Input

Apps own their PTY read loop and forward raw bytes to
`framework_feed_input`. Two inbound framings are parsed (they are
asymmetric on purpose — see `strategy.md` §7): OSC-framed SC client-input
envelopes (mouse button/motion/wheel + resize, subscribed by
`framework_attach`) and CSI/plain keys. Pointer events hit-test the
overlay tree first, then the root tree, dispatch to the deepest visible
widget, and bubble until consumed; a press captures for the drag. Keys go
to the focused widget first, then the app callback. Tab / Shift-Tab walk
tree order over visible focusable widgets, Esc closes overlays, and a
press outside a dismiss-on-outside overlay hides it and swallows the
click.

## Theme

`struct yetty_ygui2_theme` (defs.h) carries the palette in the packed
0xAABBGGRR wire format; the framework defaults to the brand palette and
widgets read the roles at paint time (`widget_theme_copy`). Per-widget
color setters (label color, panel bg, progress accent) still override with
0 meaning "theme default". `framework_set_theme` restyles the whole tree
(one reopen per minted widget).

## Widget catalog

| Technique | Widgets |
|---|---|
| T1 prim leaf | label, button, checkbox, toggle, radio, slider, spinner, progress, separator, chip, statusbar, stepper, textinput, tooltip, popup_menu, dropdown |
| T2 transparent layout | row/column (`row_add` / `column_add`) |
| T3 chrome container | panel, table, dialog |
| T4 offset/clip scroller | scrollarea |
| T5 complex node | complex_host (creation record once, then streamed `UPDATE`s); plot (wraps api_yplot — DSL builders, single streamed ring buffer with capacity 2..65536, `plot_append_samples` at ~40 B/sample, resize = one addressed geometry op with receiver-local chrome re-render, cached-window replay across intentional structural replacement) |
| T6 buffer embed | ydraw_embed |
| overlay (modifier) | popup_menu, dropdown's popup, dialog, tooltip — mounted under group 2 via `framework_overlay_add`, absolutely placed, shown/hidden with `set_visible`. The overlay tree paints in its own ambient paint-z band (`CMD_PAINT_Z 1000`), above every app-tree primitive; hosted records (complexes/embeds) carrying z >= 1000 are outside the guarantee |

Remaining strategy.md §10 catalog entries (menubar, window, splitter,
colorpicker, datepicker, combobox-with-typing, tree_node,
collapsing_header, tabbar, list, filepicker, textarea, rich, ynodes) are
not yet implemented; each maps onto one of the six techniques above.

## Invariants and caveats

- **Fullscreen ownership**: ygui2 owns the pane's output channel; app
  writes that scroll the surface retire wire ids that the fire-and-forget
  DCS channel cannot detect (strategy.md §5, ownership invariant).
- **Resize never rebuilds — unconditionally**: in fullscreen mode (the
  default) the first frame reserves the FULL supported viewport range
  (32768 px; the terminal accepts reserves to 16 Mpx), so EVERY height
  `set_viewport` can accept is inside the immutable span. Inline mode
  (`framework_set_fullscreen(0)` before the first emit) reserves only
  the declared viewport height — the insertion lives in the scrollback
  flow — and growth past it is an explicit rejection (`clear()` + emit
  re-inserts). Every accepted resize — grow, shrink,
  width — is relayout-only and the live insertion with its hosted
  complex runtimes (cached plots AND uncached generic hosts alike)
  survives; no accepted transition can destroy retained state.
  Non-finite or beyond-range dimensions are rejected outright without
  touching the committed viewport, and the pane-resize envelope path
  commits `content_scale` only after the viewport transition succeeded
  — input mapping can never diverge from the drawn projection. A widget hosting a resizable
  runtime (the plot) follows up with ONE addressed geometry op (~28 B)
  in the same frame envelope; the receiver re-plans the retained record
  and re-renders the plot's self-owned chrome group locally — the
  record, its sample data and its chrome never re-ship on resize. Only
  a truly structural change (expression / buffer declarations)
  replaces the record, and that insertion replays the cached sample
  window inside its own envelope. The delete+home+reinsert rebuild
  exists only as ship-failure recovery.
- **Ship failures** roll the projection back to a full-rebuild state: the
  next emit deletes both roots and re-inserts, so local and terminal
  state cannot silently diverge.
- **Retained content survives repaints** — skin/theme/ancestor-resize
  reopens touch only skin subgroups; hosted complex runtimes live in the
  containment group and persist. A STRUCTURAL ancestor reopen (membership
  change above a host) still recreates hosted complexes (strategy.md §4);
  keep complex hosts out of structurally churning subtrees until E5
  lands.
- **Generated accessor surface**: codegen exposes framework-internal
  mutators (`init_base`, `link_child`, `set_node_id`, emitted-state
  setters …) alongside the app API. Apps use the documented surface —
  tree building via `root_create`/`widget_add`/`overlay_add`, removal via
  `widget_remove` (never raw `widget_destroy`), state via the per-widget
  setters. Guards exist where misuse is destructive (`set_transparent`
  after emission errors; depth over the wire budget errors at add).
- **ydraw_embed is leaf-only**: `set_buffer` validates the record region
  and rejects command records (path/update/delete/reserve/paint-z/group)
  — an embed is contained content, not a producer program.

## Hosts and apps

- `src/yetty/yguiapp2/` — terminal host: fully raw termios (no ICRNL/
  IXON/ISIG — Ctrl-C is the byte 0x03 on the clean quit path) + alt
  screen, attaches the framework to stdin/stdout, select loop with
  lone-Esc flush, emit-on-dirty, HOLD/ACK input barrier + unsubscribe on
  every exit path. `q` / Ctrl-C quits.
- `tools/ytop2/` — live dashboard port (per-core bars, memory, process
  table) at a few hundred bytes per tick.
- `tools/ygreeter2/` — THE feature showcase (all widget demos live in
  this one executable): chips, toggle→tooltip, radio group→stepper,
  slider→progress binding, spinner, textinput, dropdown, dialog,
  statusbar, click counter, checkbox, and the clipped wheel-scrolling
  area (offset-only updates).
- **FFI demos (Python, Lua, Go, TypeScript)** —
  `demo/scripts/ffi/ygui2/<language>/*.sh` (run inside a yetty pane) →
  sources in `demo/ffi/ygui2/<language>/` (hello, counter, dashboard,
  catalog — the same four apps in every language). Each language has an
  ergonomic wrapper with the same shape (App host loop, Node builders,
  callback trampolines, RadioGroup):
  `bindings/python/yetty/ygui2.py` (over the generated
  `yetty.generated.ygui2` module), `bindings/lua/yetty/ygui2.lua`
  (LuaJIT ffi, hand-cdef'd), `bindings/go/ygui2/` (cgo package;
  `go.mod` replace-directive from the demo module), and
  `bindings/typescript/ygui2.mjs` + `ygui2.d.ts` (koffi over the shared
  runtime, exported as `@yetty/ydraw/ygui2`). `libyetty_ffi.so` exports
  the whole ygui2 surface.

## Tests

`test/ut/ygui2/wire-test.c` (ctest `ygui2_wire`) pins: the one-envelope
first frame; zero-byte clean frames; one smaller reopen per text change;
nothing for unchanged setters; click dispatch + capture; the stream_update
envelope shape (including the tree staying clean); Tab focus traversal;
typed bytes landing in the focused textinput; popup item selection closing
the popup; outside-press dismissal. The engine-side contract behavior
(spans, retirement, clip, reopen placement) is pinned in
`test/ut/yvterm/`.
