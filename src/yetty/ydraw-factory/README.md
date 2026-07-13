# ydraw-factory — GPU-side factory runtime for composite figures

The server-side half of the composite model: it turns composite wire bytes
(format defined in [ydraw-core](../ydraw-core/README.md)'s `composite.h`) into
renderable GPU instances. Consumed by the receiving canvases
(`../ydraw/scrolling-canvas.c`, `scrolling-grid.c`,
[ygrid](../ygrid/README.md)), by yterminal/yui at setup time, and by every
concrete composite factory — generated (yplot, yimage, yvideo via
[ydraw-gen](../ydraw-gen/README.md)) or hand-written (ymesh, yshadertoy).
Built only when `YETTY_ENABLE_LIB_WEBGPU` is on; no-GPU builds use just
ydraw-core.

## Three-level architecture

- **Abstract factory** (`struct yetty_ydraw_composite_factory`) — a registry
  mapping `type_id` → concrete factory (up to 32). Created once the WebGPU
  device/queue exist; stashes the event loop and propagates it to concrete
  factories so instances can subscribe to timers/input.
- **Concrete factory** (`struct yetty_ydraw_concrete_factory`) — one per
  composite type. Owns the shared `yetty_yrender_pipeline`; `compile_pipeline`
  is **deferred to the first `create_instance` of that type** (a plain text
  session never pays for figure shaders). Also carries optional
  `update_instance`, `set_visual_zoom` / `set_cell_zoom` (two independent
  zoom transforms) and a `hook_data` slot for per-type state.
- **Instance** (`struct yetty_ydraw_composite`) — one per figure occurrence,
  stored in the host grid. Holds a copy of its wire bytes, its `bounds` and
  `rolling_row`, a per-instance `resource_set` + `binder` (own uniform/storage
  buffers and bind group, referencing the factory's shared pipeline), a
  per-instance `dirty` bit, an embedded event-listener, and a per-instance ops
  vtable (`destroy`, `update`) so runtime dispatch bypasses the factory.

```c
struct yetty_ydraw_composite_factory_ptr_result fac_res =
    yetty_ydraw_composite_factory_create(device, queue, target_format,
                                         allocator, event_loop);
yetty_ydraw_composite_factory_register(fac_res.value, yplot_concrete);

struct yetty_ydraw_composite_ptr_result inst_res =
    yetty_ydraw_composite_factory_create_instance(fac_res.value,
                                                  wire_bytes, size, rolling_row);
yetty_ydraw_composite_render(inst_res.value, target, x, y);
yetty_ydraw_composite_destroy(inst_res.value);
```

## GPU residency — bounded LRU

A figure's binder holds real allocator slots, and 10k lines of scrollback can
anchor far more figures than the allocator budget. The abstract factory owns a
`yetty_ydraw_gpu_residency` manager: an intrusive LRU threaded through each
instance's `res_prev`/`res_next`. Only the `YETTY_YDRAW_GPU_RESIDENCY_BUDGET`
(128) most recently rendered figures stay resident; falling off the tail runs
`binder->release_gpu()`, and the next `render()` reacquires slots via
`binder->finalize()`. Every render touches the instance to the MRU head, so an
on-screen figure is never evicted mid-frame.

## Incremental updates

A wire `CMD_UPDATE` (see `../ydraw-core/cmds.h`) is resolved by the canvas to
an instance and dispatched through `instance->ops->update(self, target_field,
body, body_size)` — `target_field` is the schema-level slot id, the body's
semantics belong to the figure type (yplot: chunked f32 sample writes).
Factories/instances without an update op silently drop the record.

## Files

| file | role |
|------|------|
| `composite-factory.c` | abstract-factory registry, deferred pipeline compile, instance mint/destroy/render wrappers, residency LRU, zoom fan-out |

Public header: `include/yetty/ydraw-factory/composite-factory.h` (pulls in
`<webgpu/webgpu.h>`; keep client-only code on the ydraw-core side).

## See also

- [ydraw](../ydraw/README.md) — the factory pattern and render flow in context.
- [ydraw-gen](../ydraw-gen/README.md) — generates concrete factories from YAML.
- [yrender](../yrender/README.md) — pipelines, resource sets, binder.
- [GPU resource binding](../../../docs/gpu-resource-binding.md).
