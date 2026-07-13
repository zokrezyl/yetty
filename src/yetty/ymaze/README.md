# ymaze — animated maze test scene (generate + solve → ydraw prims)

`ymaze` generates a maze, solves it, and renders the animated solve as ydraw
SDF primitives. It is a pure producer — no IO, no GPU, no terminal
interaction: each `render(time)` fills a caller-owned
`yetty_ydraw_drawable_list`, and the frontend owns time and the buffer. A
port of the yetty-poc `ydraw-maze` C++ renderer; depends only on
`ydraw-core` and `ysdf`.

## How it works

1. **Generate** — a recursive-backtracker DFS carves a `cols × rows` grid
   (one byte per cell, an OR of the standing N/S/E/W walls).
2. **Solve** — BFS from (0,0) to (cols-1, rows-1) yields the solution path.
3. **Render** — each call emits the walls, start/end markers, an animated
   trail, and an actor circle interpolated along the path at `actor_speed`
   cells per second. The buffer is cleared first and its scene bounds set
   to `(0, 0, scene_width, scene_height)`.
4. With `auto_regen` on, a finished path triggers a fresh maze on the next
   render; `*out_regenerated` reports when that happened.

The RNG is an inline splitmix64 (seed 0 → seeded from `CLOCK_MONOTONIC`).
Config values are clamped to the same bounds the C++ argv parser used
(3–80 cols, 3–50 rows, speed 0.5–50, wall width 0.3–5). `bg_color` is
advisory — the layer underneath provides the background.

## Public API (`include/yetty/ymaze/ymaze.h`)

```c
struct yetty_ymaze_config cfg = yetty_ymaze_config_default();
struct yetty_ymaze_ptr_result mr = yetty_ymaze_create(&cfg, /*seed=*/0);

/* per frame: */
bool regenerated = false;
yetty_ymaze_render(mr.value, buf, time_seconds, &regenerated);

/* on host resize / on demand: */
yetty_ymaze_set_scene_size(mr.value, width, height);
yetty_ymaze_regenerate(mr.value);

yetty_ymaze_destroy(mr.value);
```

All fallible entry points return Result types (`yetty_ymaze_ptr_result`,
`yetty_ycore_void_result`).

## Consumers

- **`tools/ymaze`** — standalone GPU app: yclass class `ymaze:app`
  (subclass of `yapp:app`, annotated source `tools/ymaze/app.c`) renders
  the maze into a single full-window ygrid figure every frame, paced at
  ~30 fps. The generated public header of that class lands at
  `include/yetty/ymaze/app.h`; the library in this directory is plain C,
  not a yclass class.
- **ygui `ymaze` widget** (`../ygui/widgets/ymaze.c`) — wraps
  `yetty_ymaze_render` into a `ydraw_embed` widget that re-marks itself
  dirty after every emit so the actor keeps animating; exercised by the
  ygui demos (`demo/ygui/40_ymaze`).

## Files

| file | role |
|------|------|
| `ymaze.c` | the whole engine: DFS generation, BFS solve, per-frame primitive emit |

## See also

- `../yzoo/README.md`, `../yjungle/README.md` — the sibling animated test scenes
- `../ydraw/README.md` — drawable lists and the scrolling canvas that display the output
- `../ysdf/README.md` — the SDF primitive set the walls/markers are built from
- `../ygrid/README.md` — the figure the standalone app renders into
