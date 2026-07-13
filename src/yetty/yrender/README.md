# yrender — render pipeline

The GPU pipeline module: resource-set binder, GPU allocator, pipeline
creation, and the render targets (texture, VNC, X11 tile). Layers and
figures describe what to draw as trees of resource sets; yrender flattens
them into one binding set and drives the dirty-driven upload/draw flow
described below.

## Overview

The render pipeline connects layer data to the GPU through a dirty-flag driven flow:

```
Layer (dirty) → Resource Set Tree → Binder (finalize/update) → GPU → Draw
```

## Binder Lifecycle

### finalize() — one-time setup

Runs once on the first dirty frame. Collects all resource sets, creates GPU objects, compiles the pipeline.

1. Flatten the resource set tree (depth-first: children before parents)
2. Pack all buffers into one mega storage buffer, recording byte offsets
3. Shelf-pack all textures into per-format atlas textures (R8, RGBA8), recording UV regions
4. Compute aligned uniform buffer layout
5. Upload all data to GPU
6. Generate WGSL binding code (uniform struct, binding declarations, offset constants, region constants)
7. Merge generated bindings with layer shader code
8. Compile shader module and create render pipeline
9. Store size/dimension/hash snapshots for change detection

### update() — per-frame

Runs every frame after finalize. Checks for structural changes, uploads dirty data.

1. **Check shader hash** — if any shader code changed, re-finalize
2. **Check buffer sizes** — if ANY buffer size changed, all mega buffer offsets are invalid → re-finalize
3. **Check texture dimensions** — if ANY texture dimension changed, atlas layout is invalid → re-finalize
4. **Upload dirty buffers** — only buffers with `dirty = 1`, clear flag after upload
5. **Upload dirty textures** — only textures with `dirty = 1`, clear flag after upload
6. **Upload uniforms** — always (small, may change without dirty flag)

### Re-finalize

When `update()` detects a structural change:
1. Release all GPU objects (storage buffer, uniform buffer, atlases, bind group, pipeline)
2. Keep quad vertex buffer (never changes)
3. Reset shader code buffer
4. Run the full finalize sequence again

## Dirty Flag Flow

```
PTY data → vterm → on_damage callback → rebuild_cell_buffer → buffer.dirty = 1, layer.dirty = 1
                                                                     ↓
New glyph requested → rasterize_glyph → atlas grows → font.dirty = 1
                                                           ↓
get_gpu_resource_set → propagate font.dirty → texture.dirty = 1, buffer.dirty = 1
                                                           ↓
terminal_render_frame → check layer.dirty → skip if clean
                                          → finalize (first time) / update (subsequent)
                                          → draw
                                          → clear layer.dirty
```

## Mega Buffer Layout

All storage buffers packed sequentially, 4-byte aligned:

```
[raster_font_buffer: 152 bytes][padding: 0][text_grid_buffer: 23040 bytes]
 ↑ offset 0 (u32 offset: 0)                ↑ offset 152 (u32 offset: 38)
```

Shader accesses via generated constants:
```wgsl
@group(0) @binding(3) var<storage, read> storage_buffer: array<u32>;
const raster_font_buffer_offset: u32 = 0u;
const text_grid_buffer_offset: u32 = 38u;
```

## Atlas Texture Layout

Per-format atlas (e.g., atlas_r8 for R8Unorm textures). Source textures shelf-packed into a power-of-2 atlas. UV regions generated as constants:

```wgsl
@group(0) @binding(1) var atlas_r8_texture: texture_2d<f32>;
@group(0) @binding(2) var atlas_r8_sampler: sampler;
const raster_font_texture_region: vec4<f32> = vec4<f32>(0.0, 0.0, 0.656, 0.094);
```

## Uniform Buffer Layout

All uniforms packed into one struct with WGSL alignment rules (vec2→8, vec3→16, vec4→16, mat4→16, f32/u32/i32→4):

```wgsl
struct Uniforms {
    text_grid_grid_size: vec2<f32>,       // offset 0
    text_grid_cell_size: vec2<f32>,       // offset 8
    text_grid_cursor_pos: vec2<f32>,      // offset 16
    text_grid_cursor_visible: f32,        // offset 24
    text_grid_cursor_shape: f32,          // offset 28
    text_grid_scale: f32,                 // offset 32
    text_grid_default_fg: u32,            // offset 36
    text_grid_default_bg: u32,            // offset 40
};
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
```

## Key Files

| file | role |
|------|------|
| `gpu-resource-binder.c` | the binder: flatten, pack, upload, WGSL generation (`include/yetty/yrender/gpu-resource-set.h`, `gpu-resource-binder.h`) |
| `types.c` | buffer / texture / uniform types + size, alignment, hash utilities (`include/yetty/yrender/types.h`) |
| `primitive-gpu-binder.c` | binder specialization for ydraw primitive buffers |
| `gpu-allocator.c` | GPU buffer/texture allocation tracking |
| `pipeline.c` | render pipeline creation / shader module compilation |
| `font-dispatcher.c` | routes glyph requests to the font resource sets |
| `render-target-texture.c` / `render-target-vnc.c` / `render-target-x11-tile.c` | render-target backends (`include/yetty/yrender/render-target.h`) |
| `blend.wgsl` | blend shader source |

See [GPU Resource Binding](../../../docs/gpu-resource-binding.md) for the
resource-set struct and the `submit`/`finalize`/`update`/`bind` binder ops, and
[Layered Rendering](../../../docs/layered-rendering.md) for how terminal layers
and the figure compositor feed this pipeline.
