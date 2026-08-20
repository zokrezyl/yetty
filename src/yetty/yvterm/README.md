# yvterm — terminal content internals: grid model, vterm figure, SDF + shader-glyph layers

`yvterm` is the terminal content of one pane, split into two yclass classes:
`class@yvterm:grid` (grid.c — the model: libvterm text grid, anchored rich
content, tiered scrollback, keyboard/PTY I/O) and `class@yvterm:vterm`
(vterm.c — a [yfigure](../yfigure/README.md) that composes a grid instance
and renders it on the GPU). Its sole consumer is
[yterminal](../yterminal/README.md)`/terminal.c`, which creates the figure,
feeds it PTY bytes, and seats it in the pane's figure container. Main
dependencies: libvterm, [yclass](../yclass/README.md),
[ywire](../ywire/README.md), [yfont](../yfont/README.md),
[yrender](../yrender/README.md), ydraw-list/[ydraw-factory](../ydraw-factory/README.md), lz4.

## Split of concerns

**grid.c owns the one CPU-side truth**: text cells
(`struct yetty_yvterm_text_cell` — codepoint, glyph index, fg/bg, attrs,
wide-cell width), anchored primitives and complexes, on two scrolling line
rings — the primary screen (with scrollback) and the alternate screen (which
restarts its scroll origin on every entry). libvterm drives the state
machine; keyboard input (`on_key`/`on_char`) and libvterm query responses go
back to the child through a registered `pty_write` callback. **vterm.c is
the renderer**: it reads the grid through bulk accessors (`line_cells`,
`slot_cells`, `slot_complexes`, `slot_primitive_words`, …), uploads a
4-u32-per-cell buffer to the text-grid shader (`grid-text.wgsl`, a staged
shader asset — MSDF glyphs, cursor, selection highlight, visual zoom,
OSC-driven post-color/coordinate effects), and draws the anchored figures. The public `yetty_yvterm_vterm_*` model entry points
are thin delegators to the composed grid, so the terminal keeps one API
surface.

## Rich content on the grid

Figures and raw drawable records are anchored **per line**. A rich block is
re-homed onto its **bottom** line (`relocate_rich_to_bottom`), with its row
span recorded there, so it leaves scrollback only when its *last*
overlapping line is evicted while still drawing top-down from where its text
sits. The creating wire envelope of every complex is retained verbatim in
the line's arena, so an archived line can rebuild its figure runtime on
demand through the `materialize` hook the terminal registers (it owns the
complex factory).

## Tiered scrollback (scroll-tiers.c)

```
newest ────────────────────────────────────────────────► oldest
[ HOT: line ring ][ WARM: lz4 segments in RAM ][ COLD: spill file ]
```

Lines aging out of the hot ring are serialized into segments, lz4-compressed
under the `scrollback/warm-bytes` budget, then spilled to a session-scoped
temp file (`scrollback/file-max-bytes`, 0 = unbounded — effectively infinite
scrollback). Lines are addressed by a monotone **timeline index**;
`grid_live_anchor` / `grid_history_floor` bound the reachable range.
`yetty_yvterm_grid_view_window` resolves the renderer's per-frame window to
one slot id per row — real ring slots for hot rows, or extended ids served
transparently from the archive materialization cache (with prefetch in the
scroll direction so figure re-decode hides behind the scroll). Ring and
archive bytes are accounted with [ycore](../ycore/README.md) memtags
(`register_memtags` feeds the [yctl](../yctl/README.md) `memtags` dump).
Config keys are in [yconfig](../yconfig/README.md).

## Sub-renderers owned by the vterm figure

- **grid-sdf-layer.c** — rasterises the raw ydraw records stored per line (SDF
  shapes, GLYPH primitives, `TEXT_DRAWABLE_LIST` runs, FONT resources — the
  ycat PDF/SVG/markdown path). Reuses the shared machinery (yrender
  gpu-resource-binder, the generated `ysdf.gen.wgsl`, the font dispatcher)
  but is standalone — it does not use [ygrid](../ygrid/README.md). Plain-C
  helper, not a yclass class.
- **grid-shader-glyph-layer.c** — animated procedural "shader glyphs":
  PUA-B codepoints (U+100000..U+100FFF) render as per-cell fragment shaders
  (spinner, plasma, …) assembled from `<paths/shaders>/glyph-shaders/*.wgsl`.
  An event-loop timer repaints while any are on screen and self-stops when
  none are. `include/yetty/yvterm/shader-glyph-pua.h` carries the
  codepoint ↔ glyph-id mapping helpers shared with the text renderer.

## Public API sketch

```c
struct yetty_yclass_object_ptr_result obj_res = yetty_yvterm_vterm_figure_create(
    cols, rows, context, pty_write_fn, pty_ud, request_render_fn, rr_ud, mouse_sub_fn, ms_ud,
    clipboard_write_fn, clip_ud);                        /* OSC 52 clipboard writes out */

yetty_yvterm_vterm_feed(obj, bytes, len);            /* PTY output in */
yetty_yvterm_vterm_resize(obj, grid_size, cell_size);
yetty_yvterm_vterm_on_key(obj, key, mods);           /* keyboard -> PTY  */
yetty_yvterm_vterm_set_view_top(obj, active, view_top_total_idx);  /* scrollback view */
yetty_yvterm_vterm_get_selection_text(obj, &buffer);
yetty_yvterm_vterm_register_wire(obj, wire_sm);      /* ywire records -> grid */
```

The generated `grid.h` / `vterm.h` publish the full class surface;
`grid-api.h` is the hand-written terminal-facing subset.

## File map

| file | role |
|------|------|
| `grid.c` | the model class: rings, libvterm callbacks, input, selection, view, tier integration |
| `vterm.c` | the figure class: text shader + uniforms, complex/SDF/shader-glyph passes, zoom + effects |
| `scroll-tiers.c` / `scroll-tiers.h` | archive engine: line (de)serialization, lz4 warm segments, cold spill file, materialization cache |
| `grid-sdf-layer.c` / `grid-sdf-layer.h` | SDF/glyph/text renderer for per-line raw ydraw records |
| `grid-shader-glyph-layer.c` / `grid-shader-glyph-layer.h` | PUA-B animated shader-glyph renderer |
| `grid-sdf-layer.wgsl` | the SDF layer's shader (staged shader asset) |
| `grid-text.wgsl` | the text-grid shader vterm.c compiles (staged shader asset; effects-lib.wgsl is prepended at load) |
| `grid.gen.c`, `vterm.gen.c`, `rpc.gen.c`, `model.yaml` | codegen output — never hand-edit ([yclass](../yclass/README.md)) |

Generated public headers: `include/yetty/yvterm/{grid.h,vterm.h}`;
hand-written: `grid-api.h`, `shader-glyph-pua.h`.

## Status

Four headers under `include/yetty/yvterm/` — `text-layer.h`,
`text-scrollback.h`, `ydraw-content.h`, `osc-args.h` — are leftovers from
the earlier yrender terminal-layer architecture: nothing in the tree
includes them and their functions have no implementation. The live
scrollback and OSC paths are the ones described above.

## Cross-references

- [yterminal](../yterminal/README.md) — the coordinator that owns and drives this module
- [ywire](../ywire/README.md) — the OSC/wire statemachine that delivers rich records
- [ydraw](../ydraw/README.md) — the rolling-row scroll model the anchoring follows
- [layered-rendering.md](../../../docs/layered-rendering.md), [gpu-resource-binding.md](../../../docs/gpu-resource-binding.md)
