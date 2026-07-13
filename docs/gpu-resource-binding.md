# GPU Resource Binding

Yetty packs an arbitrary number of buffers and textures from an arbitrary number
of components into a **fixed, small set of GPU bindings**. This sidesteps
WebGPU's per-group binding limits: a text layer plus its font, plus dozens of
PDF-embedded subset fonts, all collapse into one storage buffer + one atlas per
format + one uniform block.

This document covers the data structures and the binder flow. See
[Design Overview](design.md) for the rationale and [Render Pipeline](../src/yetty/yrender/README.md)
for the per-frame upload and recompilation logic.

---

## Resource set tree

Every component that needs GPU resources fills in a
`struct yetty_yrender_gpu_resource_set` (declared in
`include/yetty/yrender/gpu-resource-set.h`). Resource sets form a **tree**: a
layer's set lists the font's set as a child, and the binder flattens the whole
tree depth-first (children before parents).

```c
struct yetty_yrender_gpu_resource_set {
    char namespace[YETTY_YRENDER_NAME_MAX];     /* prefixes generated WGSL names */
    struct yetty_ycore_pixel_size pixel_size;

    struct yetty_yrender_texture textures[YETTY_YRENDER_RS_MAX_TEXTURES]; /* 4 */
    size_t texture_count;

    struct yetty_yrender_buffer  buffers[YETTY_YRENDER_RS_MAX_BUFFERS];   /* 4 */
    size_t buffer_count;

    struct yetty_yrender_uniform uniforms[YETTY_YRENDER_RS_MAX_UNIFORMS]; /* 32 */
    size_t uniform_count;

    struct yetty_yrender_shader_code shader;     /* WGSL body for this provider  */

    struct yetty_yrender_gpu_resource_set *children[YETTY_YRENDER_RS_MAX_CHILDREN]; /* 64 */
    size_t children_count;

    uint32_t instance_count;  /* draw shape: 6 verts × instance_count. 0 = skip. */
};
```

Each `buffer`/`texture`/`uniform` carries a description **plus a CPU data
pointer and a dirty flag**. The component owns the backing data; the resource set
just points at it.

> The struct lives in the `yetty_ydraw_` namespace for historical reasons but is
> declared and used throughout `yrender`. The matching result type is
> `yetty_yrender_gpu_resource_set_result`.

---

## The binder

`struct yetty_yrender_gpu_resource_binder`
(`include/yetty/yrender/gpu-resource-binder.h`) owns the merged GPU objects and
the compiled pipeline. It is driven through its ops table:

```c
struct yetty_yrender_gpu_resource_binder_ops {
    void (*destroy)(struct yetty_yrender_gpu_resource_binder *self);

    /* Per frame: hand the binder a (sub)tree of resource sets to collect. */
    struct yetty_ycore_void_result (*submit)(
        struct yetty_yrender_gpu_resource_binder *self,
        const struct yetty_yrender_gpu_resource_set *rs);

    /* One-time: flatten, pack, create GPU objects, compile pipeline. */
    struct yetty_ycore_void_result (*finalize)(struct yetty_yrender_gpu_resource_binder *self);

    /* Per frame: upload only dirty data; re-finalize on structural change. */
    struct yetty_ycore_void_result (*update)(struct yetty_yrender_gpu_resource_binder *self);

    /* Per frame: bind to a render pass at the given group index. */
    struct yetty_ycore_void_result (*bind)(struct yetty_yrender_gpu_resource_binder *self,
                                           WGPURenderPassEncoder pass, uint32_t group_index);

    WGPURenderPipeline (*get_pipeline)(const struct yetty_yrender_gpu_resource_binder *self);
    WGPUBuffer (*get_quad_vertex_buffer)(const struct yetty_yrender_gpu_resource_binder *self);

    /* Streaming: write a sub-range of one flattened storage buffer with a
     * single wgpuQueueWriteBuffer — no rebind, no re-finalize. Used for live
     * data (audio samples, scrolling time series). */
    struct yetty_ycore_void_result (*write_buffer_chunk)(
        struct yetty_yrender_gpu_resource_binder *self,
        size_t buffer_index, size_t byte_offset, const void *data, size_t size);
};
```

### Two construction modes

```c
/* Binder owns its own pipeline (compiles its shader at finalize).
 * Used by layer / render-target code where pipeline-per-binder is fine. */
yetty_yrender_gpu_resource_binder_create(device, queue, surface_format, allocator);

/* Binder uses an externally-owned, shared pipeline; it owns only the
 * per-instance side (uniform buffer, storage buffer, bind group). Used by
 * complex figures (yplot, yimage, ...) so all instances of one kind share
 * one compiled shader. The pipeline must outlive the binder. */
yetty_yrender_gpu_resource_binder_create_with_pipeline(device, queue, allocator, pipeline);
```

---

## What `finalize()` produces

The binder flattens the resource set tree and packs everything into a fixed
binding layout, generating the WGSL glue automatically:

- **One storage buffer** — every component buffer concatenated (4-byte aligned)
  into a single `array<u32>`, with generated offset constants
  (`text_grid_buffer_offset`, `raster_font_buffer_offset`, …).
- **One atlas texture per format** (R8, RGBA8) — every component texture
  shelf-packed, with generated UV-region constants (`<namespace>_texture_region`).
- **One uniform block** — every component uniform packed into a single WGSL
  struct following WGSL alignment rules (vec2→8, vec3/vec4/mat4→16, scalar→4).
- **Merged shader** — generated binding declarations, offset constants, and
  region constants are prepended to each provider's shader body.

The result is a constant number of GPU bindings regardless of how many
components contributed. See [Render Pipeline](../src/yetty/yrender/README.md) for the exact
`finalize()` / `update()` step lists and the change-detection that triggers a
re-finalize.

---

## Per-frame flow

```c
/* A renderer owns one binder. Each frame: */
binder->ops->submit(binder, &layer_resource_set);  /* collect the tree         */
binder->ops->update(binder);                        /* upload dirty data,       */
                                                     /* re-finalize if structure changed */
binder->ops->bind(binder, pass, /*group*/ 0);
wgpuRenderPassEncoderDraw(pass, 6, instance_count, 0, 0);
```

- **First dirty frame:** `finalize()` runs (via `update`'s first-call path) —
  create GPU objects, compile pipeline.
- **Subsequent frames:** `update()` uploads only buffers/textures whose dirty
  flag is set, clearing the flag after upload.
- **Structural change** (a buffer size or texture dimension changed, or shader
  code changed by hash): the binder releases its GPU objects and re-finalizes.

## Key files

- `include/yetty/yrender/gpu-resource-set.h` — resource set tree struct
- `include/yetty/yrender/gpu-resource-binder.h` — binder ops + constructors
- `include/yetty/yrender/types.h` — buffer / texture / uniform types
- `src/yetty/yrender/gpu-resource-binder.c` — flatten, pack, codegen, upload
- `src/yetty/yrender/pipeline.c` — shared pipeline (two-tier mode)
- `src/yetty/yrender/types.c` — size / alignment / hash helpers
