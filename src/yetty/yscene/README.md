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
  terminal's registry; `tools/yscene-demo` ships a scene figure end-to-end
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

Also done (second hardening round + complex leaves):
- One registry binding through every path (constructor dom + runtime +
  headless fallback + setter); rebind rejected once content exists.
- Transactional envelopes: semantic pre-validation (self-nested groups
  rejected before any mutation) + pending-poison on a partial apply —
  commits refuse until a full reset (CMD_ZERO / reset_content).
- Failure-atomic publish: derive builds into a scratch snapshot and
  swaps only on success.
- Font lifecycle: cache handles tracked per slot; reset/navigation
  releases wire fonts and clears producer-id maps; slot-0 default font
  arrives via the factory args bundle (terminal wires composite factory
  + compositor font).
- Complex (composite/image) leaves: instances minted at ingest (keyed
  by their immutable span location, so same-body CMD_UPDATE targets
  them), refreshed at staging, swept when their span leaves the
  committed scene, drawn after the prim pass in paint-key order
  (ygrid-parity interim — the z-run interleave that lets prims cover a
  complex is the follow-up). CMD_UPDATE routes to live instances.
- Gradient fills fold inherited opacity (linear + radial payload
  colors), not just the style-header colors.

Also done (first ygui producer migration — the ybrowser page):
- The browser page scrollarea renders into a retained `yscene` figure
  (`yetty_ygui_scrollarea_enable_scene`): the whole document ships once
  in document space (no viewport culling), the scene scrolls it on the
  GPU via `set_child_scroll` (a scroll tick re-ships nothing), and
  navigation resets with a leading CMD_ZERO. The ygui framework ships
  retained figure bodies WITHOUT the per-frame CMD_ZERO.
- PROGRESSIVE updates through the group-update contract: the embed
  splits the page into ~16 KiB chunks of whole records, each a group
  with a position-stable id; a body carries only the chunks whose bytes
  changed (replace-in-place keeps their paint depth), plus CMD_DELETEs
  for groups no longer vouched for. An in-place page mutation (JS
  ticker, image pixels arriving) ships one chunk, not the page; an
  unchanged frame ships nothing.
- The in-yetty client loop drives the ywire connection's reactor seam
  as the SINGLE stdin consumer (input/raw lane sinks + readable/
  writable pumps) — the previous private-yface reader raced the
  connection and ate WINDOW_ADJUST credit grants, deadlocking any body
  larger than the 256 KiB channel window (pages with images).
  Follow-ups: overlay scrollbar in the chrome layer (a viewport-fixed
  thumb cannot live inside the scrolled document), per-tab scroll
  save/restore, content-keyed (element-identity) chunking so an
  insertion re-ships only the insertion point instead of every later
  chunk.

Next increments: z-run interleave with per-run scissors, per-prim
effective clip in a yscene shader fork, the content-addressed resource
channel for heavy payloads (#691 "Heavy payloads"), exact render-plan /
logical-dom serialization, rotation/shear + anisotropic text in staging,
incremental (dirty-subtree) derive, compact leaf records, spatial
hit-test, yrdawn in-page adapter, remaining producer migration (ygui
chrome), then ygrid retirement per the #691 parity list.
