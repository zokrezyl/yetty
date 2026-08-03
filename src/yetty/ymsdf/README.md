# ymsdf — polymorphic MSDF glyph-CDB generator (cpu | gpu)

`ymsdf` puts one ops-table interface in front of the two MSDF generation
backends: the CPU path ([`ymsdf-gen`](../ymsdf-gen/README.md), msdfgen
library, multi-threaded) and the GPU path
([`ymsdf-wgsl`](../ymsdf-wgsl/README.md), WebGPU compute shader). Both take a
TTF and produce a `.cdb` of MSDF glyph bitmaps that the font/text pipeline
loads as an atlas source (see [`yfont`](../yfont/README.md) and
[`ycdb`](../ycdb/README.md)).

## How the backend is selected

The config key `msdf/generator` (values `"cpu"` or `"gpu"`, default `"gpu"`)
picks the implementation at startup. `yframework` calls
`yetty_ymsdf_generator_create_from_config()` right after the WebGPU device is
ready (the gpu impl needs `WGPUDevice` + `WGPUInstance` up front) and stores
the instance on the GPU context (`rt->gpu.msdf_generator` in
`yframework.c`), exposed to the app as `yetty_context.msdf_generator`. Every
consumer borrows that one shared instance, so the two backends can be A/B'd
by flipping a single config knob. Any other value for the key is rejected
loudly — a typo never silently picks a backend.

The factory rejects `"cpu"` at runtime when the CPU backend was not compiled
in (`YETTY_ENABLE_FEATURE_YMSDF_GEN=OFF` defines `YETTY_YMSDF_NO_CPU`; the
Windows build keeps msdfgen out because it ships `/MT` against an `/MD`
third-party stack).

## Public API

```c
struct yetty_ymsdf_generator_config cfg = {
    .ttf_path = "/path/font.ttf",
    .cdb_path = "/cache/font.cdb",
    .font_size = 32.0f,   /* 0 → default 32 */
    .pixel_range = 4.0f,  /* 0 → default 4  */
};

/* From config (the yframework path): */
struct yetty_ymsdf_generator_ptr_result gr =
    yetty_ymsdf_generator_create_from_config(config, device, instance, shaders_dir);

/* Or construct a backend directly (unit tests, harnesses): */
gr = yetty_ymsdf_generator_create_cpu();
gr = yetty_ymsdf_generator_create_gpu(device, instance, "/…/msdf_gen.wgsl");

struct yetty_ycore_void_result r = gr.value->ops->generate(gr.value, &cfg);
gr.value->ops->destroy(gr.value);   /* borrows device/instance — not released */
```

`ops->name()` returns `"cpu"` or `"gpu"` for logs/diagnostics. `generate()`
is allowed to be slow; consumers gate on file existence (the `.cdb` acts as
a cache) before calling it.

## Backend notes

- **cpu-generator.c** — adapts the (ttf_path, output_dir) shape of
  `yetty_ymsdf_gen_config_cpu_generate()` to the polymorphic full-`cdb_path`
  API: splits the path, runs the CPU generator, and renames the output if the
  TTF basename differs from the requested stem.
- **gpu-generator.c** — captures `WGPUDevice`/`WGPUInstance` (and an owned
  copy of the shader path) at create time so consumers call `generate(cfg)`
  without threading GPU handles through. Delegates to
  `yetty_ymsdf_wgsl_config_generate()`. If `shader_path` is NULL the wgsl
  impl falls back to its exe-dir + `./shaders` search chain.

## Consumers

All hold a borrowed `struct yetty_ymsdf_generator *` and call it when a font
referenced by a drawable list has no cached `.cdb` yet:

- `../ydraw/scrolling-canvas.c` — font materialisation for scrolling ydraw
  content ([ydraw](../ydraw/README.md)).
- `../yvterm/grid-sdf-layer.c` — the SDF layer's named-font path
  ([yvterm](../yvterm/README.md)).
- `../yscene/scene.c` — the scene figure's font slots
  ([yscene](../yscene/README.md)).

## Layout of the module

| file | role |
|------|------|
| `factory.c` | `create_from_config` — config-key dispatch, backend validation |
| `cpu-generator.c` | ops wrapper over `yetty_ymsdf_gen` (compiled only with `YETTY_ENABLE_FEATURE_YMSDF_GEN`) |
| `gpu-generator.c` | ops wrapper over `yetty_ymsdf_wgsl` |
| `../../../include/yetty/ymsdf/generator.h` | the ops table, config struct, factories |

Built when `YETTY_ENABLE_FEATURE_YMSDF_WGSL` is on (the factory is always
invoked at startup, so the wrapper follows the gpu impl's gate).
