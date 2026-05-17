# Terminal Layers — Scrolling & Alt-Screen

This document covers the *dynamic* state model shared by yetty's three terminal
layers: how content scrolls, how alt-screen works, and how scrollback could
eventually be persisted to disk as a verifiable, mergeable history. It
complements `layered-rendering.md` (which covers the GPU pipeline) and
`terminal-screen.md` (which covers compositing).

The layers are:

| Layer | Owns | Driven by |
|---|---|---|
| **text-layer**   | one `VTerm` (libvterm) — primary + alt buffers, scrollback, cursor | PTY bytes (text + CSI/OSC) |
| **ydraw-layer** | a `ydraw_canvas` (rolling-row primitive grid) | OSC `600000-600003` |
| **ymgui-layer**  | a registry of *cards* (placed ImGui sub-regions) | OSC `610000-610004` |

All three are siblings, instantiated once per terminal in `terminal.c`. They
share callbacks so scroll, cursor moves, and alt-screen toggles propagate
between them.

---

## 1. Scroll model: rolling rows

### The problem

A naive scroll-on-line-add costs O(lines × primitives) per scroll event. With
thousands of cells filling and the user holding `j` in vim, that's not viable.

### The trick

Every layer that holds anchored content (ydraw, ymgui) addresses lines by an
**absolute monotonic counter** — the *rolling row*. Lines never move; the
viewport's idea of "row 0 on screen" advances.

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

A primitive (or ymgui card) placed at the cursor stores `rolling_row =
row0_absolute + cursor_row`. On screen its y-pixel is
`(rolling_row - row0_absolute) * cell_h`. **Scroll is a single counter bump**;
no per-primitive update.

### Cross-layer propagation

When one layer scrolls, every other layer must follow so anchors stay aligned:

```
text-layer (libvterm: line falls off top)
   ↓ scroll_callback(lines=1)
terminal_scroll_callback
   ↓ for each layer != source:
     layer.ops->scroll(layer, 1)   // bumps row0_absolute
```

`in_external_scroll` flag on each layer prevents the propagation from
ping-ponging back. Same shape for cursor moves
(`cursor_callback`/`set_cursor`).

### Scrollback view (tmux-style)

Mouse-wheel up enters scrollback. The terminal pins a `view_top_total_idx`
(absolute row index, layer-agnostic), pushes it to every layer via
`set_view_top(active=1, view_top_total_idx)`. Each layer freezes its display
at that absolute row even as live content keeps arriving below. Pressing
Enter or scrolling past the live anchor exits — `set_view_top(active=0)`
returns to live tracking.

### Lifetime today

Scrollback is held in RAM:

| Layer | Storage |
|---|---|
| text-layer  | libvterm `sb_pushline` callback feeds a per-terminal ring of `VTermScreenCell` rows |
| ydraw-layer | `canvas->lines` deque keyed by rolling row; primitives stay alive until the line drops off the front |
| ymgui-layer | each card holds its last frame mesh + atlas; cards drop only on `CARD_REMOVE`/`CLEAR` |

Memory grows monotonically until the ring caps. Nothing is persisted. A
yetty crash or restart loses the entire history.

---

## 2. Alt-screen

DEC modes `?1049` / `?1047` / `?47` ask the terminal to swap to a separate
screen buffer (vim, less, mc, top, htop). On exit the original screen
returns intact. Every layer must follow — otherwise the user's vim session
sees stray ydraw plots from the prior shell, or sees empty ymgui where it
just left a Dear ImGui app.

### Hook chain

```
PTY byte stream
   ↓
libvterm parser → settermprop(VTERM_PROP_ALTSCREEN, bool)
   ↓
text-layer.on_settermprop:
   • libvterm has already swapped its own buffer pointer
   • refresh rs.buffers[0] to the new buffer
   • fire layer.alt_screen_fn(active)
   ↓
terminal_alt_screen_callback:
   • for each layer: layer.ops->set_alt_screen(active)
   • request_render
```

### Per-layer save/restore

| Layer | Implementation |
|---|---|
| text-layer  | libvterm owns the swap (primary + alt VTermScreenBuffer); we just refresh the GPU buffer pointer |
| ydraw-layer | lazy-build a sibling `ydraw_canvas`; toggle = swap `canvas` ↔ `saved_canvas` |
| ymgui-layer | swap `cards` array ↔ `saved_cards` (atlases, buffers, bind groups travel with the pointers, no GPU work at toggle time) |

ymgui drops focus across the boundary (a `FOCUS-lost` is emitted to the
prior client). Focus is a transient runtime fact — the next client click
re-establishes it on the now-active card.

### What a layer does NOT do

- No data copy. Both halves coexist in their fully-initialized form.
- No GPU re-upload. WebGPU resources outlive the swap.
- No re-resize. Both halves track the same `grid_size` / `cell_size`.

The cost of a toggle is one `set_alt_screen` per layer plus a render.

---

## 3. Future: scrollback-as-history

Today, scrollback evaporates with the process. There's a natural extension:
treat the rolling-row stream as an **append-only event log**, persistable
to disk, content-addressable, and mergeable.

### Why "blockchain-like"

Not a blockchain in the consensus sense — there's no network, no
adversaries, no proof-of-work. What we *do* want from that family:

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
    uint8_t   layer_id;         // 0=text, 1=ydraw, 2=ymgui
    uint8_t   kind;             // text-line / prim-add / frame / clear / ...
    uint16_t  flags;
    uint32_t  body_len;
    uint8_t   body[];           // text bytes / serialized prim / wire frame
    uint8_t   prev_hash[32];    // chain link
    uint8_t   self_hash[32];    // H(prev_hash || rolling_row || layer_id || kind || body)
};
```

Each layer becomes a writer:

| Layer | What it writes |
|---|---|
| text-layer  | each line popped from libvterm's scrollback (already has a clean event in `sb_pushline`) |
| ydraw-layer | each primitive add / line drop |
| ymgui-layer | card lifecycle + each frame's mesh+atlas (or just the *card_id, hash-of-frame* for dedup) |

### File format

```
session.ylog
├── header { magic, version, session_uuid, started_at, cell_size, … }
├── entry 0
├── entry 1
…
└── footer { last_hash, total_entries, ended_at }
```

Append-only, line-oriented framing (length-prefixed with checksums). One
file per session. `~/.local/share/yetty/sessions/<uuid>.ylog`.

### Replay & resume

A yetty boot can pick a `.ylog` and replay its entries into fresh layers —
each layer's `write` op already speaks the same shape as live OSC. The
result is the original screen state, instantly. The active session can
then continue appending to the same log.

### Merging multiple histories

Two sessions on different machines that both ran `vim foo.c` will produce
overlapping ranges of identical entries (text lines especially). Hash-based
dedup over the entries lets a UI ask "show me everything I've seen across
all sessions in the last week", deduplicate, and present a merged timeline.

A useful operation: **range proof**. To prove "this output was produced
during session X" you ship the range plus the chain of hashes and the
final `last_hash` signed by the session key. Tampering anywhere in the
range breaks the chain.

### Privacy

Some content is sensitive (passwords typed in shell, tokens in command
output). Two mitigations belong in the design before this ships:

- **Per-session encryption key** stored under the user's keyring; the
  log is meaningless without it.
- **Redaction marks** — text-layer can drop lines matching configured
  patterns (e.g., `password:`) before they hash into the chain. This is
  one of the few cases where it's OK for the chain to *not* see the
  data; the redaction event itself is in the chain.

### What this lets us build later

- `yetty-history` CLI that searches across all sessions.
- "Open this scrollback range in a new pane" — load a `.ylog` slice as
  a read-only terminal.
- Sync sessions between machines through a plain file sync (Dropbox,
  Syncthing, git-annex). The hash chain detects divergence.
- Compact a long session by dropping bodies but keeping hashes — you
  still have a verifiable summary of "what I did" without the raw content.

### Cost concerns

- Hashing every line: BLAKE3 over short bodies is sub-microsecond on a
  modern CPU. Not the bottleneck.
- Disk: a busy session is maybe 10 MB/h text. A multi-day session fits
  on any laptop.
- Latency: all writes are append-only, fsync-batched per N entries or
  per N ms. Never on the render path.

The chain doesn't change anything about how layers render *now*. It's a
parallel sink: every event a layer accepts, it also forwards (in a
background-thread queue) to the log writer. Layers stay oblivious to
persistence.

---

## Pointers

- Layer base interface: `include/yetty/yterm/terminal.h`
- Scroll plumbing: `terminal.c::terminal_scroll_callback`, `*_layer_scroll`
- Alt-screen wiring: `terminal.c::terminal_alt_screen_callback`,
  `text-layer.c::on_settermprop` (`VTERM_PROP_ALTSCREEN`),
  `ydraw-layer.c::ydraw_layer_set_alt_screen`,
  `ymgui-layer.c::ymgui_set_alt_screen`
- Wire format for ymgui cards (the model the persisted-history idea would
  reuse): `include/yetty/ymgui/wire.h`
