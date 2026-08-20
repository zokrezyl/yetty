# ymesh — 3D mesh figure (glTF .glb / PLY / STL / OBJ)

`ymesh` loads a mesh and renders it as a complex ydraw figure with a
real 3D pipeline (vertex + index buffers, depth test, Lambert shading or
wireframe). It is split like yvideo: a GPU-less client side that
serialises the wire format, and a server side that owns the raw WebGPU
pipeline inside the terminal.

Formats (content-sniffed by `ymesh-load.c`, no extension needed):

| format | notes |
|--------|-------|
| `.glb` | glTF 2.0 binary via cgltf (first mesh, first primitive) |
| `.ply` | ascii + binary_little_endian; missing normals are computed; a **vertex-only PLY renders as a point cloud** (octahedron marker per point, sized from the cloud bbox, capped at 150k points) |
| `.stl` | binary + ascii, flat-shaded per facet |
| `.obj` | `v`/`vn`/`f` with fan triangulation and negative indices; missing normals computed (smooth, area-weighted) |

Per-vertex PLY colors are not yet carried to the GPU — the wire has no
color attribute (follow-up in #596).

## Two targets

| target | contents | deps |
|--------|----------|------|
| `yetty_ymesh_core` | `.glb` decode (cgltf), wire serialisation, DCS emit | `cgltf`, `ydraw-list`, `yface` — no GPU |
| `yetty_ymesh` | concrete complex factory + WGSL shaders (incbin) | `ymesh_core`, `ydraw-factory`, `yrender`, Dawn (gated on `YETTY_ENABLE_LIB_WEBGPU`) |

## Client side (`ymesh.c`, `ymesh-glb.c`)

`yetty_ymesh_glb_parse` extracts the FIRST mesh's FIRST primitive: POSITION +
NORMAL streams, the index buffer, and the bounding box. `yetty_ymesh_render`
serialises one prim with type id `YETTY_YMESH_TYPE_ID` (0x80000005) into a
fresh ydraw buffer; `yetty_ymesh_dcs_bin_emit` wraps it in the
`YETTY_DCS_YDRAW_BIN` envelope. The serializer is hand-written (not
schema-generated) because the pipeline needs a real vertex layout, an index
buffer, and a depth attachment — none of which the fullscreen-quad schema
generator expresses.

Wire format: `[type_id][payload_size]` + bounds (4×f32) + bbox min/max
(6×f32) + camera (`azimuth`, `elevation`, `dist_factor`, `pan_x`, `pan_y`,
`mode`) + counts, then positions / normals / indices. Camera is a
bbox-centred Y-up orbit; distance = `bbox_radius * dist_factor`; `mode` is
0 = solid (Lambert) / 1 = wireframe.

## Server side (`ymesh-gen.c`)

The concrete factory registered with the abstract complex factory. Each
instance owns its vertex/index/uniform buffers, an offscreen RGBA8 +
depth16 target (clamped to 1024², linearly scaled at blit), and renders in
two passes: a 3D pass into the offscreen target (`ymesh.wgsl`), then a
fullscreen-triangle blit into the layer target with `LoadOp_Load`
(`ymesh-blit.wgsl`). The MVP is derived on the GPU side from bbox + camera
fields and re-derived when the wire bytes change (interactive viewer).

## Public API sketch

```c
#include <yetty/ymesh/ymesh.h>

struct yetty_ymesh_render_config config = { .mode = YETTY_YMESH_MODE_SOLID };
struct yetty_ydraw_drawable_list_result r =
    yetty_ymesh_render_path("model.glb", &config);
yetty_ymesh_dcs_bin_emit(r.value, stdout);
/* server side: yetty_ymesh_factory_create() / _destroy (ymesh-gen.h) */
```

## Layout of the module

| file | role |
|------|------|
| `ymesh.c` | wire serialisation, buffer attach, DCS emit |
| `ymesh-glb.c` | minimal cgltf wrapper → host-side mesh |
| `ymesh-gen.c` | concrete factory: raw-WebGPU 3D + blit passes |
| `ymesh.wgsl` / `ymesh-blit.wgsl` | shaders (embedded via incbin) |

Public headers: `ymesh.h`, `ymesh-glb.h`, `ymesh-gen.h`, and `ymesh-math.h`
(inline column-major vec3/mat4 helpers). Gated by
`YETTY_ENABLE_FEATURE_YMESH`.

## Status

MVP: positions + normals + indices of a single mesh/primitive. Textures,
UVs, materials, skinning, animations, and multi-primitive scenes are
ignored.

## Consumers

- **yterminal** (`terminal.c`) and **ydraw** (`scrolling-canvas.c`) register
  the factory alongside yplot / yimage / yshadertoy / yvideo.
- **tools/ymesh** — CLI emitter; one-shot, or interactive: subscribes to
  terminal-wide input and re-emits clear+bin on drag-orbit / pan / wheel-zoom
  / `W` wireframe toggle.

## Related

- [../ydraw-factory/README.md](../ydraw-factory/README.md) — complex
  factory model
- [../yrender/README.md](../yrender/README.md) — render targets
- [../ydraw/README.md](../ydraw/README.md) — complex storage/lifecycle in
  the scrolling canvas
