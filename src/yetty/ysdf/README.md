# ysdf — SDF primitive definitions, parsing and construction

ysdf is the single source of truth for yetty's SDF drawable primitives.
One YAML file (`sdf-drawables.yaml`) defines every shape — its numeric type
id, geometry fields, WGSL distance function and C AABB code — and
`gen-sdf-code.py` generates matching C and WGSL from it. Everything that
emits or consumes SDF records (ychart, ydiagram, ycircuit, ygui widgets,
ybrowser/ylexbor paint, ygrid, the terminal SDF layer, …) builds on this
module. Depends only on `ycore` and `ydraw-core` (plus libyaml for the YAML
parse path).

## Wire layout

An SDF record is a fixed-size block of 32-bit words:

```
[type][z_order][fill_color][stroke_color][stroke_width][geometry...]
```

with an optional extra id word when the type carries
`YETTY_YDRAW_HAS_ID_FLAG`. Geometry starts at word 5. About 28 types exist
(circle, box, segment, triangle, ellipse, arc, rounded_box, star, pie, ring,
heart, capsule, gradient boxes, sphere_3d / box_3d / torus_3d / cylinder_3d,
…) with ids in the `0x7FFFFFxx` range — see `enum yetty_ysdf_type` in
`include/yetty/ysdf/types.gen.h`.

## The generator

```
uv run src/yetty/ysdf/gen-sdf-code.py
```

reads `sdf-drawables.yaml` and writes (all marked DO NOT EDIT):

- `include/yetty/ysdf/types.gen.h` — type enum, per-shape geometry structs,
  inline `yetty_ysdf_word_count` / `yetty_ysdf_primitive_size` /
  `yetty_ysdf_geometry_transform`.
- `include/yetty/ysdf/funcs.gen.h` + `funcs.gen.c` — one
  `yetty_ydraw_drawable_list_add_cmd_add_<shape>()` builder per shape.
- `aabb.gen.c` — `yetty_ysdf_compute_aabb` (per-shape C code from the YAML).
- `ysdf.gen.wgsl` — the WGSL `sdf_*` functions plus the `evaluate_sdf_2d`
  dispatcher (NaN-safe `length` for fast-math backends). Staged into the
  shader assets by [../yshaders](../yshaders/README.md); ygrid and the
  vterm SDF layer attach it as a child resource set.
- `yaml-factory.gen.h` + `yaml-factory.gen.c` — per-shape factories for the
  ydraw-yaml scene parser (`yetty_ysdf_register_yaml_factories`).

This is a standalone generator, separate from the yclass `make codegen`
flow. To add or change a shape, edit the YAML and re-run the script — never
the `*.gen.*` files.

## Public API sketch

```c
/* Discriminator + base ops for the drawable-list registry (handler.h).
 * primitive_size() returns 0 for non-SDF types — no hardcoded id ranges. */
struct yetty_ydraw_drawable_list_entry_ops_ptr_result
yetty_ysdf_handler(uint32_t drawable_type);

/* Typed builder, one per shape (funcs.gen.h). */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_cmd_add_circle(
    struct yetty_ydraw_drawable_list *list, uint32_t id, uint32_t z_order,
    uint32_t fill_color, uint32_t stroke_color, float stroke_width,
    const struct yetty_ysdf_circle *geom);

/* Splice one drawable list into another under scale+translate (merge.h).
 * Used by ybrowser to embed an ysvg-rendered scene at an image box. */
struct yetty_ycore_int_result yetty_ydraw_drawable_list_merge_transformed(
    struct yetty_ydraw_drawable_list *destination,
    const struct yetty_ydraw_drawable_list *source,
    float offset_x, float offset_y, float scale_x, float scale_y,
    uint32_t z_order_offset);
```

`merge.c` is the only hand-written translation unit: it walks a source
stream record by record, transforms SDF geometry / text positions /
composite bounds per the primitive's own semantics, drops `CMD_ZERO`, and
appends to the destination. FONT resources pass through unchanged (font-id
remapping is not implemented — see the coverage notes in `merge.h`).

## File map

| file | role |
|------|------|
| `sdf-drawables.yaml` | shape definitions — the source of truth |
| `gen-sdf-code.py` | generator (uv-run script, pyyaml) |
| `funcs.gen.c` / `aabb.gen.c` / `yaml-factory.gen.c` | generated C |
| `ysdf.gen.wgsl` | generated WGSL SDF library + dispatcher |
| `merge.c` | hand-written drawable-list merge under affine transform |
| `yaml.gen.c` | orphaned earlier generator output (`yetty_ysdf_yaml_parse`) — not compiled by the CMakeLists and no callers |
| `include/yetty/ysdf/handler.h` | inline registry ops (size / AABB) |
| `include/yetty/ysdf/merge.h` | merge contract + coverage notes |

## Consumers

`yetty_ysdf_handler` is the default handler of every drawable-list registry
(`../ydraw/drawable-list-registry.c`, `../ygrid/grid.c`,
`../yterminal/terminal.c`, `tools/osc-analyzer`). The typed builders are
called by every SDF producer: [../ychart](../ychart/README.md),
[../ydiagram](../ydiagram/README.md), [../ycircuit](../ycircuit/README.md),
[../ysvg](../ysvg/README.md), the ygui widgets, ybrowser / ylexbor paint,
yflame, ymarkdown, ymap, ymaze and more.

## See also

- [../ydraw/README.md](../ydraw/README.md) — drawable lists and scrolling.
- [../ygrid/README.md](../ygrid/README.md) — the figure that renders SDF batches.
- [../../../docs/gpu-resource-binding.md](../../../docs/gpu-resource-binding.md).
