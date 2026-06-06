# yflame — flame-graph figure

`yflame` turns **folded stack samples** into a flame graph rendered inline in
the terminal: nested rectangles where a box's width is proportional to how
often its call stack was sampled, and its height position is its depth in the
stack. It is the same visualization as the Linux `perf` / Brendan Gregg
FlameGraph tooling, drawn natively by yetty's ydraw layer.

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

## Public API

`include/yetty/yflame/yflame.h`:

```c
struct yetty_yflame_render_config {
    float    bounds_x, bounds_y, bounds_w;  /* placement + width (px) */
    float    frame_height;                  /* px per level; 0 → 18 */
    float    min_width;                     /* skip narrower boxes; 0 → 0.5 */
    uint32_t flags;                         /* YETTY_YFLAME_FLAG_* */
};

#define YETTY_YFLAME_FLAG_LABELS 0x1u
#define YETTY_YFLAME_FLAG_ICICLE 0x2u

/* folded text → fresh ydraw drawable list (caller owns it) */
struct yetty_ydraw_drawable_list_result yetty_yflame_render(
    const char *input, size_t len, const struct yetty_yflame_render_config *config);

/* drawable list → YDRAW_BIN OSC envelope written to `out` */
struct yetty_ycore_size_result yetty_yflame_osc_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);
```

`bounds_h` is intentionally absent from the contract — the height is a function
of stack depth, not a free parameter.

## Files

| File | Role |
|------|------|
| `yflame.c` | parse + tree + layout + box/label emit + OSC emit |
| `include/yetty/yflame/yflame.h` | public API |
| `CMakeLists.txt` | the `yetty_yflame` static library |

## Build

GPU-less; links only `yetty_ycore`, `yetty_ydraw_core`, `yetty_ysdf`, and
`yetty_yface`. Gated by `YETTY_ENABLE_FEATURE_YFLAME` (ON by default). No
third-party dependencies — BSL-clean (it reads a data format, it does not
incorporate any profiler code).
