# yscene — retained scene graph (#691)

The retained-scene replacement for `ygrid`, built alongside it until
parity. One module, one public yclass class, one internal tree:

| piece | file | what it is |
|---|---|---|
| `scene` | `scene.c` | `class@yscene:scene`, `parent@yfigure:figure` — the **single public object**. Owns one dom, surfaces the whole content-mutation API (typed methods + the legacy wire-envelope adapter), derives world state, builds GPU staging, draws, hit-tests. |
| dom | `dom.c` + `internal.h` | The GPU-free retained content **tree** — a plain internal struct + functions, owned 1:1 by its scene, never RPC/FFI-exposed. Nodes (groups) carry placement + children + content batches; batches are immutable verbatim wire spans with generation-based retirement. |

Terminology: a **node** is what other code historically calls a
`CMD_GROUP` or an "entity". Drawables are wire records owned by nodes
through batches.

## Paint order

One total order over every drawable, the converged #691 key:

```
(paint_z, seq, record_index)
```

`seq` is minted from ONE dom-global counter for both nodes (at first
declaration) and batches (at batch creation), so the source order
"records, child node, more records" is directly expressible. Replacing
a batch (or re-emitting a node's content) PRESERVES its seq — re-emitted
content repaints at its original depth, which retires the ygrid
creation-order/flicker bug class by construction. The CPU sorts leaves
at derive; the shader never needs a z compare.

## Consistency model (single event-loop v1)

Producers mutate, then `commit`; **commit synchronously derives and
publishes the leaf snapshot before returning**, so mutations arriving
after a commit can never corrupt what that commit published. Replaced/
deleted spans are *retired*, not freed, until the commit-scoped derive
has consumed them and `reclaim`s — that is the whole locking story.
Scroll and view scale are scene view state: they never touch the dom
and never re-derive; the shader and hit-test share one mapping
(`document = screen / view_scale + scroll`).

## Status / roadmap

Done:
- dom: tree, immutable spans, seq, generations, dirty tracking with
  O(dirtied) clear, tombstone-purging id index.
- scene model layer: mutation surface, wire adapter, derive with
  transform/clip/opacity inheritance, hit-test, dump.
- GPU primitive rendering: staging from the sorted leaf array (buffer
  order IS paint order — no per-cell sort), 16×16 cull buckets, shared
  binder + ygrid.wgsl pipeline, world-baked translate/scale geometry,
  GPU-side scroll (`cz_off`).
- Wire integration: the `yscene` figure kind registers with the
  terminal's registry; `tools/yscene-demo` ships a card end-to-end
  (verified live: mint → adapter → derive → staging → draw).
- Headless tests: `test/ut/yscene/` (dom contract + scene behavior).

Also done (post-review hardening + the ychromium unblockers):
- Commit isolation (P0): every committing path derives + publishes the
  leaf snapshot synchronously; later pending mutations cannot corrupt
  it. Dirt/reclaim are commit-scoped.
- Atomic envelopes: process_bytes validates the whole body before any
  mutation; CMD_UPDATE / GROUP_REF / nested ZERO / over-deep nesting
  are rejected up front.
- One registry binding (constructor-created dom; late rebind rejected),
  staging keyed on generation + extent, view_scale drives the shader
  (`cz_scale`) so pixels and hit-tests agree, inherited opacity folds
  into staged colors, scroll-only frames upload nothing.
- FONT_RESOURCE installation (MSDF cache/slots/dispatcher rebuild) and
  TEXT_DRAWABLE_LIST→glyph expansion at staging — web pages' text and
  shipped fonts now reach the GPU. Glyph output is derived state only;
  the master spans stay verbatim.
- `yetty_yscene_render_plan()`: headless staging snapshot for tests.

Next increments: complex leaves (composite/yimage staging at their z
cut points, CMD_UPDATE routing, z-run interleave with per-run scissors,
yrdawn in-page adapter), per-prim effective clip in a yscene shader
fork, rotation/shear + anisotropic text in staging, incremental
(dirty-subtree) derive, compact leaf records, producer migration (ygui),
then ygrid retirement per the #691 parity list.
