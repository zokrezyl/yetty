# yflame — flame-graph client

`yflame` turns **folded stack samples** into a flame graph rendered inline in
the terminal: nested rectangles where a box's width is proportional to how
often its call stack was sampled, and its height position is its depth in the
stack. It is the same visualization as the Linux `perf` / Brendan Gregg
FlameGraph tooling, drawn natively by yetty's ydraw layer.

`flame` is a **yclass class** (`class@yflame:flame`) — a client/frontend
object, **not** a backend figure. It parses the folded input, lays out the
call tree, and renders to a ydraw drawable list; the host (`yfigure` / the
scrolling canvas) is what actually displays it. Being a yclass class,
`make codegen` emits the public headers, the method dispatch, and `model.yaml`
— the contract the FFI / host-language bindings are generated from. The only
hand-written source is the annotated `flame.c`.

> **Tool & how-to:** see [`tools/yflame/README.md`](../../../tools/yflame/README.md)
> for the `yflame` CLI and the `yflame.sh` record-and-render wrapper.

## Why a module, not a yplot feature

A flame graph is a **labelled tree of rectangles**, not a continuous `f(x)`
curve, so it is deliberately *not* built on yplot. yplot evaluates math per
pixel on the GPU and draws lines; a flame graph needs CPU-side tree
aggregation, a recursive box layout, and text inside every box. The full
rationale is in the tracker (issue: *Flame graph visualization as a new
`yflame` figure module (not part of yplot)*).

Instead `yflame` follows **ydiagram's** model: it emits generic ydraw SDF box
and MSDF text primitives that the canvas already knows how to render. No
custom shader, no GPU code.

## Pipeline

```
folded text  →  call tree  →  recursive layout  →  ydraw boxes + labels  →  YDRAW_BIN OSC
   parse          dedup          x by samples,        SDF box per frame      yface envelope
                                 y by depth           + truncated label
```

### 1. Input: the folded format

One collapsed stack per line — `;`-separated frames, a space, then a sample
count:

```
main;parse;lex 42
main;eval;exec 128
```

The count is split off at the **last** space (frame names may contain
spaces). This is the lingua franca emitted by `stackcollapse-*`, async-profiler
(`-o collapsed`), py-spy, bpftrace, and others — so yflame is
profiler-agnostic and parses no profiler binary format.

### 2. Call tree

Each line adds its count to **every** frame along its path, so a node's value
is the total samples passing through it. Children are deduplicated by name and
sorted alphabetically (the canonical, stable, mergeable flame-graph order).

### 3. Layout

Recursive and O(n): the root spans the full width; each node's pixel span is
divided among its children proportional to their sample counts. Depth maps to
a row. Classic **flame** orientation puts the root at the bottom growing up;
**icicle** (`YETTY_YFLAME_FLAG_ICICLE`) puts the root at the top growing down.
The graph's height is derived from the deepest stack
(`(max_depth + 1) * frame_height`).

### 4. Rendering

One SDF box per visible frame plus a truncated MSDF label:

- **Colour** — the warm "flame" palette (red 205–255, green 0–230, blue 0–55),
  hashed per frame name (FNV-1a). Deterministic: the same function is always
  the same colour.
- **Labels** — truncated to fit the box using an approximate advance
  (`0.6 · font_size` per char, the same fallback ydiagram uses), clamped to a
  UTF-8 boundary so a multi-byte glyph is never split.
- **min_width** — boxes narrower than this (pixels) are skipped, so deep, wide
  graphs don't emit millions of sub-pixel rectangles.

## Public API (yclass class `yflame:flame`)

Generated into `include/yetty/yflame/` (`flame.h`, `methods.h`, `rpc.h`) from
the annotated `flame.c`. Every method takes a `yetty_yclass_ctx *` first — pass
`NULL` for the in-process/local path (every slot is `local@`, never RPC-proxied).

```c
/* lifecycle */
struct yetty_yclass_object_ptr_result yetty_yflame_flame_create(ctx);
struct yetty_ycore_void_result        yetty_yflame_destroy(ctx, obj);

/* configure + load */
struct yetty_ycore_void_result yetty_yflame_configure(ctx, obj,
    float width, float frame_height, float min_width, uint32_t flags);  /* 0 = default each */
struct yetty_ycore_void_result yetty_yflame_parse(ctx, obj, const char *input, size_t len);

/* render the current view (focus subtree + hover highlight) → fresh drawable list */
struct yetty_ydraw_drawable_list_result yetty_yflame_render(ctx, obj);

/* interaction (content-coordinate hit-test → node id; zoom; hover) */
struct yetty_ycore_int_result  yetty_yflame_hit_test(ctx, obj, float x, float y); /* id, or -1 */
struct yetty_ycore_void_result yetty_yflame_focus(ctx, obj, int32_t node_id);     /* zoom in   */
struct yetty_ycore_void_result yetty_yflame_reset(ctx, obj);                      /* zoom out  */
struct yetty_ycore_void_result yetty_yflame_set_highlight(ctx, obj, int32_t node_id);

/* one-shot: serialize a rendered list as a YDRAW_BIN envelope to an fd */
struct yetty_ycore_void_result yetty_yflame_emit_osc(const struct yetty_ydraw_drawable_list *list, int fd);
```

```c
#define YETTY_YFLAME_FLAG_LABELS 0x1u
#define YETTY_YFLAME_FLAG_ICICLE 0x2u
```

Two consumption modes share this one surface:

- **one-shot** — `render` → `emit_osc(STDOUT_FILENO)` ships a YDRAW_BIN envelope
  into the scrolling layer (like `ycat`); it ages out of scrollback.
- **interactive** — `render` → ship via `yview` (`YCOMPOSITOR_BIN`) as a
  positioned server figure; on mouse, call `hit_test`, then
  `set_highlight`/`focus`/`reset`, and re-`render`.

The width is configured; the **height is derived** from stack depth, not a free
parameter.

## Files

| File | Role |
|------|------|
| `flame.c` | the only hand-written source — annotated yclass class; `#include`s `flame.gen.c` at its foot |
| `flame.gen.c`, `methods.gen.c`, `rpc.gen.c`, `model.yaml` | codegen output — never hand-edited |
| `include/yetty/yflame/{flame,methods,rpc}.h` | generated public headers |
| `include/yetty/yflame/types.h` | hand-written: module result-type `#include`s the codegen needs (e.g. `drawable-list.h` for `render`'s by-value result) |
| `CMakeLists.txt` | the `yetty_yflame` static library |

## Build

GPU-less; links `yetty_ycore`, `yetty_yclass`, `yetty_yface`, `yetty_ydraw_core`,
`yetty_ysdf`. C23 (for the `[[clang::annotate]]` attributes). Gated by
`YETTY_ENABLE_FEATURE_YFLAME` (ON by default). Add `yflame` to `YCLASS_MODULES`
in the root Makefile so `make codegen` regenerates it. No third-party
dependencies — BSL-clean (it reads a data format, it does not incorporate any
profiler code).
