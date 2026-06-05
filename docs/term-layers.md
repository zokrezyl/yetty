# Terminal Layers — Scrolling & Alt-Screen

This document covers the *dynamic* state model behind a yetty terminal: how
content scrolls, how alt-screen works, and how scrollback could eventually be
persisted to disk as a verifiable, mergeable history. It complements
`layered-rendering.md` (which covers the GPU pipeline / layer-vs-figure split)
and `terminal-screen.md` (which covers compositing).

## What a terminal renders today

A terminal (`src/yetty/yterminal/terminal.c`) no longer holds an array of
sibling layers. It renders exactly two things per frame, in order:

1. **one content layer** (`terminal->layer`, a
   `yetty_yrender_terminal_layer`), then
2. **the root `yfigure` container** (`terminal->root_container`), the
   positioned-figure compositor.

A third surface, the **background layer**, paints the per-pane opaque wipe —
but it is owned by the **yui tile** (`src/yetty/yui/tile.c`), not by the
terminal, because on a shared multi-pane target the wipe must be a scissored
draw per pane rather than a full-attachment clear. See
`include/yetty/yterminal/background-layer.h`.

### The content layer is a composite

The single content layer is a `yetty_yvterm_content_layer`
(`src/yetty/yvterm/content-layer.c`). It embeds the layer base as its first
member, so the terminal sees one `yetty_yrender_terminal_layer`, but it owns
and drives **two sub-renderers** that used to be sibling layers:

| Sub-renderer | Owns | Driven by |
|---|---|---|
| **text grid** (`text-layer.c`)     | one `VTerm` (libvterm) — primary + alt buffers, scrollback arena, cursor | PTY bytes (text + CSI/OSC) |
| **ydraw canvas** (`ydraw-content.c`) | a scrolling `ydraw_canvas` (rolling-row primitive grid) | DCS `600000`–`600003` |

Both sub-renderers keep their own GPU resource set + shader, so the content
layer's `render` op drives two render handles (`text-layer.wgsl`, then
`ydraw-layer.wgsl`) into the same target, plus a third pass for the
text-owned **shader-glyph figure**. Everything that used to round-trip
text↔ydraw through the terminal (scroll, cursor, alt-screen, clear, selection,
view-top, resize, visual-zoom) is now **internal wiring inside
content-layer.c**.

### Figures are not layers

`ymgui`, `yrdawn`, `ygrid`, `ygui` are **figures in the root `yfigure`
container**, not layers. They arrive over the compositor wire (DCS `630000`,
`YETTY_DCS_YCOMPOSITOR_BIN`), are positioned by the compositor in pane-pixel
coordinates, and paint themselves after the content layer. `ymgui-layer.c` and
`yrdawn-layer.c` are gone. Because they are compositor-positioned, **they do
not participate in the rolling-row scroll / alt-screen propagation described
below** — that model now lives entirely inside the content layer. (A figure
that *wants* to anchor to terminal rows does so as a drawable in the ydraw
canvas, which does scroll.)

> **Wire codes moved OSC → DCS.** Bulk drawing / figure payloads are large
> opaque blobs, so they ride DCS envelopes (which a multiplexer like tmux
> forwards verbatim), not OSC. ydraw lives at DCS `600000`–`600004`, the
> compositor at DCS `630000`, yclass RPC at DCS `800000`
> (`include/yetty/yterminal/dcs-codes.h`). Short control/metadata — the
> client-input channel (mouse / resize / focus / key / subscribe) — stays on
> OSC (`include/yetty/yterminal/client-input.h`,
> `include/yetty/yterminal/osc-codes.h`).

---

## 1. Scroll model: rolling rows

### The problem

A naive scroll-on-line-add costs O(lines × primitives) per scroll event. With
thousands of cells filling and the user holding `j` in vim, that's not viable.

### The trick

The ydraw canvas addresses lines by an **absolute monotonic counter** — the
*rolling row*. Lines never move; the viewport's idea of "row 0 on screen"
advances (`ydraw_rolling_row_0`, a u32 uniform set by
`ydraw-content.c::set_rolling_row_0`).

```
                rolling rows (monotonic, never decrement)
                      ▲
   ┌───── primary ────┴──────────────────────────────────┐
   │  17  18  19  20  21  22  23  24  25  26  27  28  …  │
   │                  ▲                                  │
   │           row0_absolute = 21       (= viewport top)  │
   │                                                     │
   │   visible: rolling 21..21+rows-1                    │
   └─────────────────────────────────────────────────────┘
```

A primitive placed at the cursor stores `rolling_row = row0_absolute +
cursor_row`. On screen its y-pixel is `(rolling_row - row0_absolute) *
cell_h`. **Scroll is a single counter bump**; no per-primitive update.

### Cross-renderer propagation (inside the content layer)

When one sub-renderer scrolls, the other must follow so anchors stay aligned.
This used to be a terminal-wide broadcast (`terminal_scroll_callback`); it is
now internal to the content layer, over exactly two sub-renderers:

```
text grid (libvterm: line falls off top)
   ↓ scroll_fn  (content_on_layer_scroll)
content-layer.c
   ↓ for each sub-renderer != source:
     sub->ops->scroll(sub, lines)      // ydraw bumps rolling_row_0
```

The `in_external_scroll` flag on each sub-renderer prevents the propagation
from ping-ponging back. The same shape applies to cursor moves
(`content_on_layer_cursor` → each sub-renderer's `set_cursor`). The terminal
no longer owns scroll/cursor broadcast callbacks — the content layer's
`scroll` / `set_cursor` ops are even left unimplemented, since nothing outside
the layer drives them.

### Scrollback view (tmux-style)

Mouse-wheel up (or PageUp) enters scrollback. The terminal pins a
`view_top_total_idx` (absolute row index, sub-renderer-agnostic) and pushes it
to the content layer via `set_view_top(active, view_top_total_idx)`
(`terminal.c::terminal_push_view_top`). The content layer fans that out to
both the text grid and the ydraw canvas
(`content-layer.c::content_layer_set_view_top`), each of which freezes its
display at that absolute row even as live content keeps arriving below.

The absolute index is stable because text and ydraw share the same anchor: the
content layer's `get_live_anchor` returns the max of the two sub-renderers'
anchors, and every text scroll triggers a ydraw scroll and vice versa, so one
index identifies the same logical row in both. Pressing Enter, typing, or
scrolling past the live anchor exits — `set_view_top(active=0)` returns to live
tracking (`terminal_scrollback_exit`).

### Lifetime today

Scrollback is held in RAM:

| Sub-renderer | Storage |
|---|---|
| text grid   | libvterm's `sb_pushline` callback feeds a per-terminal cells/lines ring arena (`text-scrollback.c`, `struct yetty_yvterm_text_sb_arena`) |
| ydraw canvas | the scrolling canvas keeps primitives keyed by rolling row; they stay alive until the line drops off the front |

Memory grows until the ring caps. Nothing is persisted. A yetty crash or
restart loses the entire history.

---

## 2. Alt-screen

DEC modes `?1049` / `?1047` / `?47` ask the terminal to swap to a separate
screen buffer (vim, less, mc, top, htop). On exit the original screen returns
intact. The ydraw canvas must follow too — otherwise the user's vim session
sees stray ydraw plots from the prior shell.

### Hook chain

```
PTY byte stream
   ↓
libvterm parser → settermprop(VTERM_PROP_ALTSCREEN, bool)
   ↓
text-layer.c::on_settermprop:
   • libvterm has already swapped its own buffer pointer
   • refresh the GPU cell buffer to the new buffer
   • fire base.alt_screen_fn(active)
   ↓
content-layer.c::content_on_alt_screen:
   • for each sub-renderer: sub->ops->set_alt_screen(active)
   • request_render
```

This used to be a terminal broadcast (`terminal_alt_screen_callback`); the
content layer now hooks the text grid's `alt_screen_fn` at create time and
drives the ydraw side from there. The same is true for full-screen erase (CSI
2J / 3J): the text grid's `clear_screen_fn` lands in
`content-layer.c::content_on_clear_screen`, which forwards to each
sub-renderer's `clear_screen` op.

### Per-sub-renderer save/restore

| Sub-renderer | Implementation |
|---|---|
| text grid   | libvterm owns the swap (primary + alt `VTermScreenBuffer`); we just refresh the GPU buffer pointer |
| ydraw canvas | lazy-build a sibling `ydraw_canvas` on the first toggle; toggle = swap `canvas` ↔ `saved_canvas` (`ydraw_content_set_alt_screen`) |

### What a sub-renderer does NOT do

- No data copy. Both halves coexist in their fully-initialized form.
- No GPU re-upload. WebGPU resources outlive the swap.
- No re-resize. Both halves track the same `grid_size` / `cell_size`.

The cost of a toggle is one `set_alt_screen` per sub-renderer plus a render.

> **Figures and alt-screen.** ymgui / yrdawn / ygrid figures live in the
> compositor, outside the content layer, and are *not* swapped by the
> alt-screen toggle. A figure that should hide under vim is the figure's own
> concern (the producing client stops emitting / removes it); the terminal
> does not save/restore figure state across the boundary.

---

## 3. Future: scrollback-as-history

> **Status: exploratory design note — not implemented.** This section sketches
> a direction for persistent scrollback; none of it ships today. It's recorded
> here because the rolling-row event stream above makes it a natural
> extension.

Today, scrollback evaporates with the process. There's a natural extension:
treat the rolling-row stream as an **append-only event log**, persistable to
disk, content-addressable, and mergeable.

### Why "blockchain-like"

Not a blockchain in the consensus sense — there's no network, no adversaries,
no proof-of-work. What we *do* want from that family:

| Property | What it buys |
|---|---|
| Append-only      | The history of a session is immutable once written |
| Hash-linked      | `entry[N].hash = H(entry[N-1].hash ‖ entry[N].body)` — any tamper invalidates the chain from that point |
| Content-addressed | Two identical entries have the same hash; trivial dedup across sessions/machines |
| Merkle-friendly | Easy to prove a sub-range without sending the whole log |
| Mergeable        | Divergent histories (parallel sessions, two machines, an ssh into the same server) line up by hash |

### Proposed entry shape

```
struct entry {
    uint64_t  rolling_row;      // monotonic per session
    uint8_t   source_id;        // 0=text grid, 1=ydraw, 2=figure
    uint8_t   kind;             // text-line / prim-add / frame / clear / ...
    uint16_t  flags;
    uint32_t  body_len;
    uint8_t   body[];           // text bytes / serialized prim / wire frame
    uint8_t   prev_hash[32];    // chain link
    uint8_t   self_hash[32];    // H(prev_hash || rolling_row || source_id || kind || body)
};
```

Each producer becomes a writer:

| Source | What it writes |
|---|---|
| text grid   | each line popped from libvterm's scrollback (already a clean event in `sb_pushline`) |
| ydraw canvas | each primitive add / line drop |
| figures      | figure lifecycle + each frame's mesh+atlas (or just the *figure_id, hash-of-frame* for dedup) |

### File format

```
session.ylog
├── header { magic, version, session_uuid, started_at, cell_size, … }
├── entry 0
├── entry 1
…
└── footer { last_hash, total_entries, ended_at }
```

Append-only, length-prefixed framing with checksums. One file per session.
`~/.local/share/yetty/sessions/<uuid>.ylog`.

### Replay & resume

A yetty boot can pick a `.ylog` and replay its entries into a fresh content
layer — each sub-renderer's `write` op already speaks the same shape as live
DCS. The result is the original screen state, instantly. The active session
can then continue appending to the same log.

### Merging multiple histories

Two sessions on different machines that both ran `vim foo.c` will produce
overlapping ranges of identical entries (text lines especially). Hash-based
dedup over the entries lets a UI ask "show me everything I've seen across all
sessions in the last week", deduplicate, and present a merged timeline.

A useful operation: **range proof**. To prove "this output was produced during
session X" you ship the range plus the chain of hashes and the final
`last_hash` signed by the session key. Tampering anywhere in the range breaks
the chain.

### Privacy

Some content is sensitive (passwords typed in shell, tokens in command
output). Two mitigations belong in the design before this ships:

- **Per-session encryption key** stored under the user's keyring; the log is
  meaningless without it.
- **Redaction marks** — the text grid can drop lines matching configured
  patterns (e.g., `password:`) before they hash into the chain. This is one of
  the few cases where it's OK for the chain to *not* see the data; the
  redaction event itself is in the chain.

### What this lets us build later

- `yetty-history` CLI that searches across all sessions.
- "Open this scrollback range in a new pane" — load a `.ylog` slice as a
  read-only terminal.
- Sync sessions between machines through a plain file sync (Dropbox,
  Syncthing, git-annex). The hash chain detects divergence.
- Compact a long session by dropping bodies but keeping hashes — you still
  have a verifiable summary of "what I did" without the raw content.

### Cost concerns

- Hashing every line: BLAKE3 over short bodies is sub-microsecond on a modern
  CPU. Not the bottleneck.
- Disk: a busy session is maybe 10 MB/h text. A multi-day session fits on any
  laptop.
- Latency: all writes are append-only, fsync-batched per N entries or per N
  ms. Never on the render path.

The chain doesn't change anything about how the content layer renders *now*.
It's a parallel sink: every event a sub-renderer accepts, it also forwards (in
a background-thread queue) to the log writer. Rendering stays oblivious to
persistence.

---

## Pointers

- Layer base interface + ops: `include/yetty/yterminal/terminal.h`
  (`struct yetty_yrender_terminal_layer`, `struct yetty_yterminal_layer_ops`)
- Content layer (the single layer; text↔ydraw cross-wiring):
  `src/yetty/yvterm/content-layer.c`
  (`content_on_layer_scroll`, `content_on_layer_cursor`,
  `content_on_alt_screen`, `content_on_clear_screen`,
  `content_layer_set_view_top`, `content_layer_render`)
- Scroll plumbing: `text-layer.c::text_layer_scroll`,
  `ydraw-content.c::ydraw_content_scroll` / `on_canvas_scroll`
- Alt-screen wiring: `text-layer.c::on_settermprop` (`VTERM_PROP_ALTSCREEN`),
  `ydraw-content.c::ydraw_content_set_alt_screen`
- Scrollback view: `terminal.c::terminal_push_view_top`,
  `terminal_scrollback_apply`, `terminal_scrollback_exit`
- Text scrollback arena: `src/yetty/yvterm/text-scrollback.c`
- Wire codes: `include/yetty/yterminal/dcs-codes.h` (ydraw / compositor /
  RPC payloads), `include/yetty/yterminal/osc-codes.h` and
  `include/yetty/yterminal/client-input.h` (short control / client-input)
- Compositor figures (the model the persisted-history idea would reuse):
  `src/yetty/yfigure/container.c`, `include/yetty/yfigure/wire.h`
