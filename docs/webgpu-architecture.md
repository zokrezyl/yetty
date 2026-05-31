# WebGPU Architecture

How yetty owns and drives WebGPU objects. This complements
[Contexts](contexts.md) (the build chain and context structs),
[GPU Resource Binding](gpu-resource-binding.md) (how buffers/textures pack into a
fixed binding set), and [Render Pipeline](render.md) (the per-frame upload loop).

Yetty uses the Dawn implementation of WebGPU through its C header
(`webgpu/webgpu.h`). The whole stack is C; there are no WebGPU C++ wrappers.

---

## Object ownership

WebGPU objects are created at the level that can own them for the right
lifetime, and handed down by value inside context structs.

| Object | Created by | Lives on |
|---|---|---|
| `WGPUInstance` | platform / yinit | `yetty_yinit_runtime`, `yinit_gpu_context` |
| `WGPUSurface` | platform / yinit | `yetty_yinit_runtime` (NULL when headless) |
| `WGPUAdapter` | yframework | `yetty_yframework_gpu_context` |
| `WGPUDevice` | yframework | `yetty_yframework_gpu_context` |
| `WGPUQueue` | yframework | `yetty_yframework_gpu_context` |
| GPU allocator | yframework | `yetty_yframework_gpu_context.allocator` |
| MSDF generator | yframework | `yetty_yframework_gpu_context.msdf_generator` |
| Render target | yframework | `yetty_yframework.render_target` |
| Pipelines / binders / buffers / atlases | each renderer | the renderer that owns them |

So **the platform creates only what needs the OS** (instance + surface), and
**yframework owns the GPU connection and the shared services**. The terminal app
and its renderers own only their own pipelines and per-frame data. See
[Contexts](contexts.md) for the exact structs and the create/teardown order.

```
platform / yinit
│   WGPUInstance, WGPUSurface
│
└── yframework  (yetty_yframework_create)
    │   WGPUAdapter, WGPUDevice, WGPUQueue
    │   GPU allocator, MSDF generator
    │   render target (surface / texture / VNC / X11-tile)
    │   event loop, wgpu await machinery, optional VNC + RPC servers
    │
    └── yetty  (yetty_create)
        └── tabs / panes / terminals
            ├── text layer    → renderer → binder → pipeline
            ├── ydraw layer    → renderer → binder → pipeline
            └── figure container (compositor) → per-figure binders/pipelines
```

---

## One device, one queue

WebGPU has one queue per device. All uploads (`wgpuQueueWriteBuffer`,
`wgpuQueueWriteTexture`) and all submits (`wgpuQueueSubmit`) go through the
single queue that yframework owns. A frame records every layer and figure into
command buffers and submits them, then presents the surface (or copies the
render target to a VNC/readback buffer in headless mode).

The render target is an abstraction owned by yframework, not hard-wired to the
surface — see [Layered Rendering](layered-rendering.md) for the target ops and
the surface / texture / VNC / X11-tile implementations.

---

## Binding model: one mega-binding, not per-view groups

Yetty does **not** use a fixed "group 0 = shared, group 1 = per-view" layout.
Instead, each renderer owns a **resource-set binder** that flattens a tree of
resource sets (layer + its fonts + nested providers) and packs everything into a
fixed, small binding set:

- one storage buffer (all component buffers concatenated, with generated offset
  constants),
- one atlas texture per format (R8, RGBA8) with generated UV-region constants,
- one uniform block (all uniforms packed per WGSL alignment).

This is what removes WebGPU's per-group binding-count limit: any number of
buffers and textures, from any number of components, collapse into a constant
number of bindings. The full mechanism, struct layout, and the
`submit`/`finalize`/`update`/`bind` flow are in
[GPU Resource Binding](gpu-resource-binding.md).

---

## Multiple terminals and figures

A yetty window can host several panes, each with its own terminal, and each
terminal composites a stack of layers plus a figure container. They all share:

- the one device + queue + allocator from yframework,
- the MSDF font generator (large atlas materialized on demand),
- the figure factories registered by
  `yetty_yframework_register_figure_factories` — complex figures (yplot, yimage,
  ymgui, yrdawn, …) of the same kind share one compiled pipeline, and each
  instance binds only its own per-instance data.

Per-terminal state (the cell buffer, per-layer uniforms, per-figure data) is
owned by that terminal's renderers and is never global.

---

## Pointers

- WebGPU object ownership: `include/yetty/yetty/yetty.h`,
  `include/yetty/yframework/yframework.h`
- Allocator: `include/yetty/yrender/gpu-allocator.h`
- Binder + pipeline: `include/yetty/yrender/gpu-resource-binder.h`,
  `include/yetty/yrender/pipeline.h`
- Render targets: `include/yetty/yrender/render-target.h`
