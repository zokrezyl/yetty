# yterminal — the terminal view

How a single terminal turns PTY bytes and OSC/DCS envelopes into a composited frame.
This is the high-level orchestration view; the mechanisms it references each have
a dedicated doc. `yterminal` owns the terminal object itself — PTY, scrollback,
selection, input forwarding — and composes the content layer
([yvterm](../yvterm/README.md)) with the root figure compositor
([yfigure](../yfigure/README.md)).

---

## What a terminal owns

A `struct yetty_yterminal_terminal` (`include/yetty/yterminal/terminal.h`) owns:

- **One content layer** — `terminal->layer`, implemented by
  `yetty_yvterm_content_layer`. This wraps terminal text and row-anchored ydraw
  content; it is not the old public stack of sibling layers.
  - **text grid** — one `VTerm` (libvterm): primary + alt buffers, scrollback,
    cursor. Driven by PTY bytes (text + CSI/OSC).
  - **ydraw canvas** — a scrolling canvas of SDF/MSDF primitives anchored to
    terminal rows. Driven by DCS drawing commands.
- **A root `yfigure` container** — hosts rich content (ygui, ymgui, yrdawn
  canvases, ygrid, yplot, …) and renders **after** the content layer by z-order.
- **A wire state machine** — `ywire` framer/dispatcher that routes incoming
  OSC/DCS envelopes to raw text, the ydraw canvas, yclass RPC, or the root
  `yfigure` compositor.

Each content part / figure has a **dirty flag**; the terminal renders only what
changed (see [yrender](../yrender/README.md)).

---

## Rendering a frame

```
PTY bytes ─┬─ raw text → libvterm → text grid (dirty)
           └─ OSC/DCS → ywire dispatch ─┬─ ydraw canvas (dirty)
                                        ├─ yclass RPC
                                        └─ yfigure records → root container (dirty)

per frame:  clear the shared target once (root event handler)
            render the content layer directly into the target
              (text grid, ydraw canvas, shader-glyph figure)
            render the root yfigure container LAST, by z-order
            present (or copy to VNC / readback when headless)
```

Everything paints straight into one shared target — **no per-layer intermediate
textures and no blend pass**. A single global `clear()` wipes the target each
frame; render passes use `LoadOp_Load`, so a dirty top-level figure forces the
content layer to repaint underneath before the root container composites again.

- **Direct-to-target virtual layering + yfigure z-order**:
  [Layered Rendering](../../../docs/layered-rendering.md).
- **Resource packing** (one storage buffer + per-format atlases + one uniform
  block, generated WGSL): [GPU Resource Binding](../../../docs/gpu-resource-binding.md).
- **Object ownership** (device/queue/allocator/render target):
  [WebGPU Architecture](../../../docs/webgpu-architecture.md).

---

## Scrolling and the cell buffer

The text grid is a flat, contiguous cell buffer sized `rows × cols`, kept in a
GPU-friendly layout so it uploads with no per-row copying. A scroll is a single
`memmove` of the visible region plus a cleared last row; libvterm's
`sb_pushline` callback feeds the scrollback ring.

Row-anchored ydraw content does **not** rewrite coordinates on scroll. Each
primitive stores the **rolling row** it was created at; the shader subtracts the
current top rolling-row to find its screen position. Scroll is therefore O(1).
Root-container figures are compositor-positioned and do not automatically
participate in row scrolling. The full model, including content-layer scroll
propagation and alt-screen save/restore, is in
[Layered Rendering](../../../docs/layered-rendering.md);
the primitive-side detail is in [ydraw](../ydraw/README.md).

---

## Pointers

- Terminal: `include/yetty/yterminal/terminal.h`,
  `src/yetty/yterminal/terminal.c`
- Content layer: `include/yetty/yvterm/content-layer.h`,
  `src/yetty/yvterm/content-layer.c`
- Text grid: `src/yetty/yvterm/text-layer.c`
- ydraw canvas: `src/yetty/yvterm/ydraw-content.c`
- Figure container: `src/yetty/yfigure/container.c`
