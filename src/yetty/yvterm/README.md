# yvterm — terminal content internals: grid model, vterm figure, SDF + shader-glyph layers

`yvterm` is the terminal content of one pane, split into two yclass classes
whose names are easy to get backwards:

- `class@yvterm:grid` (grid.c) — the CPU-side MODEL. **This is the class
  that owns libvterm** (`vterm_new` runs here; the `VTerm*`/`VTermState*`
  live in its struct), plus the text-cell rings, the rolling rich store,
  tiered scrollback, and keyboard/PTY I/O.
- `class@yvterm:vterm` (vterm.c) — the GPU RENDERER, a
  [yfigure](../yfigure/README.md) named after the module, not after
  libvterm. It contains no libvterm state; it composes a grid instance
  (`grid_obj`) and draws it (text shader, rich passes), and its
  `yetty_yvterm_vterm_*` entry points are thin delegators into the grid.

The module's sole consumer is
[yterminal](../yterminal/README.md)`/terminal.c`, which creates the figure,
feeds it PTY bytes, and seats it in the pane's figure container. Main
dependencies: libvterm, [yclass](../yclass/README.md),
[ywire](../ywire/README.md), [yfont](../yfont/README.md),
[yrender](../yrender/README.md), ydraw-list/[ydraw-factory](../ydraw-factory/README.md), lz4.

## Split of concerns

**grid.c owns the one CPU-side truth**: text cells
(`struct yetty_yvterm_text_cell` — codepoint, glyph index, fg/bg, attrs,
wide-cell width) on two scrolling line rings — the primary screen (with
scrollback) and the alternate screen (which restarts its scroll origin on
every entry) — plus the **rolling rich store** (rich-store.c) that owns all
anchored rich content as addressable BLOCKS. libvterm drives the state
machine; keyboard input (`on_key`/`on_char`) and libvterm query responses go
back to the child through a registered `pty_write` callback. **vterm.c is
the renderer**: it reads the grid through bulk accessors (`line_cells`,
`slot_cells`, `slot_rich_block_count`, `slot_rich_block`,
`slot_rich_block_record`, …), uploads a 4-u32-per-cell buffer to the
text-grid shader (`grid-text.wgsl`, a staged shader asset — MSDF glyphs,
cursor, selection highlight, visual zoom, OSC-driven post-color/coordinate
effects), and draws each block's records at the block's own anchor. The
public `yetty_yvterm_vterm_*` model entry points are thin delegators to the
composed grid, so the terminal keeps one API surface.

## Rich content: the rolling rich store (rich-store.c)

The normative model — node kinds, addressing, operations, positioning,
lifecycle — is [drawable-use-cases.md](drawable-use-cases.md). This section
is the implementation map onto it.

**Units (HiDPI + structural zoom).** Every rich spatial value on the
wire — geometry, group offsets, clips, extents, `CMD_RESERVE` — is
**producer-logical pixels**, and the store retains it unscaled. Cell
metrics are **CURRENT framebuffer pixels** (font size bakes the window
content scale; structural cell zoom grows them further), so every
consumption point multiplies the logical side by ONE product — the
grid's `rich_density_scale` = display density × structural cell zoom,
pushed by vterm whenever either moves (`vterm_push_rich_density`; a live
density change goes through `yetty_yvterm_vterm_set_content_scale`,
which also rebases the zoom baseline so density growth is never
misclassified as cell zoom). Consumers: the ingest row conversion and
replacement span check (read back per envelope via
`vterm_rich_density`), per-node retirement, SDF bucketing/sampling (the
layer's density + cell-zoom uniforms: the ROW ANCHOR stays in current
framebuffer cells, only the primitive-LOCAL space scales — shaped
terminal glyph runs below the `rich_first` index are framebuffer-space
text and scale by 1), and complex placement (the same
`rich_local_scale` on bounds AND group-chain offsets, so SDF and
complex siblings translate identically). A footprint's row count is
thereby invariant under density transitions and structural zoom. The
headless harness (fixed cells) sets both halves through
`yetty_yterminal_ingest_harness_set_scale`.

One command batch (today: one DCS envelope) makes at most one **insertion**,
stored as one **rich block** — the unit of rolling placement, anchored at
its insertion rolling row and owned by its **bottom** line
(`relocate_rich_to_bottom` re-homes the fresh block there), so it leaves
scrollback only when its *last* covered line is evicted while still drawing
top-down from where its text sits. Its **row span** is
`max(1, ceil(content_bottom_px / row_height))` over the batch's NEW content;
a positive `CMD_RESERVE` **overrides** it (the declared viewport fixes the
span regardless of how tall the content is — but declares only: a
reserve-only batch is mutation-only and takes no placement; both inputs
are bounds-checked and ceil'd at ingest). The grid tracks the batch's
provisional insertion independently of the block the current mutation
scope routes into, so interleaved reopens of other blocks can never orphan
it. The span is installed BEFORE the cursor advance (`rich_span_declare`),
so the scroll the advance triggers sees the insertion's true coverage; the
advance itself is chunked with the block handle re-homed onto the cursor
line (`rich_reserve_advance`) so a ring-deep reservation cannot recycle
the line carrying it; on the alternate screen (no history) the advance
clamps at the last row so an insertion can never scroll away its own
content. The cursor then advances once per batch, past the span. A
block owns an ordered set of **records**
(verbatim wire word spans in the block's arena; a complex record
additionally owns its figure runtime) and the **node tree**: GROUP records
open container nodes (parent/child via `parent_slot`), each carrying a
mutable translation offset. Line rings anchor blocks by generation-checked
**handle** — moving or reflowing a line moves handles, never the
store-owned objects, and two blocks folded onto one line keep independent
anchors and spans.

**Addressing** — an address is the path of ids from the root, folded
segment-by-segment into one 64-bit key
(`yetty_yvterm_group_key_fold`, splitmix64 rolling fold —
`include/yetty/yvterm/group-key.h`; order- and depth-sensitive, root = 0).
The grid keeps ONE binding map `key → node`, where a node is either a GROUP
(store slot) or a COMPLEX (record index): groups and complexes MAY carry an
id, primitives never do. A complex gets its OWN id via a `CMD_NODE_ID`
prefix record (latches onto the next complex, survives interleaved prims,
dropped by a GROUP record or batch end); its key folds the enclosing group
path with that id. `CMD_PATH([ids...])` latches an absolute ancestor scope
for the NEXT update/delete, enabling mutation at any depth; without it the
command's id resolves in the ambient open-group scope.

**Operations** (contract verbs; wire-order, nontransactional — each command
applies as it parses, a failed command skips alone, previous state intact):

- **insert** — a GROUP record whose key is live replaces that node's WHOLE
  subtree in place (same anchor, same row span, no cursor movement, kind
  change allowed; omitted children disappear). Command-local atomicity: the
  replacement body is pre-scanned (`terminal_ydraw_subtree_bottom_px`) and
  an oversized replacement is skipped whole. A fresh key is new content in
  the batch's insertion. Reopen-only batches take no placement at all.
- **update** — dispatched by the target node's kind: a COMPLEX routes the
  payload to its runtime (`ops->update`; live plot samples, video NALs); a
  GROUP receives a state-field write — today `GROUP_FIELD_OFFSET`
  (`[field][f32 x][f32 y]`, absolute px). Anything else is a graceful
  no-op. Accepted complex updates are journaled per record (live-first
  budget: `set_update_journal_budget`; a ragged byte tail poisons the
  journal rather than replay a partial history) and replayed after the
  creation envelope when the figure re-materializes.
- **delete** — removes the node's subtree (descendants found via the
  `parent_slot` tree), keeps the block's row span.

**Positioning & projection** — group offsets are the ONE mover (coordinates
only, never width/height). A leaf's on-screen attachment is computed per
frame: local coordinates plus accumulated ancestor offsets, clamped to the
insertion's row span — so an app scrolls its own content by writing the
root group's offset (~20 bytes) while the terminal's rows never move.
Content pushed outside the span is detached (not painted) but STILL
addressable; complex runtimes survive movement, and their geometry stays
frozen from creation (movement belongs exclusively to groups).

Key mechanisms:

- **Coverage counters** — every line counts the live blocks whose span
  covers it (`rich_coverage_count`), so `putglyph`/erase consult the rich
  model only on covered rows: a write or erase intersecting ANY covered row
  removes the complete intersecting block (block-granular — no partially
  addressable remnant). Ordinary text output pays one flag check per row.
- **Scroll retirement — per NODE, sealing per block** — at each terminal
  scroll (`grid_seal_crossing_blocks` → `grid_retire_departed_nodes`, both
  screens), every addressable node whose projected footprint (subtree
  record extents + accumulated ancestor offsets, clamped to the span —
  extents stored at ingest via `append_primitive_extent`, pixels→rows via
  the retained `cell_height_px`) lies entirely above the new live top is
  PERMANENTLY retired: its binding stops resolving while its content stays
  rendered as frozen history. A retired group is a chain BARRIER
  (`group_chain_retired` / the retired stop in `group_dead`): its whole
  subtree freezes, and a live ancestor's reopen/delete sweeps only the
  live remainder. Detached (offset-moved-out) and unknown-extent nodes are
  exempt — offsets alone never retire anything. When the block's LAST span
  row crosses the live top (`top + span - 1 < new_live_top`, primary) the
  whole block seals: every remaining binding dies, and a re-used id is
  fresh content. Region scrolls (DECSTBM) carry wholly contained blocks
  with their text; boundary-cut blocks are destroyed, never smeared.

- **Paint order** — every record carries a persistent paint key
  `(paint_z, paint_sequence, record_ordinal)`: `paint_z` is the producer's
  explicit depth (decoded once at append — SDF z word, text `layer`,
  complexes default 0), `paint_sequence` comes from one monotone
  session-scoped domain, and the ordinal disambiguates records sharing a
  group's stable **replacement anchor** (a reopened group's new content
  keeps the replaced run's sequence with body-order ordinals — array
  position is never paint order). Record/group INDICES are snapshot
  values: they are stable within one mutation batch, but live reclamation
  (record compaction, group-slot reuse) between batches may re-pack or
  recycle them — re-query after any rich mutation; the paint key, not the
  index, is the persistent identity.
  The grid caches a per-screen **paint plan** (`paint-plan.h`): all
  resident render leaves sorted by key, holding only generation-checked
  handles + keys, invalidated by membership/key changes only (scrolling,
  sealing, runtime eviction and view changes reuse it). The renderer walks
  it once per frame into an execution list of contiguous primitive ranges
  alternating with own-pipeline complex draws — a range never crosses a
  complex cut point, so equal-z content composites strictly in
  sequence order across both kinds.

The creating wire envelope of every complex is retained verbatim in its
record, so an archived block rebuilds its figure runtime — updates included
— on demand through the `materialize` hook the terminal registers (it owns
the complex factory).

## Tiered scrollback (scroll-tiers.c)

```
newest ────────────────────────────────────────────────► oldest
[ HOT: line ring ][ WARM: lz4 segments in RAM ][ COLD: spill file ]
```

Lines aging out of the hot ring are serialized into segments (text runs plus
each anchored block's alive records, arena bytes, update journals,
complete paint keys and the record's FROZEN accumulated group offset —
tier format v5; materialization reproduces the stored keys exactly,
restores the block's real timeline anchors, and re-attaches the baked
offsets through synthetic cache-local groups, so archived content sorts
into the unified paint order exactly where it lived and projects at its
final (sealed) position; the group tree itself is not archived),
lz4-compressed under the `scrollback/warm-bytes` budget, then
spilled to a session-scoped temp file (`scrollback/file-max-bytes`, 0 =
unbounded — effectively infinite scrollback). Blocks materialized from the
archive are sealed and cache-local: they render, replay their journals, and
never re-register producer ids. Lines are addressed by a monotone **timeline index**;
`grid_live_anchor` / `grid_history_floor` bound the reachable range.
`yetty_yvterm_grid_view_window` resolves the renderer's per-frame window to
one slot id per row — real ring slots for hot rows, or extended ids served
transparently from the archive materialization cache (with prefetch in the
scroll direction so figure re-decode hides behind the scroll). Ring and
archive bytes are accounted with [ycore](../ycore/README.md) memtags
(`register_memtags` feeds the [yctl](../yctl/README.md) `memtags` dump).
Config keys are in [yconfig](../yconfig/README.md).

## Sub-renderers owned by the vterm figure

- **grid-sdf-layer.c** — rasterises the raw ydraw records (SDF shapes,
  GLYPH primitives, `TEXT_DRAWABLE_LIST` runs, FONT resources — the ycat
  PDF/SVG/markdown path), each block at its own per-block anchor. Driven
  by the paint plan through a frame protocol
  (`begin` / `stage_leaf` / `finish` / `draw_range`): vterm.c stages the
  visible primitive/text leaves in plan order, one upload backs the frame,
  and each ranged draw composites exactly one contiguous run (a run-bounds
  uniform filters the shader's per-cell prim walk) so complex draws
  interleave at their exact cut points. Every staged prim gets a 3-word
  header `[rolling_row][offset_x][offset_y]` carrying the resolved ancestor
  offsets; cell-bucketing applies the offset and clamps rows to the
  insertion's span, so span clipping and culling of detached content fall
  out of the bucket walk. The C↔WGSL header/offset layout is pinned by the
  `yvterm_sdf_layout` ctest (`test/ut/yvterm/sdf-layout-check.py`). Reuses
  the shared machinery (yrender gpu-resource-binder, the generated
  `ysdf.gen.wgsl`, the font dispatcher) but is standalone. Plain-C helper,
  not a yclass class.
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
| `grid.c` | the model class: rings, libvterm callbacks, input, selection, view, tier integration, the key→node binding map, offset accumulation (`paint_leaf_resolve`) |
| `rich-store.c` / `rich-store.h` | the rolling rich store: blocks + records + the group node tree (parent slots, offsets) + generation-checked handles + update journals (libvterm- and GPU-free) |
| `vterm.c` | the figure class: text shader + uniforms, per-block complex/SDF/shader-glyph passes, zoom + effects |
| `scroll-tiers.c` / `scroll-tiers.h` | archive engine: line (de)serialization, lz4 warm segments, cold spill file, materialization cache |
| `grid-sdf-layer.c` / `grid-sdf-layer.h` | SDF/glyph/text renderer for per-line raw ydraw records |
| `grid-shader-glyph-layer.c` / `grid-shader-glyph-layer.h` | PUA-B animated shader-glyph renderer |
| `grid-sdf-layer.wgsl` | the SDF layer's shader (staged shader asset) |
| `grid-text.wgsl` | the text-grid shader vterm.c compiles (staged shader asset; effects-lib.wgsl is prepended at load) |
| `grid.gen.c`, `vterm.gen.c`, `rpc.gen.c`, `model.yaml` | codegen output — never hand-edit ([yclass](../yclass/README.md)) |

Generated public headers: `include/yetty/yvterm/{grid.h,vterm.h}`;
hand-written: `grid-api.h`, `shader-glyph-pua.h`, `group-key.h` (the shared
path-fold used by ingest, grid and tests). The wire commands the ingest
consumes (`CMD_GROUP`, `CMD_NODE_ID`, `CMD_PATH`, `CMD_RESERVE`,
`CMD_UPDATE`/`GROUP_FIELD_OFFSET`, `CMD_DELETE`) are defined in
`include/yetty/ydraw-list/cmds.h`; the ingest itself (scope stack, latches,
span pre-scan, batch finalization) lives in
[yterminal](../yterminal/README.md)`/terminal.c`.

## Status

The per-line primitive/arena/complex storage that predated the rich store
is gone; every rich mutation (insert, update, delete, covered-row
invalidation, seal, archive round-trip) flows through the block + node
model above, per the [drawable-use-cases.md](drawable-use-cases.md)
contract. What each suite pins (all at the grid/store API layer):
`test/ut/yvterm/rich-lifecycle-test.c` — bindings, exact-subtree insert,
kind change, offset state + accumulation + input validation,
placement safety (span-before-advance, alternate-screen clamp,
mixed-order batches with interleaved reopens, ring-deep reservations,
DECSTBM region scrolls during the reserve advance),
sealing, cross-reopen paint-anchor inheritance; `test/ut/yvterm/scroll-tiers-test.c`
— archive round-trip incl. paint keys, timeline anchors and frozen group
offsets; `test/ut/ydraw-list/wire-test.c` — command framing incl. the
structural markers; the `yvterm_sdf_layout` ctest — the C↔WGSL staging
layout (a source-level contract check, not a render test). The full
terminal INGEST composition (envelope bytes → latches → routing →
placement → local chrome replacement) is covered by
`test/ut/yterminal/ingest-roundtrip-test.c` through the headless
harness (`yetty_yterminal_ingest_harness_open`, #728); staged RENDER
output remains render-stack territory. Demos:
`demo/ffi/ydraw/python/{nested,twodialogs,lifecycle,pathstream,
viewportscroll}.py`.

## Cross-references

- [drawable-use-cases.md](drawable-use-cases.md) — THE normative contract for rich-content addressing, operations, placement and lifecycle
- [yterminal](../yterminal/README.md) — the coordinator that owns and drives this module (and hosts the ydraw ingest)
- [ywire](../ywire/README.md) — the OSC/wire statemachine that delivers rich records
- [ydraw](../ydraw/README.md) — the rolling-row scroll model the anchoring follows
- [layered-rendering.md](../../../docs/layered-rendering.md), [gpu-resource-binding.md](../../../docs/gpu-resource-binding.md)
