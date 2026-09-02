# ygui2 — widget toolkit on the drawable contract

Strategy for the second-generation widget toolkit. ygui2 renders EXCLUSIVELY
through the drawable contract (`src/yetty/yvterm/drawable-use-cases.md`):
groups, complexes, primitives, folded-path addressing, insert/update/delete,
group offsets, declared row span. No figure minting, no RPC session, no
full-frame redraw. The old ygui and its apps stay untouched; ports are
duplicates suffixed `2` (ytop2, ygreeter2, ...).

## 1. What ygui2 is

A retained-mode C widget toolkit (yclass module `ygui2`, files in
`src/yetty/ygui2/`) whose only backend is a byte stream to a yvterm pane:

- **out**: DCS drawable envelopes on `write_fd` — the widget tree projected
  onto the contract's group tree.
- **in**: pane-wide client-input OSC envelopes (mouse + geometry, via the
  `CLIENT_INPUT_SUB` MOUSE_*/RESIZE bits) + raw PTY key bytes on `read_fd`
  (§7).

That makes every ygui2 app a pure PTY client: it works locally, inside
`yetty -e`, and over SSH, with zero transport code in the app. The framework
does hit-testing, focus, and layout client-side; the terminal only stores,
projects, and paints drawables.

## 2. What dies from ygui — and what replaces it

| ygui mechanism                                    | Why it dies                                                     | ygui2 replacement                                                    |
|---------------------------------------------------|-----------------------------------------------------------------|----------------------------------------------------------------------|
| `CMD_ZERO` + full-frame re-emit on any change      | Resends the whole UI for a 1px slider move                      | Per-widget dirty classes; smallest sufficient wire op (§4)           |
| Anonymous prims, no per-widget invalidation        | Nothing on the receiver is addressable                          | Widget tree = group tree; every repaintable unit has a path (§3)     |
| Figure minting / sibling figures for rich content  | Parallel object universe with its own lifecycle + RPC           | Complexes are in-tree nodes with own ids; data streams via `update`  |
| yclass RPC session / ywire to the figure container | Heavy transport, doesn't cross SSH                              | DCS out / OSC in over the PTY                                        |
| Two-pass `emit_container` / `emit_body` walk       | Existed only to interleave figure boundaries with prims         | One `paint()` per widget into its own group                          |
| scrollarea / popup as promoted figures             | Scroll = re-emit or figure scroll machinery                     | Group offset `update` (~20 bytes) + clip (§5, §8)                    |
| No keyboard focus model                            | Apps hand-route every key                                       | Framework focus chain: click-to-focus, Tab order, `on_key` slot (§7) |

## 3. Core mapping: widget tree → group tree

**A widget mints a group iff it needs independent wire addressability.**
Not every widget does — that keeps wire depth far below the widget-tree
depth (path limit: 8 ids; nesting limit: 8).

| Widget kind                                       | Wire presence                                                                |
|---------------------------------------------------|------------------------------------------------------------------------------|
| Leaf that paints (button, label, slider, ...)     | Own group; prims in a separately minted SKIN subgroup (every non-transparent widget has one) |
| Container that also paints (panel, window, dialog)| Own group; chrome prims in its skin subgroup; child widget groups follow      |
| Layout-only container (hbox, vbox, splitter panes)| **Transparent** — no group; children attach to the nearest minted ancestor   |
| Scroll / visibility / clip boundary (scrollarea)  | Own group (offset + clip carrier); user children mount under one owned content group |
| Complex host (yplot, yimage, yvideo, yshadertoy)  | Own group; the complex is RETAINED content directly in the containment group (never in skin), a child node with its own id |
| App-buffer embed (ydraw_embed, ymarkdown, ypdf)   | Own group; app drawable list inserted as its subtree                         |

- **Id allocation**: one monotonic u32 sequence for everything — the app
  root is 1, the overlay root 2, widgets count up from 3 at add time, and
  each minted widget's SKIN subgroup id (plus any retained child id, e.g.
  the plot's stream target) is minted lazily from the same sequence at
  first insertion. Ids are stable for the widget's lifetime.
- **Offsets carry position**: a minted group's offset = widget rect origin
  minus the accumulated origin of its nearest minted ancestor. Transparent
  containers contribute position only through their children's offsets.
- **Paint order = emission order, stable across reopens**. The root
  envelope is `GROUP(1)` app tree + `GROUP(2)` overlay tree; popups,
  dropdown lists, dialogs, and tooltips mount under the overlay root and
  paint on top. This holds across structural reopens because the engine's
  replacement-anchor rule covers the WHOLE subtree: a nested group created
  inside a replacement body inherits the replaced subtree's paint anchor
  (pinned by `test_reopen_nested_subtree_keeps_slot`), so reopened app
  content can never leapfrog the overlay. Belt and braces for hard
  stacking guarantees (e.g. a modal barrier): wrap the overlay tree in an
  explicit `CMD_PAINT_Z` scope — the wire plane machinery already exists.

Typical wire depth: root / overlay / window / scrollarea-content / widget =
5 — comfortably inside the limit of 8 even for ygreeter-class UIs, because
hbox/vbox chains are transparent.

## 4. Rendering: dirty classes and their wire cost

The framework tracks five dirty classes per widget; `emit()` walks the tree
once and issues the **smallest sufficient operation** for each, all
concatenated into ONE envelope (one batch):

| Dirty class     | Trigger                                     | Wire operation                                       | Cost               |
|-----------------|---------------------------------------------|------------------------------------------------------|--------------------|
| position        | layout moved the rect, content unchanged    | `update(path)` → `GROUP_FIELD_OFFSET`                | ~20 bytes          |
| skin            | text / color / value / size changed         | reopen the widget's `skin` subgroup                  | that widget's prims|
| geometry        | SIZE changed on a widget hosting a resizable runtime (plot) | `update(path-to-complex)` geometry op — the receiver re-plans the runtime + its chrome LOCALLY | ~28 bytes |
| structure       | child added / removed / reordered           | reopen the parent group subtree; `delete(path)` for removals | that subtree + all runtimes under it |
| complex data    | plot points / video frame / shader uniforms | `update(path-to-complex)` with the runtime payload   | payload only       |

Streaming complex data (`plot_append_samples`, `plot_stream_samples`,
ranges ops) is DELIBERATELY immediate: each call ships its own envelope
right away instead of waiting for the next `emit()` — a live feed must
not queue behind GUI-frame batching. Everything driven by dirty classes
(position/skin/geometry/structure) still folds into one envelope per
`emit()`.

**Structure-dirty caveat — runtime loss.** An exact-subtree reopen kills
EVERY descendant, complex runtimes included: adding one table row can reset
a streaming plot anywhere below the reopened parent. The wire today cannot
express "insert a missing descendant under a live parent" (the contract
describes it; `CMD_PATH` latches only onto update/delete). E5 narrows the
blast radius but does not erase it: it covers **additions** (addressed
descendant insert) and, with addressed `delete`, **targeted removals** —
it cannot reorder an existing child or move its emission slot. ygui2
therefore handles the three structure-dirt shapes differently:

- **add** → E5 descendant insert (until it lands: parent reopen);
- **remove** → addressed `delete(path)` (already on the wire);
- **reorder** → NOT a tree operation. Widget order on screen is layout,
  so the framework expresses visual reorder as **offset updates** on the
  moved children — tree order only matters where siblings overlap, which
  chrome siblings in a layout never do. The rare z-critical overlap
  reorder (overlay stacking changes) keeps the documented reopen blast
  radius.

The containment TARGET is enforcement by CONSTRUCTION: complex-hosting
widgets get a dedicated carrier group outside every sibling-churn scope,
so no reopen for an add/remove of siblings can span a live complex.
**Current state: NOT yet implemented.** The landed emitter is the
documented pre-E5 fallback — an ancestor's structure dirt reopens the
whole subtree, killing and recreating any complex runtime below it.
For a GENERIC `complex_host` the streamed runtime state is NOT replayed
afterwards; the PLOT widget is the cache-aware exception — it keeps the
authoritative sample window producer-side and replays it (full window +
ring head) INSIDE the replacement insertion envelope, so its visible
state survives every structural reopen with no later application
traffic. Until E5 (addressed descendant insertion) and the carrier
topology land, keep uncached complex hosts out of subtrees with
structural churn.

Engine facts this relies on (verified):

- An in-place reopen of a live path never sets `new_content`
  (`terminal.c:2099-2104`) — a batch made purely of reopens/updates/deletes
  takes **no placement and moves no cursor**. Steady-state frames are
  placement-free.
- Wire-order nontransactional apply: one envelope carrying
  `delete + reopen + N updates` applies in order; a command-local failure
  (e.g. oversized replacement) skips that command only.

**First frame** is the one insertion:
`RESERVE(32768px — the full supported range)` + `GROUP(1){app tree}` +
`GROUP(2){overlay}` +
**one `GROUP_FIELD_OFFSET` update per minted group with a nonzero local
origin** — one batch, one placement, span = the declared viewport. The
offset updates are REQUIRED: a `GROUP` record carries identity and body,
not coordinates, so every fresh group starts at (0,0) and locally-painted
widgets would otherwise stack at the origin. Wire order inside the batch:
inserts first, then the offset updates (wire-order apply resolves the
just-created paths). Cost: ~20 bytes per minted group — part of the
first-frame budget and of the wire-cost tests (§12).

**Frames after that** are reopens + updates: a slider drag reopens its
skin subgroup (~100 bytes); a window drag is one offset update; a
streaming plot append is one envelope (sample chunk + ring-head op,
~40 bytes/sample); a plot RESIZE is one geometry op (~28 bytes) — the
record, its samples and its chrome never re-ship, the receiver re-plans
locally. The 60 fps case never resends the tree.

## 5. Modes, positioning, scrolling, visibility

**Fullscreen mode** (ytop2, ygreeter2; the framework default): enter the
alternate screen, cursor home, insert with `RESERVE` = the FULL supported
viewport range (32768 px — see §5 Resize), so every accepted resize stays
inside the immutable span. The alternate screen CAN scroll
(a linefeed on the bottom row rotates it — and, having no history, a scroll
destroys departing content), but the engine guarantees the reservation
itself never does: the insertion's span is installed before the cursor
advance and the advance clamps at the last row (contract §1b; pinned by
`test_alt_screen_insert_never_scrolls_itself`). The framework additionally
never emits bare newlines into the pane, so nothing else scrolls either —
the insertion stays live, every id stays live for the whole app run.

**Ownership invariant (normative):** in fullscreen mode ygui2 OWNS the
pane's output channel — nothing else may write bytes that scroll the
surface. The DCS wire is fire-and-forget: there is no acknowledgement,
liveness query, or retirement notification, so the framework CANNOT
detect that a terminal-side scroll retired its ids; after such a scroll
every incremental update silently no-ops against retired paths. A scroll
is therefore a CONTRACT VIOLATION by the app (stray stdout/stderr writes
reaching the pane), not a recoverable event; the visible symptom is a
frozen UI, and the manual recovery is `framework_clear()` + emit (fresh
insertion). A future engine extension (projection generation / liveness
response) is the prerequisite for automatic recovery and for inline-mode
robustness. On exit, leaving the alt screen discards the block with the
screen. This is exactly the ncurses shape.

**Inline mode** (yzoo2-style toys, ynet-style one-shot figures;
`framework_set_fullscreen(0)` BEFORE the first emit): one insertion with
`RESERVE` = the declared viewport height at the cursor — never more; the
insertion sits in the user's scrollback flow and the reserve advance
really scrolls, so over-reserving would blast the transcript. Growth past
that reservation is an explicit `set_viewport` rejection; the app grows
by `clear()` + emit (a fresh insertion **at the current cursor**, a new
transcript block — inline re-insertion never writes the cursor-home the
fullscreen clear path uses: homing would stamp the replacement over
visible row 1 of unrelated shell output; the old reservation may remain
as blank transcript space). Subsequent frames reopen in place. As the user's shell scrolls it, nodes retire individually
as their footprints leave the surface (frozen into the transcript), and
the whole insertion seals once fully off — the frozen image is the
transcript record. No special casing; the contract already does this.

**Scrolling** is the contract's headline mechanic applied per widget: a
scrollarea is a group whose child content group gets
`update → GROUP_FIELD_OFFSET` on wheel/drag. Content taller than the
scrollarea's rect needs a **clip rect** so it doesn't bleed over siblings —
today the engine clips only to the insertion's row span. That is engine
extension E1 (§8). Panning a ynodes canvas is the same offset on its
content group.

**Visibility** (`widget_set_visible`, popup open/close, tab switching):
delete + re-insert works but kills complex runtimes under the hidden
subtree. For an uncached generic `complex_host` the streamed state is
gone; a hidden PLOT gets it back on re-insert — the insertion envelope
replays its producer-side cached window. Engine extension E2 adds
`GROUP_FIELD_VISIBLE` so hiding becomes a state write that preserves
runtimes outright.

**Resize** (RESIZE envelope): NEVER destructive, unconditionally. In
fullscreen mode the first frame reserves the FULL supported viewport
range (32768 px — the terminal's reserve clamp sits at 16 Mpx, and an
alt-screen app's span is bookkeeping, not storage), so EVERY height
`set_viewport` accepts is inside the immutable span and no accepted
resize can ever need a new insertion (inline mode reserves only its
content height and rejects growth past it — see the modes above): grow, shrink, and width changes are all relayout-only —
targeted SKIN reopens for widgets whose size actually changed, offsets
for moves, nothing for the rest. Beyond-range or non-finite dimensions
are rejected outright WITHOUT touching the committed viewport (never a
silent projection clip, never a destructive "recovery"). The
pane-resize envelope path is transactional: `content_scale` commits
only after the viewport transition succeeded.
The live insertion, its ids, and every hosted complex runtime survive
every resize. A widget hosting a RESIZABLE runtime (the plot) follows
up with ONE addressed geometry op (~28 bytes) in the same frame
envelope: the receiver re-plans the retained record at the new figure
size and re-renders the plot's self-owned chrome group locally
(yplot_record_rechrome) — the record, its sample data and its chrome
NEVER re-ship on resize, however large the runtime (§6).
Content beyond the current pane is projection-clipped for free. (The
delete-roots + home + fresh-insert rebuild still exists, but ONLY as
ship-failure recovery — never for resize.)

## 6. Complexes inside widgets

In ygui, a plot widget minted a sibling figure over RPC. In ygui2 every
minted widget's emitted shape separates REPAINTABLE chrome from RETAINED
content:

```
GROUP(w)                    <- containment: stable, offset = position
  GROUP(skin_w) { prims }   <- widget_paint: repaintable chrome ONLY
  NODE_ID(1)
  Plot(...)                 <- widget_paint_retained: hosted runtime
  GROUP(chrome_1) { texts } <- the runtime's SELF-OWNED chrome (plot:
                               ticks/title/legend; receiver-replaceable)
  child widget groups...
```

- **Data streaming**: `CMD_PATH([...,w]) + update(1, payload)` — points
  flow without any repaint. The retained node binds DIRECTLY in the
  containment group, so the address never includes the skin subgroup.
- **Movement**: the widget group's offset moves the complex; the runtime
  survives (contract: movement belongs to groups, runtimes survive it).
- **Resize / axis change**: one addressed geometry/ranges op. The plot's
  creation record carries a chrome-state tail (title, labels, legend,
  the pre-inset figure rect); the RECEIVER re-plans the record in place
  and reopens `GROUP(chrome_1)` with locally re-rendered label prims
  (`yetty_yplot_record_rechrome` + the instance's `emit_chrome` op) —
  no producer bytes beyond the op. The chrome is PART OF THE PLOT:
  the widget's skin stays empty; ygui2 never draws plot chrome.
- **Skin/theme/ancestor repaints** reopen only `GROUP(skin_w)` — the
  retained runtime is untouched. Replacing the creation record
  (set_record, plot expression/buffer-declaration changes) is an
  INTENTIONAL structural reopen — the ONLY remaining record re-send,
  and the plot replays its cached window inside that same envelope.
- yimage/yvideo/yshadertoy are the same shape with their kinds; the
  `plot` widget wraps api_yplot behind this contract.
- **Depth budget**: the containment tree accepts 7 minted levels — the
  deepest widget's skin subgroup (and a plot's chrome group, at the same
  level) is the 8th nested group, exactly yvterm's ingest stack. The
  CMD_PATH id budget (8) is separate.

App-buffer embeds (ydraw_embed, and ymarkdown/ybrowser/ydiagram/yrich_view/
ypdf on top of it) insert the app's drawable list as the embed group's
subtree; a content change reopens that group.

## 7. Input

What the host provides (the pane-wide mouse path landed as engine
extension E6; the DEC-mode path serves figure-based clients and stays
untouched):

- `CLIENT_INPUT_SUB` (610010) honours **RESIZE** (geometry envelopes incl.
  the HiDPI `content_scale`), **KEY_FANOUT** (figure-focused structured
  keys — irrelevant here: rich groups are not figures), and the
  **MOUSE_CLICK / MOUSE_MOVE / MOUSE_WHEEL** bits: mouse events that no
  yscene figure owns are emitted as pane-wide `SC_CLIENT_INPUT_MOUSE`
  envelopes (`figure_id` 0, PANE-LOCAL framebuffer pixels). A subscribed
  wheel is claimed by the client instead of driving terminal scrollback —
  the same philosophy as `?1502` claiming the scroll keys. Pane mouse
  subscription also suppresses the terminal's own click-selection, and
  any input subscription triggers geometry envelopes on pane resize.
- **Keyboard** stays on the raw PTY channel: CSI/keycode bytes
  interleaved with the envelopes.

So `attach` does: send `CLIENT_INPUT_SUB` with
`RESIZE|MOUSE_CLICK|MOUSE_MOVE|MOUSE_WHEEL`, optionally
`CONTENT_RECT`/`CONTENT_INSET`. No DEC mouse modes needed — a pure
drawable-tree app has no figures for that path to hit. `detach`/exit sends
a zero-flag `CLIENT_INPUT_SUB` (each envelope declares the full state) with
a synchronous flush before the fds close — leave the terminal as found.
`feed_input(bytes)` splits the stream: client-input OSC envelopes decode
into events; everything else runs through a CSI key decoder (the ygui
`feed_byte` contract). Apps keep owning their read loop exactly as before.

- **Mouse**: pane-local pixel coordinates from the envelopes, divided by
  `content_scale` into the framework's logical space before hit-testing.
  Hit-test walks the client-side widget tree depth-first, later siblings
  win, overlay tree first, scroll offsets subtracted, hidden subtrees
  pruned. Dispatch `on_press/on_release/on_motion/on_scroll`
  (consumed-int, bubbling) — same virtual contract as ygui so widget
  logic ports unchanged.
- **Keyboard focus (new)**: framework-owned focus chain over the RAW-key
  decode. Click focuses the deepest focusable widget; Tab/Shift-Tab
  traverse focusables in tree order; Esc closes the topmost overlay. The
  focused widget's `on_key` slot gets the event first; unconsumed keys
  fall through to the app-level key callback (`set_key_cb`, kept for
  compatibility — ytop2's global hotkeys keep working unmodified).
- **Resize**: RESIZE envelope → `set_viewport` internally → relayout
  only — targeted skin reopens and offsets, never a re-insert (§5).

## 8. Engine extensions required (small, in order)

| Id | Extension                                                        | Why                                                             | Shape                                                                                     |
|----|------------------------------------------------------------------|------------------------------------------------------------------|-------------------------------------------------------------------------------------------|
| E5 | Addressed **descendant insert** (missing-descendant form)        | structure-dirt ADDITIONS must not kill complex runtimes below the reopened parent (§4 caveat — removals use addressed delete, reorder uses offsets; E5 does NOT cover reorder) | extend the `CMD_PATH` latch to apply to the next `GROUP`/complex record: insert-at-path joins the parent's insertion, appended after existing children, must fit the span, no cursor movement |
| ~~E1~~ (LANDED) | `GROUP_FIELD_CLIP`                                               | scrollarea/table/list viewports; also fixes the known complex edge-bleed gap | `update` payload `[field][f32 x][y][w][h]` local-space; projection intersects ancestor clips; SDF via shader rect test, complexes via a clip pass-through. **Requires a contract amendment first**: today groups carry coordinates and NEVER size (§1a); a clip rect is spatial extent. The amendment declares clip as non-layout PROJECTION state (it never affects row span or placement) — or, if the coordinates-only rule stays strict, clipping becomes a separate projection-scope node instead of a group field. Decide in the contract before implementing. |
| E2 | `GROUP_FIELD_VISIBLE`                                            | hide/show without killing complex runtimes                       | `update` payload `[field][u32 visible]`; projection skips hidden subtrees                  |
| E3 | Raise `PATH_MAX_IDS` / `GROUP_DEPTH_MAX` 8 → 16                  | safety valve for pathological trees (transparent containers keep typical depth ≤ 5) | constant bump + tests                                                     |

Landed already: **E1 `GROUP_FIELD_CLIP`** (wire field 0x2 → store →
ancestor-intersecting projection resolve → 7-word SDF prim header + WGSL
content-space discard, pinned by the layout contract check; contract §1a
amended: clip is non-layout projection state). Also landed (were E4/E6 +
review follow-ups): **pane-wide mouse
forwarding** (`CLIENT_INPUT_SUB` MOUSE_* bits → `SC_CLIENT_INPUT_MOUSE`
for figureless events, wheel claimed from scrollback, selection
suppressed — §7); span installed before the reserve advance +
alternate-screen advance clamp (an insertion can never seal/destroy
itself; pinned); the batch's provisional insertion tracked independently
of the mutation scope (mixed reopen/new-content batches in any order;
pinned); ring-deep reservations survive via the chunked re-homing advance
(pinned); reopen-only and reserve-only batches take no placement
(pinned / per contract); whole-subtree replacement-anchor inheritance
(overlay stacking across reopens; pinned); offset/RESERVE input
validation + fractional-bottom ceil; archived frozen offsets (tier v5;
pinned). Everything else ygui2 needs already exists and is demo-verified:
folded paths, `CMD_PATH`, `CMD_NODE_ID`, `RESERVE`, offsets + projection +
span clipping, exact-subtree reopen, complex streaming, alt-screen rich
blocks.

## 9. API compatibility — port by suffix

The hot client surface keeps its names under the `yetty_ygui2_` prefix, so
ytop-class apps port mechanically (`s/yetty_ygui_/yetty_ygui2_/`) plus
deletions:

| ygui symbol                                                | ygui2 disposition                                                       |
|------------------------------------------------------------|-------------------------------------------------------------------------|
| `framework_create / destroy / clear`                       | same                                                                    |
| `framework_attach(read_fd, write_fd, compressed)`          | same signature; now = DCS/OSC transport + input subscription, not RPC   |
| `framework_set_root / set_viewport / set_key_cb`           | same                                                                    |
| `framework_feed_input / feed_mouse_button / _motion / _scroll` | same (mouse feeds exist for in-process hosts; PTY apps get mouse via envelopes) |
| `framework_is_dirty / emit`                                | same names; emit is now incremental (§4)                                |
| `framework_set_container_obj / attach_connection`          | **dropped** (no figure container); in-process hosts get a sink callback instead |
| `widget_new / widget_add / set_position / set_size / set_visible / layout_get / layout_set` | same                                       |
| per-widget setters (`label_set_text`, `progress_set_value`, `table_add_row`, ...) | same names, same semantics                       |
| clickable mixin `on_click_set`, event bus `widget_subscribe/emit` | same                                                             |
| theme struct + `set_theme` + per-widget color setters      | same shape (`yetty_ygui2_theme`); both styles kept — ytop's per-widget palette must keep working |
| `emit_ctx`, `emit_create_child`, figure helpers            | **dropped** — widget authors implement `paint()` into the widget's group |

In-process host (yui): the framework gains a **sink mode** — instead of a
`write_fd`, envelopes go to a callback the host wires straight into its own
terminal's ingest. Same envelope bytes, no PTY round-trip. That is how yui
eventually migrates; not needed for the app ports.

## 10. Widget implementation plan — techniques, then layers

Six techniques cover all 53 widgets; each widget names its technique and is
otherwise just paint + state:

| Technique                | Mechanism                                                        | Widgets                                                                                                       |
|--------------------------|-------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| T1 prim leaf             | own group + skin subgroup; skin-dirty → skin-subgroup reopen     | label, button, checkbox, radio, toggle, slider, spinner, stepper, progress, separator, chip, selectable, statusbar, breadcrumbs, tooltip, colorpicker, datepicker, choicebox, textinput, textarea, rich, tabbar, menubar |
| T2 transparent layout    | no group; flex math only                                          | hbox, vbox, splitter, collapsing_header (header row is a T1 child)                                             |
| T3 chrome container      | group + skin subgroup + child groups (skin subgroups are UNIVERSAL: every minted widget gets one) | panel, window, dialog, table, list, tree_node                                                                  |
| T4 offset/clip scroller  | group; content child group; wheel → offset update; E1 clip        | scrollarea, filepicker, long list/table bodies, ynodes canvas (pan)                                            |
| T5 complex node          | retained hook: own-id complex DIRECTLY in the containment group (never in skin); data → `update` | yplot, yimage, yvideo, yshadertoy                                                                              |
| T6 buffer embed          | group; app drawable list as subtree; change → reopen              | ydraw_embed, ymarkdown, ybrowser, ydiagram, yrich_view, ypdf, ymaze, yzoo, yjungle                             |
| overlay (modifier)       | mounts under `GROUP(2)`; absolute offset                          | popup_menu, dropdown/combobox popups, dialog, tooltip                                                          |

Phases (each ends with a runnable milestone):

1. **Core** (LANDED) — module skeleton (`ygui2:framework`, `ygui2:widget`
   yclass classes), transport (envelope writer over the drawable-list API,
   input envelope parser), client-side flex layout, group minting + id
   paths, dirty tracker, theme. Milestone: headless wire tests green
   (`test/ut/ygui2/wire-test.c`).
2. **ytop2 set** (LANDED) — T1 label/progress + T2 hbox/vbox + T3
   panel/table + T6 ydraw_embed + key callback. Milestone: **ytop2** live
   in a pane at a few hundred bytes per tick, zero-placement steady
   frames.
3. **Interaction** (LANDED) — press/release/motion/scroll dispatch with
   bubbling + capture, click-to-focus on the nearest focusable ancestor,
   Tab/Shift-Tab traversal, button/checkbox/toggle/radio/slider/spinner;
   overlay root (wire group 2, always inserted), popup_menu, dropdown,
   dialog, tooltip, Esc-closes-overlay, dismiss-on-outside. Milestone:
   ygreeter2 exercising focus + popups live.
4. **Scrolling + text** (LANDED core) — E1 clip, scrollarea, textinput.
   Milestone: ygreeter2's clipped wheel-scrolling area live, and its
   textinput with caret + focus ring. Remaining: textarea, list, filepicker,
   tabbar, tree_node, collapsing_header.
5. **Rich content** (LANDED core) — T5 complex_host (creation record once,
   then `framework_stream_update` addressed envelopes; stream shape pinned
   headless), the first-class `plot` widget (ring-append streaming: live
   in ygreeter2's wave and ytop2's per-core cpu history), T6 ydraw_embed,
   `yguiapp2` terminal host. Remaining: toy ports (yzoo2 / ymaze2 /
   yjungle2).
6. **Catalog** (LANDED core) — separator, chip, statusbar, stepper,
   dropdown, theme roles read at paint time + `set_theme` restyle.
   Milestone: **ygreeter2**, the showcase — verified live (tooltip via
   toggle, dropdown selection, dialog open/close, slider→progress binding,
   spinner, typed textinput). Remaining: ynodes, datepicker, colorpicker,
   rich, combobox-with-typing, menubar, window, splitter — each maps onto
   an existing technique.

## 11. App port plan

| App      | Port effort                                                                                                    |
|----------|-----------------------------------------------------------------------------------------------------------------|
| ytop2    | Suffix swap + delete the RPC pump; keys/layout/setters unchanged. First real-world proof (phase 2).             |
| yzoo2 / ymaze2 / yjungle2 | Trivial — 8 symbols through `yguiapp2` (phase 5).                                              |
| yperf2 / ydu2 | Same shape as ytop2.                                                                                       |
| ygreeter2| The catalog test; ports last (phase 6). Its ywire container path is replaced by plain `attach`.                  |
| ynet2    | **ynet uses no ygui at all** — it emits a raw drawable list. ynet2 = wrap the topology in an own-id group so a future live-capture mode can `update` flows in place; otherwise unchanged. |

## 12. Testing strategy

- **Wire-cost pinning (headless, ctest)**: the framework emits into a
  capture buffer; tests parse it with the drawable-list iterator and assert
  the exact command sequence per dirty class — the first frame is exactly
  `RESERVE` + the tree + one offset update per minted nonzero-origin group,
  a moved widget produces ONE offset update, a text change ONE leaf reopen,
  a stream tick ONE complex update, a clean frame ZERO bytes. This pins the
  incremental model so a regression back to full-redraw is a test failure,
  not a perf mystery.
- **Ingest round-trip**: LANDED (#728) — `yterminal_ingest_roundtrip`
  feeds producer-serialized envelope bytes through the REAL terminal
  ingest via the headless harness
  (`yetty_yterminal_ingest_harness_open` + mime ingest → the same
  `terminal_ydraw_apply_body` pipeline as the PTY wire path) against an
  instrumented complex factory, and asserts: runtime identity across a
  geometry envelope (never re-created), the retained retirement extent
  following the runtime's new AABB, local chrome-group replacement at
  the figure's ORIGINAL paint-z from a scope-less update, and the
  failure contract (a failed chrome emission leaves the old chrome
  standing with `chrome_dirty` retryable; the next update converges).
  The yplot half (record rechrome, atomic patch_ranges) is pinned in
  `yplot_emit`; the receiver invariants (extent→retirement boundary,
  z-preserving replacement, capacity reserve, storage/topology
  boundedness) in `yvterm_rich_lifecycle`.
- **Input tests**: synthetic MOUSE/KEY envelopes through `feed_input`;
  assert hit-target, focus transitions, bubbling.
- **Live verification**: each phase milestone runs in a real pane via
  yctl.py + screenshots; ytop2 vs ytop side-by-side.
