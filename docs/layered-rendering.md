# Layered Rendering Architecture

## Overview

A terminal is drawn as a stack of **layers** plus a **figure container**, all
painting **directly into one shared render target**. There are no per-layer
intermediate textures and no separate blend pass: each layer draws into the
target with `LoadOp_Load` (so pixels persist), and the target is wiped once per
frame by a single global `clear()`. The root `yfigure` container renders **last**,
on top of the layers.

```
yui paints the pane background ──► target   (the terminal owns NO background layer)

terminal_render_frame(terminal, target, force)
    │  (target already cleared once this frame by the root event handler)
    │
    ├── for each layer in terminal->layers[], bottom → top:  ──► target  (LoadOp_Load)
    │       text-layer  →  ydraw-scrolling-layer
    │       layer->ops->render(layer, target, force)
    │
    └── root yfigure container renders LAST                  ──► target  (figures on top)
                                                                  │
                                                                  ▼
                                             surface / texture / VNC / X11-tile
```

The terminal's `layers[]` holds exactly two layers — **text-layer** and
**ydraw-scrolling-layer**. The pane background is painted by `yui` (the
tab/pane chrome), not by the terminal; there is no terminal background layer.

**Why direct-to-target (no blend round-trip).** The previous model rendered each
layer to its own texture and blended them. At 4K that is 4 × ~33 MB layer render
targets plus a ~33 MB blend output read and written every frame — enough to
starve the display compositor (observed on tvOS). Painting every layer straight
into the shared target removes that bandwidth entirely.

**Why `LoadOp_Load` everywhere.** Because `loadOp` ignores the scissor rect, a
`Clear` on any layer pass would wipe *every* pane sharing the big target. So no
layer pass ever clears; the only wipe is the one global `clear()` the root event
handler issues before the panes render.

---

## Layers vs figures

| Surface | What it is |
|---|---|
| **pane background** | Painted by **`yui`** (the tab/pane chrome), *not* a terminal layer. The terminal never owns a background layer. |
| **text-layer, ydraw-scrolling-layer** | The two `yetty_yrender_terminal_layer`s in `terminal->layers[]`, painted bottom→top directly into the target. text-layer hosts libvterm; the ydraw scrolling layer holds SDF primitives. |
| **figure container** (`yfigure`) | The root container, *outside* `layers[]`. Hosts the rich-content **figures** (ygui, ymgui, yrdawn, ygrid, yplot, …) and renders after the layers — it is the compositor for figures. |

Shader-glyph, ymgui, and yrdawn used to be layers; they are now figures in the
container, so `layers[]` is down to just text + ydraw-scrolling. See
[ydraw](../src/yetty/ydraw/README.md) for the primitive/figure model and
[Terminal Layers](term-layers.md) for the scroll/alt-screen state shared across
both.

---

## The render target (`render-target.h`)

A render target is the destination a layer or figure paints into. It is owned by
`yframework` (`yetty_yframework.render_target`) and handed to the terminal each
frame.

```c
struct yetty_yrender_target_ops {
    void (*destroy)(struct yetty_ydraw_target *self);
    struct yetty_ycore_void_result (*clear)(struct yetty_ydraw_target *self);  /* once/frame */
    struct yetty_ycore_void_result (*render_layer)(struct yetty_ydraw_target *self,
                                                   struct yetty_yrender_terminal_layer *layer);
    struct yetty_ycore_void_result (*blend)(struct yetty_ydraw_target *self,
                                            struct yetty_ydraw_target **sources, size_t count);
    struct yetty_ycore_void_result (*present)(struct yetty_ydraw_target *self);
    WGPUTextureView (*get_view)(const struct yetty_ydraw_target *self);
    WGPUTexture (*get_texture)(const struct yetty_ydraw_target *self);
    struct yetty_ycore_void_result (*resize)(struct yetty_ydraw_target *self,
                                             struct yetty_yrender_viewport viewport);
    struct yetty_ycore_void_result (*set_visual_zoom)(struct yetty_ydraw_target *self,
                                                      float scale, float ox, float oy);
};
```

The type is `struct yetty_ydraw_target`. A layer's `render` op ultimately calls
the target's `render_layer` (which begins a `LoadOp_Load` pass and draws into the
target's view). `present()` outputs the finished frame; `get_texture()` /
`get_view()` expose the backing texture for readback.

Implementations (in `src/yetty/yrender/`):

- **surface target** — on-screen window (WGPUSurface).
- **texture target** (`render-target-texture.c`) — offscreen render-to-texture.
- **VNC target** (`render-target-vnc.c`) — VNC server framebuffer (headless).
- **X11-tile target** (`render-target-x11-tile.c`) — X11 tiled output.

> The `blend` op is part of the interface but is **not** used on the terminal
> render path today (kept for the offscreen/compose-from-sources use case). The
> older `blender.h`, `layer-renderer.h`, and `rendered-layer.h` headers likewise
> remain in `yrender` but are not on this path — the terminal allocates no
> per-layer renderer and no blender.

### Legacy: `terminal->layer_targets[]`

The terminal *does* still allocate and resize one render target per layer
(`terminal->layer_targets[]`, set up in `terminal_create` and kept in sync on
resize) — a remnant of the old render-to-texture-then-blend model.
`terminal_render_frame()` no longer uses them: it renders every layer into the
single shared target passed in. So `layer_targets[]` is dead weight on the
current direct path (nothing reads it) and is pending cleanup — documented here
so it isn't mistaken for part of the live render flow.

---

## The frame loop (two passes)

`terminal_render_frame()` (`src/yetty/yterm/terminal.c`) runs two passes over
`layers[]`:

1. **Dirty scan.** If `force_redraw` is set, or *any* layer reports
   `is_dirty()`, or the root container is dirty → set `force = 1`. A dirty
   top layer must force the layers *below* it to repaint, otherwise the dirty
   layer's old pixels (e.g. the silhouette an ymgui window just vacated) would
   survive under it.
2. **Render bottom→top.** For each layer: skip if empty (`is_empty`), else
   `layer->ops->render(layer, target, force)`. The op returns `1` iff it
   actually drew; that propagates `force = 1` upward so layers above a repaint
   also refresh.

```c
int force = force_redraw;
for (size_t i = 0; !force && i < terminal->layer_count; i++)
    if (layer_is_dirty(terminal->layers[i])) force = 1;
if (!force && root_container_dirty(terminal)) force = 1;

for (size_t i = 0; i < terminal->layer_count; i++) {
    struct yetty_yrender_terminal_layer *layer = terminal->layers[i];
    if (layer_is_empty(layer)) continue;
    struct yetty_ycore_int_result r = layer->ops->render(layer, target, force);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layer render failed");
    if (r.value == 1) force = 1;
}

/* Root container renders LAST, on top of every layer. */
struct yetty_yfigure_figure *rf = yetty_yfigure_container_as_figure(terminal->root_container);
yetty_yfigure_render(NULL, /* the figure object */ ..., target);
rf->dirty = 0;
```

**New-OSC path.** When `YGRID_USE_NEW_OSC=1` (`terminal->new_osc_path_active`),
the loop skips every legacy layer except text-layer — the figure container plus
text-layer (background + terminal text) are the only paints.

---

## Per-layer GPU binding

Each layer produces a `struct yetty_yrender_gpu_resource_set` (buffers, textures,
uniforms, shader, children). A `gpu-resource-binder` flattens that set, packs it
into one storage buffer + per-format atlas textures + one uniform block, and
caches the compiled pipeline keyed by a shader-code hash (recompiling only when
the shader text changes — uniform/buffer changes reuse the pipeline). That
mechanism is unchanged by the direct-to-target switch; see
[GPU Resource Binding](gpu-resource-binding.md) and
[Render Pipeline](render.md) for the binder flow and the dirty-driven upload.

---

## File Structure

```
include/yetty/yrender/
├── render-target.h          # target interface (yetty_ydraw_target)
├── render-target-x11-tile.h # X11-tile target
├── gpu-resource-set.h       # resource set tree
├── gpu-resource-binder.h    # binder interface
├── gpu-allocator.h          # GPU allocation tracking
├── pipeline.h               # shared pipeline (two-tier binder)
├── primitive-gpu-binder.h   # SDF primitive binder
├── font-dispatcher.h        # glyph → atlas dispatch
├── texture-format.h         # format helpers
├── types.h                  # buffer / texture / uniform types
└── blender.h, layer-renderer.h, rendered-layer.h   # legacy — not on the current path

src/yetty/yrender/
├── render-target-texture.c  # offscreen texture target (the LoadOp_Load render_layer impl)
├── render-target-vnc.c      # VNC server buffer target
├── render-target-x11-tile.c # X11-tile target
├── gpu-resource-binder.c    # flatten / pack / codegen / upload
├── gpu-allocator.c          # allocation tracking
├── pipeline.c               # shared pipeline
├── primitive-gpu-binder.c   # SDF primitive binder
├── font-dispatcher.c        # glyph dispatch
└── types.c                  # type utilities

src/yetty/yterm/terminal.c   # terminal_render_frame — the loop above
src/yetty/yfigure/container.c# the root container (compositor) rendered last
```

---

## Future Extensions (Out of Scope)

- **Multi-target** — render to multiple targets simultaneously.
- **Layer effects** — per-layer post-processing.
- **Dirty regions** — partial re-rendering instead of whole-target repaint.
