# yzoo — animated control-point zoo test scene

`yzoo` animates a population of control points: they spawn around the
scene centre, drift outward with exponential growth, connect to their
nearest neighbours, and get culled at the viewport edge. Connections
render as line-segment-approximated quadratic beziers, straight
segments, or one of 22 SDF shapes placed at the connection midpoint. A
pure producer of ydraw primitives — no IO, no GPU, no terminal
interaction; the frontend drives time and owns the buffer. Port of the
yetty-poc `ydraw-zoo/zoo-renderer.cpp`; depends only on `ydraw-core` and
`ysdf`.

## How it works

Each `render(time)` call spawns points until `target_points` are alive,
drifts every point outward at `growth_rate`, culls points past the scene
edge, maintains ~`target_conns_per_cp` connections per point (dropping
any beyond `max_connection_dist`), then emits all primitives for the
current time. CP markers grow with age from `cp_marker_size`;
`curve_ratio` decides the fraction of connections drawn as bezier
polylines (`bezier_segments` line segments each). The buffer is cleared
first and its scene bounds set to `(0, 0, scene_width, scene_height)`.

Differences from the C++ original (documented in `yzoo.c`):

- splitmix64 PRNG instead of `std::mt19937` (matches `ymaze.c`);
- curves approximated as straight-segment polylines — the ysdf API has no
  quadratic-bezier primitive;
- shape connections pick from the 22 ysdf shapes in this codebase, not
  the old 43-shape table;
- `bg_color` stays in the config but is not painted — the layer
  underneath provides the background (same convention as ymaze).

Config values are clamped to the C++ argv-parser bounds (2–100 points,
1–10 connections per CP, growth 0.05–3.0, and so on).

## Public API (`include/yetty/yzoo/yzoo.h`)

```c
struct yetty_yzoo_config cfg = yetty_yzoo_config_default();
struct yetty_yzoo_ptr_result zr = yetty_yzoo_create(&cfg, /*seed=*/0);

/* per frame: spawn/cull/connect, then emit everything */
yetty_yzoo_render(zr.value, buf, time_seconds);

yetty_yzoo_set_scene_size(zr.value, width, height);
yetty_yzoo_destroy(zr.value);
```

## Consumers

- **`tools/yzoo`** — standalone GPU app: yclass class `yzoo:app`
  (subclass of `yapp:app`, annotated source `tools/yzoo/app.c`; generated
  header `include/yetty/yzoo/app.h`) renders the zoo into a full-window
  ygrid figure every frame.
- **ygui `yzoo` widget** (`../ygui/widgets/yzoo.c`) — wraps
  `yetty_yzoo_render` into a `ydraw_embed` widget, self-dirtying after
  each emit so the critters keep moving; exercised by
  `demo/ygui/28_yzoo`.

## Files

| file | role |
|------|------|
| `yzoo.c` | the whole engine: point lifecycle, connection upkeep, per-frame emit |

## See also

- `../ymaze/README.md`, `../yjungle/README.md` — the sibling animated test scenes
- `../ydraw/README.md` — drawable lists and the layers that display the output
- `../ysdf/README.md` — the 22-shape SDF set used for shape connections
