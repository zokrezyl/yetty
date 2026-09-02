# ydraw client interface — target-API sketches

Sketches of the language surfaces of the ydraw client interface (the
`ydrawlist2` + `ysdf2` yclass modules and their generated bindings,
re-exported to Python as `yetty.ydraw`). Semantics throughout are the
ydraw-list producer model — one drawable list, immediate appends, a
dcs-emit producing the same `YDRAW_BIN` envelope the C tools emit.

Status: ALL FOUR language trees RUN against `libyetty_ffi.so` —
shapes/fonts/text (hello.*), plots incl. buffers and views (plot.* +
the 15-demo yplot corpus), and the complex kinds (yimage/ymesh/
yshadertoy/yvideo, one-shot records). The `hello` envelope is
byte-identical across python, lua, go and typescript. Runners (all
from the repo root — asset paths are repo-root-relative):

- python: `/usr/bin/python3 demo/ffi/ydraw/python/<demo>.py`
  (ctypes bindings under `bindings/python/`).
- lua: `LUA_PATH='bindings/lua/?.lua;;' luajit
  demo/ffi/ydraw/lua/<demo>.lua` (LuaJIT ffi).
- go: `CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi"
  LD_LIBRARY_PATH=<build>/src/yetty/yffi go run
  demo/ffi/ydraw/go/<demo>.go` (cgo, resolved via the root
  `go.work`).
- typescript: `npm install` once in `demo/ffi/ydraw/typescript/`,
  then `node demo/ffi/ydraw/typescript/<demo>.ts` (node ≥ 23
  type-stripping + koffi; `tsc -p demo/ffi/ydraw/typescript`
  typechecks the tree against the generated `ydraw.d.ts`).

Demos are grouped **per language**, one directory each, with identical
content: `python/` is the source of truth; `lua/`, `typescript/` and
`go/` mirror it (the 15 yplot demos are machine-translated from the
python files to prevent drift).

| subdirectory (inside each language) | covers |
|---|---|
| `hello.*` | the core surface: SDF shapes, fonts, text runs |
| `groups.*` | named entity groups: begin_group/end_group wrap content in GROUP(id); a live id re-emitted REPLACES in place (same anchor rows, no scroll), delete_group(id) removes the subtree (reserved rows stay); the timed loop shows in-place animation, panel-by-panel deletion, and the add/delete cycle where a re-used deleted id is fresh content at the cursor |
| `zorder.*` | the total paint order (paint_z, sequence) across primitives, text AND complexes: overlapping shapes emitted AGAINST the intended stacking (high z first, a z=-1 backdrop emitted last), equal-z ties resolved by emission order, text layer-interleaved with shapes, and a yplot complex with primitives cut under/over it at its sequence — each caption states the stacking the receiver must produce (python only) |
| `zanchor.*` | the replacement-anchor rule with a complex in the stack: BACK shapes, MID shapes and a FRONT yplot — only MID is re-emitted, staying sandwiched under the complex through 40 reopens, climbing above the plot at z=+1 (the interleave a prims-first renderer cannot produce), sinking below BACK at z=-1, returning to its exact slot at z=0, and leaving the stack on DELETE (python only) |
| `zplot.*` | `layer` is the ONE uniform z-order attribute on primitives, text AND complexes: `Box(layer=3)`, `Text(layer=9)`, `Plot(layer=5)` all identical. One list with two yplots and two boxes at layer=-1/1/3/5, added in scrambled order — the receiver's (layer, sequence) sort interleaves the plots between the boxes (BACK box < plot A < FRONT box < plot B) even though plot B is added first (python only) |
| `nested.*` | nested groups as PATHS (`[100]`, `[100.1]`, `[100.2]`): exact-subtree replace in place (re-emit the parent and the subtree becomes exactly what you sent — omit a child and it is gone), local-id scoping (a top-level `GROUP(1)` is the path `[1]`, not `[100.1]`), and whole-subtree delete (python only) |
| `twodialogs.*` | the same component instantiated twice with IDENTICAL internal local ids under distinct roots — paths `[500.1]` vs `[501.1]` never collide (the case that used to need a producer namespace); instance A animates in place while B never moves, then deletes independently (python only) |
| `lifecycle.*` | the robustness rules: anonymous content is cumulative and unaddressable, update/delete of a missing id are silent no-ops, a deleted id no-ops until re-used as fresh content, classical text written into a drawing's rows invalidates that insertion, and a plot scrolled fully into history seals — rendered in scrollback, permanently id-less (python only) |
| `pathstream.*` | absolute-path addressing of NESTED complexes: two plots with their own ids inside `GROUP(7)` — paths `[7.10000]`/`[7.10012]`; `dlist.path(prefix)` + `update`/`delete` targets exactly one of them at any depth (python only) |
| `viewportscroll.*` | the VIEWPORT primitive: `reserve(height_px)` declares the insertion span; 1160px of content in a 280px window, sent once; scrolling = updating the root group's OFFSET (~20 bytes/tick); panned-out content is detached yet stays addressable — the nested plot `[1,9]` streams via `path()` while out of view (python only) |
| `plotstream.*` | every complex carries an addressable `id` (`Plot(id=7)`, like a primitive). A later envelope pushes new data into that exact figure with `dlist.update(id, payload)` — this demo creates one plot with a data buffer and id, then streams a scrolling sine into it in place (no re-creation). The yplot update payload is `[buffer_index][sample_offset][count][f32 samples…]` (python only) |
| `plot.*` | plots as drawables + subplot layout by rect arithmetic |
| `yplot/` | all 15 `demo/scripts/yplot/*.sh` demos reproduced 1:1 (see `python/yplot/README.md` for the DSL → object mapping) |
| `yimage/` | yimage complex records from the PNGs in `demo/assets/yimage/` |
| `yvideo/` | yvideo complex records from the H.264 clips in `demo/assets/yvideo/` |
| `ymesh/` | ymesh complex records from the .glb models in `demo/assets/ymesh/` |
| `yshadertoy/` | yshadertoy complex records from the WGSL files in `demo/assets/yshadertoy/` |

The five complex kinds (`Plot` 0x80000003, `Image` 0x80000004, `Mesh`
0x80000005, `Video` 0x80000006, `Shadertoy` 0x80000007) each pack their
record via the kind's existing wire serializer — no new wire anywhere.

Language conventions: python/lua keep `snake_case` fields; typescript
uses `camelCase` and an options-object constructor argument; go uses
struct literals with `CamelCase` fields (each go file is a standalone
`package main` sketch). In typescript the imported `Function` class
shadows the JS global deliberately, for cross-language parity.
