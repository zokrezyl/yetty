# Layered Rendering Architecture

## Overview

Terminal rendering is split into multiple layers (text, ydraw), each rendering to
its own texture. A blender composites the layer textures to a render target. On
top of the legacy layers, a **figure container** (the compositor) renders the
rich content — see "Layers vs figures" below.

**Key principles:**
- **Simple orchestration** - the terminal iterates layers and calls the blender
- **Complex logic in `yrender/`** - rendering and blending logic lives in
  `src/yetty/yrender/`
- **Blender owns target** - Target is encapsulated, can be changed at runtime
- **1:1 renderer-layer coupling** - Each layer has its dedicated renderer
- **Renderer owns binder** - Binder caches compiled shader/pipeline

> ## Layers vs figures
>
> This document describes the **layer** pipeline: the `yrender` machinery that
> renders a `yetty_yrender_terminal_layer` to a texture and blends it. Today the
> terminal instantiates two such layers — **text-layer** (libvterm) and
> **ydraw-scrolling-layer** (SDF primitives) — plus a background layer supplied
> by the host pane.
>
> Most *rich content* (ygui, ymgui, yrdawn canvases, ygrid, yplot, …) no longer
> lives in a layer. It lives as a **figure** inside a `yfigure` root container
> that the terminal renders **after** the legacy layers. That container is the
> de-facto compositor for figures; "blender" below is the lower-level GPU stage
> that composites layer textures. See [ydraw](ydraw.md) for the figure/primitive
> model and [Terminal Layers](term-layers.md) for the scroll/alt-screen state
> shared across both worlds.

---

## Architecture

```
terminal.c (simple orchestrator)
    │
    ├── layers[]      ─────────────────────────────────────┐
    │   ├── text_layer                                     │
    │   └── ydraw_layer                                   │
    │                                                      │ 1:1
    ├── renderers[]   ←────────────────────────────────────┘
    │   ├── text_renderer (owns: binder, texture, layer ref)
    │   └── ydraw_renderer (owns: binder, texture, layer ref)
    │
    └── blender->ops->blend(rendered_layers[], count)
                │
                ▼
            render_target (owned by blender)
                │
                ▼
            screen / VNC / other
```

---

## Components (src/yetty/yrender/)

### 1. render_target

Abstraction for render output. Owned by blender.

```c
struct yetty_yrender_target_ops {
    void (*destroy)(struct yetty_yrender_target *self);
    WGPUTextureView (*acquire)(struct yetty_yrender_target *self);
    void (*present)(struct yetty_yrender_target *self);
    uint32_t (*get_width)(const struct yetty_yrender_target *self);
    uint32_t (*get_height)(const struct yetty_yrender_target *self);
};
```

Implementations (in `src/yetty/yrender/`):
- **surface target** - local window (WGPUSurface), the on-screen path
- **texture target** (`render-target-texture.c`) - offscreen render-to-texture
- **VNC target** (`render-target-vnc.c`) - VNC server framebuffer (headless)
- **X11-tile target** (`render-target-x11-tile.c`) - X11 tiled output

### 2. rendered_layer

Opaque handle for a layer rendered to texture.

```c
struct yetty_yrender_rendered_layer_ops {
    void (*release)(struct yetty_yrender_rendered_layer *self);
    WGPUTextureView (*get_view)(const struct yetty_yrender_rendered_layer *self);
    uint32_t (*get_width)(const struct yetty_yrender_rendered_layer *self);
    uint32_t (*get_height)(const struct yetty_yrender_rendered_layer *self);
};
```

### 3. layer_renderer

Renders a terminal_layer to texture. **Created with a layer reference (1:1 coupling).**

```c
struct yetty_yrender_layer_renderer_ops {
    void (*destroy)(struct yetty_yrender_layer_renderer *self);

    struct yetty_yrender_rendered_layer_result (*render)(
        struct yetty_yrender_layer_renderer *self,
        struct yetty_yterm_terminal_layer *layer);

    void (*resize)(struct yetty_yrender_layer_renderer *self,
                   uint32_t width, uint32_t height);
};

/* Create renderer with layer reference */
struct yetty_yrender_layer_renderer_result yetty_yrender_layer_renderer_create(
    WGPUDevice device,
    WGPUQueue queue,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height);
```

**Renderer owns:**
- **Binder** - Caches compiled shader and pipeline. Reuses if shader code unchanged.
- **Intermediate texture** - Layer renders to this, blender reads from it.
- **Layer reference** - The renderer is bound to one layer for its lifetime.

**Dirty flag optimization:**
- `render()` calls `layer->ops->is_dirty()` first
- If not dirty, returns cached rendered_layer immediately (no GPU work)
- If dirty, calls `layer->ops->get_gpu_resource_set()` which clears the dirty flag

### 4. blender

Composites layers to target. **Owns the render target.**

```c
struct yetty_yrender_blender_ops {
    void (*destroy)(struct yetty_yrender_blender *self);

    struct yetty_ycore_void_result (*blend)(
        struct yetty_yrender_blender *self,
        struct yetty_yrender_rendered_layer **layers,
        size_t layer_count);

    void (*set_target)(
        struct yetty_yrender_blender *self,
        struct yetty_yrender_target *target);
};
```

Target ownership:
- Blender takes ownership of target
- `set_target()` destroys old target, takes new one
- Enables runtime target switching (screen → VNC)

---

## terminal.c Integration

```c
struct yetty_yterm_terminal {
    /* layers and their renderers (1:1) */
    struct yetty_yterm_terminal_layer *layers[MAX_LAYERS];
    struct yetty_yrender_layer_renderer *renderers[MAX_LAYERS];
    size_t layer_count;

    /* compositor */
    struct yetty_yrender_blender *blender;
};

static struct yetty_ycore_void_result terminal_render_frame(
    struct yetty_yterm_terminal *terminal)
{
    struct yetty_yrender_rendered_layer *rendered[MAX_LAYERS];
    size_t rendered_count = 0;

    /* Render each layer to texture (skips if not dirty) */
    for (size_t i = 0; i < terminal->layer_count; i++) {
        struct yetty_yrender_rendered_layer_result res =
            terminal->renderers[i]->ops->render(
                terminal->renderers[i],
                terminal->layers[i]);
        if (YETTY_IS_OK(res)) {
            rendered[rendered_count++] = res.value;
        }
    }

    /* Blend to target */
    return terminal->blender->ops->blend(
        terminal->blender,
        rendered,
        rendered_count);
}
```

---

## Data Flow

```
terminal_layer (text, ydraw, etc.)
       │
       │ renderer checks: layer->ops->is_dirty()
       │
       ├── NOT DIRTY ──► return cached rendered_layer (no GPU work)
       │
       └── DIRTY ──────► layer->ops->get_gpu_resource_set()
                               │         (clears dirty flag)
                               ▼
                gpu_resource_set (buffers, textures, uniforms, shader)
                               │
                               │ binder compiles pipeline (if shader changed)
                               ▼
                        intermediate texture
                               │
                               │ returned as rendered_layer
                               ▼
blender collects all rendered_layers
       │
       │ alpha-over compositing
       ▼
render_target (screen, VNC, etc.)
```

**Dirty flag lifecycle:**
1. Layer data changes → layer sets `dirty = true`
2. `render()` checks `is_dirty()` → returns cached if false
3. `get_gpu_resource_set()` called → clears `dirty = false`
4. Binder checks if shader code changed → recompiles only if needed

---

## Binder Caching

The binder (owned by renderer) caches compiled shader and pipeline:

```c
struct binder {
    WGPUDevice device;
    WGPUShaderModule shader;      /* cached */
    WGPURenderPipeline pipeline;  /* cached */
    uint32_t shader_hash;         /* hash of last compiled shader code */
};
```

**On `bind()` call:**
1. Compute hash of shader code from gpu_resource_set
2. If hash matches `shader_hash` → reuse cached pipeline
3. If hash differs → recompile shader, create new pipeline, update hash

This avoids recompilation when only buffers/uniforms change (common case).

---

## Blend Shader (blend.wgsl)

```wgsl
@group(0) @binding(0) var layer_textures: binding_array<texture_2d<f32>>;
@group(0) @binding(1) var layer_sampler: sampler;
@group(0) @binding(2) var<uniform> layer_count: u32;

@fragment
fn fs_main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
    let uv = pos.xy / vec2<f32>(target_size);

    // Start with bottom layer
    var result = textureSample(layer_textures[0], layer_sampler, uv);

    // Blend subsequent layers (alpha-over, premultiplied)
    for (var i = 1u; i < layer_count; i++) {
        let layer = textureSample(layer_textures[i], layer_sampler, uv);
        result = vec4(
            result.rgb * (1.0 - layer.a) + layer.rgb,
            result.a * (1.0 - layer.a) + layer.a
        );
    }

    return result;
}
```

---

## Runtime Target Switching

```c
/* Switch from screen to VNC */
struct yetty_yrender_target *vnc_target =
    yetty_yrender_target_vnc_create(vnc_context);

terminal->blender->ops->set_target(terminal->blender, vnc_target);
/* Old target destroyed, VNC target now owned by blender */

/* Subsequent renders go to VNC */
terminal_render_frame(terminal);
```

---

## File Structure

```
include/yetty/yrender/
├── render-target.h          # target interface
├── render-target-x11-tile.h # X11-tile target
├── rendered-layer.h         # rendered layer handle
├── layer-renderer.h         # layer → texture renderer
├── blender.h                # compositor, owns target
├── gpu-resource-set.h       # resource set tree
├── gpu-resource-binder.h    # binder interface
├── gpu-allocator.h          # GPU allocation tracking
├── pipeline.h               # shared pipeline (two-tier binder)
├── primitive-gpu-binder.h   # SDF primitive binder
├── font-dispatcher.h        # glyph → atlas dispatch
├── texture-format.h         # format helpers
└── types.h                  # buffer / texture / uniform types

src/yetty/yrender/
├── render-target-texture.c  # offscreen texture target
├── render-target-vnc.c      # VNC server buffer target
├── render-target-x11-tile.c # X11-tile target
├── gpu-resource-binder.c    # flatten / pack / codegen / upload
├── gpu-allocator.c          # allocation tracking
├── pipeline.c               # shared pipeline
├── primitive-gpu-binder.c   # SDF primitive binder
├── font-dispatcher.c        # glyph dispatch
├── types.c                  # type utilities
└── blend.wgsl               # compositing shader
```

---

## Future Extensions (Out of Scope)

- **Multi-target** - Render to multiple targets simultaneously
- **Layer effects** - Per-layer post-processing
- **Dirty regions** - Partial re-rendering

(The VNC target listed here in earlier drafts now ships —
`render-target-vnc.c`.)
