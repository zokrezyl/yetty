# ygrid — figure: spatial-bucketed batch of SDF primitives + glyphs

ygrid is a concrete figure kind (`class@ygrid:grid`, parent `yfigure:figure`):
one GPU batch that stores wire-format drawable records, buckets them by grid
cell, and renders SDF shapes, glyphs and embedded composite figures through
its own pipeline. It is the renderer behind ygui window chrome and every
producer figure (yplot / yimage / yvideo content minted through the
composite factory). The terminal's scrolling rich content is NOT rendered
by ygrid — that is [yvterm](../yvterm/README.md)'s own `sdf-layer.c`; see
"Relation to yvterm" below. Depends on
`ycore`, `yclass`, `yfigure`, `ydraw-core`, `yrender`, `ysdf`, `yfont`.

## How it works

- **Storage** — `add_record` copies wire records verbatim
  (`u32 type | u32 payload_size | bytes`) into one flat byte buffer and
  parses each once into `struct ygrid_prim_meta` (type, payload offset, AABB,
  owning entity, rolling row). Coordinates are LOCAL to the figure origin
  (or ABSOLUTE screen pixels when the factory sets `absolute_coords` — the
  ygui chrome path, clipped by the per-figure scissor).
- **Entity tree** — `CMD_GROUP(id, payload)` opens a named entity scope,
  `CMD_DELETE(id)` tombstones an entity and its prims, `CMD_ZERO` clears the
  grid. One ygrid can hold every widget of a window; an open-addressing hash
  maps external ids to slots.
- **Bucketing + staging** — prims are partitioned into `grid_cols × grid_rows`
  cell buckets; a rebuild (when `staging_dirty`) packs the grid index and the
  prim payloads into two staging arrays uploaded by the binder.
- **Rendering** — a self-owned `gpu_resource_binder` flattens the resource-set
  tree (grid + prims + uniforms, child `ysdf.gen.wgsl` SDF library, child
  `effects-lib.wgsl`, one child per font slot), compiles `ygrid.wgsl` with
  generated offsets, and draws one full-rect quad; the fragment shader walks
  the cell bucket under each pixel. Composite prims (yplot, yimage, …) are
  minted through the borrowed composite factory and render after the SDF pass
  with their own pipelines.
- **Fonts** — slot 0 is the default font for GLYPH / TEXT_DRAWABLE_LIST
  records; wire-shipped FONT prims are materialised through the font cache +
  MSDF generator into further slots. A per-slot dispatcher is regenerated on
  font changes, which re-finalizes the binder (shader hash change).
- **Scrolling** — three mechanisms: `set_content_size` + `set_scroll` turn the
  rect into a viewport over larger content; the rolling-row API
  (`set_rolling_row_0` / `set_insert_rolling_row` / `set_rolling_cell_height`)
  gives a scrolling host O(1) scroll (shader offsets each prim by
  `(rolling_row - rolling_row_0) * cell_height`) — this was the terminal's
  old ydraw-layer mechanism and currently has no in-tree caller since the
  terminal moved to yvterm; the `apply_scroll_anchor`
  slot slides absolute-coord figures with the surrounding text.

## Relation to yvterm — deliberately parallel renderers

The terminal's scroll layer ([../yvterm/README.md](../yvterm/README.md),
`sdf-layer.c`) runs the same bucket + stage + draw pipeline but is a
deliberate sibling implementation, not accidental duplication: an earlier
architecture rendered the terminal's scrolling rich content through one
shared grid renderer, and that created more problems than it solved. The
two specialize differently — yvterm's grid/layer is built for scroll
integration (per-line anchoring, tiered scrollback, figure
eviction/re-materialization), while ygrid is built for dynamically
updatable primitive/composite entity groups (`CMD_GROUP` / `CMD_DELETE`
editing of live UI content). A fix to the shared drawing logic usually
needs to land in both.

## Public API sketch

```c
/* Hand-written surface (include/yetty/ygrid/ygrid.h). */
struct yetty_ygrid_grid_ptr_result yetty_ygrid_create(
    struct yetty_ycore_rectangle rect, uint32_t grid_cols, uint32_t grid_rows,
    const struct yetty_context *context);
struct yetty_yfigure_figure *yetty_ygrid_as_figure(struct yetty_ygrid_grid *grid);
struct yetty_ycore_void_result yetty_ygrid_add_record_local(
    struct yetty_ygrid_grid *grid, const uint8_t *record_bytes, size_t record_len);
struct yetty_ycore_void_result yetty_ygrid_set_font(
    struct yetty_ygrid_grid *grid, uint32_t slot, struct yetty_yfont_font *font);
struct yetty_ycore_void_result yetty_ygrid_register_factory(
    struct yetty_yfigure_registry *registry,
    const struct yetty_ygrid_factory_args *args);   /* kind token "ygrid" */

/* Generated yclass surface (include/yetty/ygrid/grid.h) — works local
 * and over RPC. */
struct yetty_ycore_void_result yetty_ygrid_add_record(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *obj);
```

`yetty_ygrid_register_factory_for_kind` registers the same renderer under an
arbitrary kind token so ygui's producer widgets (yplot / yimage / yvideo /
yzoo / yjungle) get distinct wire kinds.

## Generated vs hand-written

ygrid is a yclass module: the public class header
`include/yetty/ygrid/grid.h`, `grid.gen.c` (appended at the foot of
`grid.c`), `rpc.gen.c` and `model.yaml` are all **generated by
`make codegen`** from the `[[clang::annotate(...)]]` markers in `grid.c` —
never hand-edit them. See [../yclass/README.md](../yclass/README.md). The
hand-written pieces are `grid.c`, the non-class helper header
`include/yetty/ygrid/ygrid.h`, and `ygrid.wgsl`.

## File map

| file | role |
|------|------|
| `grid.c` | the whole implementation: storage, entity tree, bucketing, binder, fonts, render, yclass slot overrides |
| `grid.gen.c` / `rpc.gen.c` / `model.yaml` | codegen output — do not edit |
| `ygrid.wgsl` | layer shader (cell walk + SDF/glyph evaluation + post effect); staged to assets by [../yshaders](../yshaders/README.md) |
| `include/yetty/ygrid/grid.h` | generated class header |
| `include/yetty/ygrid/ygrid.h` | hand-written create / factory / font / scroll API |

## Consumers

- `../yterminal/terminal.c` — registers the ygrid factory on the pane's
  figure registry (kind `"ygrid"` plus the producer kind tokens); the
  terminal's scroll layer itself is yvterm's `sdf-layer.c`, not an ygrid.
- `../ychrome/host.c`, `../yguiapp/app.c`, `../yui/yui.c`, `../yrich/app.c` —
  ygui chrome grids (absolute coords) and producer-kind factories.
- `tools/ycompositor`, `tools/ygreeter`, `tools/yhello`, `tools/ybrowser`,
  `tools/yzoo`, `tools/ymaze`, `tools/yjungle` — hosts registering the
  factory.

## See also

- [../ysdf/README.md](../ysdf/README.md) — SDF types, sizes, AABBs, WGSL.
- [../yfigure/README.md](../yfigure/README.md) — figure base + registry.
- [../yrender/README.md](../yrender/README.md) — binder / resource sets.
- [../../../docs/gpu-resource-binding.md](../../../docs/gpu-resource-binding.md),
  [../../../docs/layered-rendering.md](../../../docs/layered-rendering.md).
