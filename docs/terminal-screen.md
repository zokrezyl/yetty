# Terminal Screen

How a single terminal turns PTY bytes and OSC envelopes into a composited frame.
This is the high-level orchestration view; the mechanisms it references each have
a dedicated doc.

> This document previously described an early C++ design (`TerminalScreen`,
> `RenderableLayer`, a 4-layer cards model). The shipping architecture is pure C
> and is split across the docs linked below; this page is now a short map into
> them.

---

## What a terminal owns

A `struct yetty_yterm_terminal` (`include/yetty/yterm/terminal.h`) owns:

- **Layers** — a small ordered list of `yetty_yrender_terminal_layer`:
  - **text-layer** — one `VTerm` (libvterm): primary + alt buffers, scrollback,
    cursor. Driven by PTY bytes (text + CSI/OSC).
  - **ydraw-scrolling-layer** — a ydraw canvas of SDF/MSDF primitives anchored to
    the grid. Driven by OSC drawing commands.
  - (the host pane supplies a background layer beneath these.)
- **A figure container** — a `yfigure` root container that hosts the rich content
  (ygui, ymgui, yrdawn canvases, ygrid, yplot, …) and renders **after** the
  layers. This is the compositor for figures.
- **A wire state machine** — `ywire` framer/dispatcher that routes incoming
  OSC/DCS envelopes to the text layer, the ydraw layer, or a figure.

Each layer/figure has a **dirty flag**; the terminal renders only what changed
(see [Render Pipeline](render.md)).

---

## Rendering a frame

```
PTY bytes ─┬─ text → libvterm → text-layer (dirty)
           └─ OSC/DCS → ywire dispatch ─┬─ ydraw-layer (dirty)
                                        └─ figure factory → figure in container (dirty)

per frame:  for each layer: renderer renders to texture if dirty (else cached)
            blender composites layer textures → render target
            figure container composites its figures on top
            present (or copy to VNC / readback when headless)
```

- **Layer → texture → blend** is the `yrender` pipeline:
  [Layered Rendering](layered-rendering.md).
- **Resource packing** (one storage buffer + per-format atlases + one uniform
  block, generated WGSL): [GPU Resource Binding](gpu-resource-binding.md).
- **Object ownership** (device/queue/allocator/render target):
  [WebGPU Architecture](webgpu-architecture.md).

---

## Scrolling and the cell buffer

The text grid is a flat, contiguous cell buffer sized `rows × cols`, kept in a
GPU-friendly layout so it uploads with no per-row copying. A scroll is a single
`memmove` of the visible region plus a cleared last row; libvterm's
`sb_pushline` callback feeds the scrollback ring.

Anchored content (ydraw primitives, figures) does **not** move on scroll. Each
item stores the **rolling row** it was created at; the shader subtracts the
current top rolling-row to find its screen position. Scroll is therefore O(1) —
no primitive ever rewrites its coordinates. The full model, including
cross-layer scroll propagation and alt-screen save/restore, is in
[Terminal Layers](term-layers.md); the primitive-side detail is in
[ydraw](ydraw.md).

---

## Pointers

- Terminal + layers: `include/yetty/yterm/terminal.h`,
  `src/yetty/yterm/terminal.c`
- Text layer: `src/yetty/yterm/text-layer.c`
- ydraw layer: `src/yetty/yterm/ydraw-layer.c`
- Figure container: `src/yetty/yfigure/container.c`
