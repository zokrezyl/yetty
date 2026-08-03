# Layered Rendering Architecture

## Overview

The name "layered rendering" is historical, but the current architecture no
longer exposes a fixed stack of terminal-owned layers. Yetty now uses **virtual
layering**:

1. `yui` paints the pane background.
2. The terminal paints one **content layer**: the libvterm text grid, the
   row-anchored ydraw canvas, and the text-owned shader-glyph figure.
3. The terminal paints the root **`yfigure` container** last. That container is a
   generic compositor: figures have bounds, dirty state, GPU resources, and
   z-order, and figures may contain other figures.

Everything paints **directly into one shared render target**. There are no
per-layer intermediate textures and no terminal blend pass. Each render pass uses
`LoadOp_Load`, and the target is wiped once per frame by the root event handler's
global `clear()`.

```
yui pane background ─────────────────────────────────────► target

terminal_render_frame(terminal, target, force)
    │
    ├── content layer ───────────────────────────────────► target (LoadOp_Load)
    │     ├── text grid         libvterm cells / cursor / selection
    │     ├── ydraw canvas      row-anchored SDF/MSDF primitives
    │     └── shader-glyph fig  text-owned figure pass
    │
    └── root yfigure container ─────────────────────────► target (LoadOp_Load)
          └── z-ordered figures: ygui, ymgui, yrdawn, yscene,
                                 yplot, yimage, yvideo, ...
```

The terminal still has a C compatibility/base interface named
`yetty_yrender_terminal_layer`; today `terminal->layer` points to exactly one
`yetty_yvterm_content_layer`. Treat that as an implementation detail around the
content renderer, not as the old public model of sibling terminal layers.

**Why direct-to-target.** The previous model rendered each layer to its own
texture and blended them. At 4K that was 4 x ~33 MB layer render targets plus a
~33 MB blend output read and written every frame, enough to starve the display
compositor on tvOS. Painting everything straight into the shared target removes
that bandwidth cost.

**Why `LoadOp_Load` everywhere.** `loadOp` ignores the scissor rect, so a `Clear`
on any pane pass would wipe every other pane sharing the big target. The only
wipe is the one global clear before panes render.

---

## Surfaces

| Surface | What it is |
|---|---|
| **pane background** | Painted by `yui` / tile chrome, not by the terminal. It provides the opaque per-pane wipe on a shared target. |
| **content layer** | One `yetty_yvterm_content_layer` owned by `yterminal`. It internally renders the text grid, ydraw canvas, and shader-glyph figure. |
| **root `yfigure` container** | The generic compositor for rich content. Figures are positioned in pane pixels and composed by z-order after the content layer. |

`ymgui`, `yrdawn`, `yscene`, `ygui`, and other rich views are figures, not
terminal layers. `ymgui-layer.c` and `yrdawn-layer.c` are gone. A visual object
that should anchor to terminal rows belongs in the ydraw canvas; a visual object
that should float above terminal content belongs in the root `yfigure` tree.

---

## The Content Layer

The content layer is implemented by `src/yetty/yvterm/content-layer.c`. It embeds
the terminal-layer base as its first member so the terminal can call a single
render/resize/input interface, but it owns two sub-renderers:

| Sub-renderer | Owns | Driven by |
|---|---|---|
| **text grid** (`text-layer.c`) | one `VTerm`, primary + alt buffers, scrollback arena, cursor, selection | PTY bytes: text + CSI/OSC |
| **ydraw canvas** (`ydraw-content.c`) | scrolling `ydraw_canvas`, keyed by rolling rows | DCS `600000`-`600003` |

Both sub-renderers keep their own GPU resource set and shader. The content
layer's render op drives them into the same target, bottom to top:

1. text grid (`text-layer.wgsl`),
2. ydraw canvas (`ydraw-layer.wgsl`), unless the new-OSC text-only mode skips it,
3. shader-glyph figure owned by the text grid.

State that used to round-trip through terminal-wide layer broadcasts now lives
inside the content layer: scroll, cursor, alt-screen, clear, selection, view-top,
resize, and visual zoom.

---

## Root Figures

The root `yfigure` container is outside the content layer. It receives compositor
records over DCS `630000` (`YETTY_DCS_YCOMPOSITOR_BIN`) and owns the figure tree.
Each figure has its own dirty state and can contribute GPU resources through the
same binder model as other renderable components.

Root-container figures are compositor-positioned in pane pixels. They do **not**
automatically participate in terminal row scrolling or alt-screen save/restore.
That is deliberate: the row-scrolling model belongs to the content layer. If a
producer wants content to scroll with terminal rows, it emits ydraw primitives or
a row-anchored drawable. If it wants an overlay or application surface, it emits
a figure.

Bulk drawing and figure payloads ride DCS envelopes because they are large opaque
payloads that terminal multiplexers can pass through verbatim. Short
control/metadata, such as client input subscription and delivery, remains OSC.
See `src/yetty/ywire/README.md` for the code table.

---

## Frame Loop

`terminal_render_frame()` in `src/yetty/yterminal/terminal.c` does one dirty scan
and two top-level paints:

```c
int force = force_redraw;
struct yetty_yrender_terminal_layer *layer = terminal->layer;

if (!force && layer->ops->is_dirty && layer->ops->is_dirty(layer))
    force = 1;

if (!force && root_container_is_dirty(terminal))
    force = 1;

layer->ops->render(layer, target, force);

yetty_yfigure_render(root_container_as_figure, target);
root_container->dirty = 0;
```

A dirty root figure forces the content layer to repaint underneath before the
figure tree composites again. Otherwise, pixels vacated by a moved or deleted
semi-transparent figure could survive on the shared target.

---

## Render Target

A render target is the destination the content layer or a figure paints into. It
is owned by `yframework` (`yetty_yframework.render_target`) and handed to the
terminal each frame.

The target interface lives in `include/yetty/yrender/render-target.h` and is
implemented under `src/yetty/yrender/`:

- `render-target-texture.c` - offscreen texture target,
- `render-target-vnc.c` - VNC server framebuffer,
- `render-target-x11-tile.c` - X11 tiled output,
- surface target - on-screen window / `WGPUSurface`.

The `blend` op remains in the target interface for offscreen composition use
cases, but the terminal render path does not use it.

---

## GPU Resource Binding

Renderable components expose a `struct yetty_yrender_gpu_resource_set`: buffers,
textures, uniforms, shader code, and child resource sets. The
`gpu-resource-binder` flattens the tree and packs it into:

- one storage buffer,
- per-format atlas textures,
- one uniform block,
- generated WGSL binding declarations, offsets, and atlas regions.

This lets terminal content and figures share one binding strategy without being
hard-coded into a fixed layer stack. See `docs/gpu-resource-binding.md` and
`src/yetty/yrender/README.md` for binder details.

---

## Scroll Model: Rolling Rows

A naive scroll-on-line-add costs O(lines x primitives) per scroll event. With
thousands of cells filling and the user holding `j` in vim, that is not viable.
The ydraw canvas addresses lines by an **absolute monotonic counter**: the
rolling row. Lines never move; the viewport's idea of "row 0 on screen" advances
through `ydraw_rolling_row_0`, a u32 uniform set by
`ydraw-content.c::set_rolling_row_0`.

```
                rolling rows (monotonic, never decrement)
                      ^
   primary:  17 18 19 20 21 22 23 24 25 26 27 28 ...
                         ^
                  row0_absolute = 21
```

A primitive placed at the cursor stores `rolling_row = row0_absolute +
cursor_row`. On screen its y pixel is `(rolling_row - row0_absolute) * cell_h`.
Scroll is a single counter bump; no primitive rewrites its coordinates.

### Cross-Renderer Propagation

When one content sub-renderer scrolls, the other must follow so text and ydraw
anchors stay aligned. This is internal to the content layer:

```
text grid (libvterm line falls off top)
   -> scroll_fn(content_on_layer_scroll)
content-layer.c
   -> for each sub-renderer != source:
        sub->ops->scroll(sub, lines)
```

The `in_external_scroll` flag prevents ping-pong. Cursor moves follow the same
shape through `content_on_layer_cursor` and each sub-renderer's `set_cursor` op.
The terminal no longer owns scroll/cursor broadcast callbacks.

### Scrollback View

Mouse-wheel up or PageUp enters scrollback. The terminal pins a
`view_top_total_idx` absolute row index and pushes it to the content layer via
`set_view_top(active, view_top_total_idx)`. The content layer fans it out to the
text grid and ydraw canvas, each of which freezes its display at that absolute
row while live content keeps arriving below.

The index is stable because text and ydraw share the same live anchor. The
content layer's `get_live_anchor` returns the max across sub-renderers as a safe
fallback. Pressing Enter, typing, or scrolling past the live anchor exits back to
live tracking.

Scrollback is held in RAM today. Text scrollback is stored by the per-terminal
cells/lines ring arena fed by libvterm's `sb_pushline` callback. The ydraw canvas
keeps primitives keyed by rolling row until their line drops off the front.
Nothing is persisted across restart.

---

## Alt-Screen

DEC modes `?1049`, `?1047`, and `?47` ask the terminal to swap to a separate
screen buffer. The text grid and ydraw canvas must switch together so a fullscreen
program such as vim does not inherit row-anchored drawings from the shell.

```
PTY byte stream
   -> libvterm settermprop(VTERM_PROP_ALTSCREEN, bool)
   -> text-layer.c::on_settermprop
   -> content-layer.c::content_on_alt_screen
   -> each sub-renderer set_alt_screen(active)
```

The text grid lets libvterm own the primary/alt buffer swap and refreshes the GPU
cell buffer pointer. The ydraw canvas lazily builds a sibling `ydraw_canvas` on
the first toggle, then swaps `canvas` and `saved_canvas`.

No data copy, GPU re-upload, or re-resize is required. Both halves coexist and
track the same grid and cell size. Root-container figures are outside this model
and are not saved/restored by alt-screen toggles; a producer that wants a figure
to disappear under vim removes or stops emitting that figure.

Full-screen erase (`CSI 2J` / `CSI 3J`) follows the same content-layer routing:
the text grid's clear hook lands in `content_on_clear_screen`, which forwards to
each sub-renderer's `clear_screen` op.

---

## Future: Persistent Scrollback

> Status: exploratory design note, not implemented.

The rolling-row event stream could become an append-only history log. Each event
a sub-renderer accepts could also be forwarded to a background log writer:

```c
struct entry {
    uint64_t rolling_row;
    uint8_t  source_id;   // text grid, ydraw, figure
    uint8_t  kind;        // text-line, prim-add, frame, clear, ...
    uint16_t flags;
    uint32_t body_len;
    uint8_t  body[];
    uint8_t  prev_hash[32];
    uint8_t  self_hash[32];
};
```

The useful properties are append-only history, hash-linked tamper detection,
content addressing, Merkle-friendly range proofs, and deduplication across
sessions. This should include privacy design before implementation: per-session
encryption keys and explicit redaction events for sensitive content.

This would not change the render path. Persistence would be a parallel sink fed
from accepted content events.

---

## File Index

```
include/yetty/yterminal/terminal.h       terminal view + content-layer base ops
src/yetty/yterminal/terminal.c           terminal_render_frame, scrollback, input forwarding
include/yetty/yvterm/content-layer.h     content-layer public constructor / wire registration
src/yetty/yvterm/content-layer.c         text <-> ydraw cross-wiring and content render
src/yetty/yvterm/text-layer.c            libvterm grid, scrollback, alt-screen hooks
src/yetty/yvterm/ydraw-content.c         rolling ydraw canvas, row anchors, alt-screen canvas swap
src/yetty/yfigure/container.c            root figure compositor and wire consumer
include/yetty/yfigure/wire.h             compositor figure record format
include/yetty/yterminal/dcs-codes.h      ydraw / compositor / RPC DCS codes
include/yetty/yterminal/osc-codes.h      OSC namespace note
include/yetty/yterminal/client-input.h   client-input OSC payloads
```

---

## Future Extensions

- Multi-target rendering.
- Figure-level post-processing effects.
- Dirty regions / partial target repaint.
- Persistent scrollback history.
