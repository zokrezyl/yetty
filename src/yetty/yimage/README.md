# yimage — inline raster images as a ydraw figure

yimage decodes a raster image (any format stb_image handles: PNG, JPEG, GIF,
BMP, TGA, …) into RGBA8 pixels and packs them into one `yimage` composite
primitive (type id `0x80000004`) inside a ydraw drawable list. Sender side,
the list ships as a DCS `YDRAW_BIN` envelope (via [`yface`](../yface/README.md));
receiver side, a pre-compiled factory routes the pixels into the shared RGBA8
atlas and the shader samples the atlas region with hardware bilinear
filtering. The composite model (factory / instance / binder) is described in
[`ydraw`](../ydraw/README.md).

## Two-tier build

| target | contents | GPU |
|--------|----------|-----|
| `yetty_yimage_core` | decode (stb_image), wire serializer, DCS emit — `yimage.c`, `yimage-gen-wire.c` | none — links from client tools and cross-targets |
| `yetty_yimage` | concrete factory + per-instance render — `yimage-gen.c` | Dawn / WebGPU (gated on `YETTY_ENABLE_LIB_WEBGPU`) |

## Generated files

`yimage-gen.h` (in `include/yetty/yimage/`), `yimage-gen.c`,
`yimage-gen-wire.c`, and `yimage-gen.wgsl` are generated from the schema
`yimage.yaml` by [`ydraw-gen`](../ydraw-gen/README.md)'s `generate.py` — never
edit them by hand. The schema declares the six uniforms (`bounds_x/y/w/h`,
`image_w/h`), the `pixels` storage buffer, and diverts that buffer into an
RGBA8 atlas texture (`pixels_buffer: pixels`), so the pixel words never
occupy a storage-buffer binding.

## Public API (`include/yetty/yimage/yimage.h`)

```c
int width = 0, height = 0;
yetty_yimage_probe_size(bytes, len, &width, &height); /* header sniff, no full decode */

struct yetty_yimage_render_config config = {.bounds_w = 320.0f}; /* 0 = source size */
struct yetty_ydraw_drawable_list_result list_res =
    yetty_yimage_render(bytes, len, &config);          /* or yetty_yimage_render_path() */

yetty_yimage_dcs_bin_emit(list_res.value, stdout);     /* DCS YDRAW_BIN envelope */
yetty_ydraw_drawable_list_destroy(list_res.value);
```

Receiver-side registration (done by the canvas/terminal hosts, not by apps):

```c
struct yetty_ydraw_concrete_factory *factory = yetty_yimage_factory_create();
factory->destroy = yetty_yimage_factory_destroy;
yetty_ydraw_composite_factory_register(abstract_factory, factory);
```

The wire layout is `[type_id][payload_size][6 uniform words][pixels_len]
[pixels…]`, pixels packed RGBA8 as little-endian `u32`, one word per pixel,
row-major. `bounds_x/y` sit at uniform offsets 0/1 so the canvas can override
them with the post-scroll screen position at render time.

## File map

| file | role |
|------|------|
| `yimage.c` | hand-written glue: decode → serialize → drawable list, DCS emit |
| `yimage.yaml` | composite schema — source of truth for every `*-gen*` file |
| `yimage-gen-wire.c` | generated wire serializer (CPU-only) |
| `yimage-gen.c` | generated factory: shared pipeline, per-instance resource set/binder |
| `yimage.wgsl` | hand-written shader — maps pane-local pixels to the atlas UV region |
| `yimage-gen.wgsl` | generated uniform accessors, concatenated before the main shader |
| `scale-image.wgsl` | compute-shader resampler — present but NOT wired (see Status) |

## Status

Compute-shader pre-scaling (`scale-image.wgsl`) is not wired: pixels reach
the atlas at source resolution and the GPU's bilinear sampler does the
display-size scaling. Everything else described above is live.

## Consumers

- [`ycat`](../ycat/README.md) — `handler-image.c` renders image files inline.
- [`ysvg`](../ysvg/README.md) — `<image>` elements serialize yimage records
  into the SVG scene (via `yetty_yimage_core` only).
- [`ygui`](../ygui/README.md) — the `widgets/yimage.c` figure widget.
- [`ybrowser`](../ybrowser/README.md), [`ymap`](../ymap/README.md), and tools
  (`tools/ygreeter`, `tools/yhello`, the ybrowser UI) embed yimage records.
- Factory registration on the receiver: `yterminal/terminal.c`,
  `yui/yui.c`, and `ydraw/scrolling-canvas.c`.

See also [`yrender`](../yrender/README.md) for the pipeline/binder layer and
[gpu-resource-binding](../../../docs/gpu-resource-binding.md) for how the
atlas and uniform block are flattened into one binding set.
