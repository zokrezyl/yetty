# ylottie — Lottie (Bodymovin subset) → ydraw buffer

`ylottie` renders a frame of a Lottie animation into a ydraw buffer of SDF
shape primitives and MSDF text spans — the same output shape as ysvg /
ymarkdown / ypdf, where the caller owns one buffer. It is fully
self-contained: it parses the JSON itself and flattens all geometry
without any external vector/animation library. Depends only on `ycore`,
`ydraw-list`, and `ysdf`.

Lottie is an animation format; this engine flattens the document **at a
chosen frame** into a static buffer. Single-frame callers (ycat) use the
one-shot entry; players use the animation handle — parse once, render
many frames.

## Pipeline

1. **JSON parse** (`ylottie-json.c`) — recursive-descent parser into a DOM
   tree; every node and string lives in a bump arena owned by the root, so
   teardown is O(chunks). The engine operates directly on the DOM — Lottie
   `{a, k}` property objects are read on the fly.
2. **Property evaluation** (`ylottie-prop.c`) — keyframe interpolation with
   per-keyframe cubic-bezier easing handles; affine-transform math.
   Spatial position tangents (`to`/`ti`) are not applied — exact at
   keyframe times, linear in between.
3. **Geometry flattening** (`ylottie-path.c`) — `sh` bezier shapes via
   adaptive cubic subdivision (same algorithm as ysvg); `sr` polystars
   analytically.
4. **Layer walk** (`ylottie-paint.c`) — layers painted back to front,
   groups carrying transform/opacity/fill/stroke inheritance:
   - ellipse → SDF circle / ellipse (flattened polyline when rotated)
   - rect → SDF box / rounded box (4-segment outline when rotated)
   - path / polystar → flattened closed polylines
   - text layers (`ty` 5) → MSDF `TEXT_DRAWABLE_LIST` entries
   The SDF set has no arbitrary-polygon fill, so filled free-form shapes
   are approximated by tracing the perimeter in the fill colour (the same
   compromise ysvg makes); ellipses and axis-aligned rects fill natively.

**Not covered:** image/precomp layers, masks, track mattes, effects,
gradients (approximated by their first stop colour), trim paths, blend
modes, merge/repeater/rounded-corner modifiers.

## Public API (`include/yetty/ylottie/ylottie.h`)

```c
/* one-shot (ycat's path) — args accepts --frame= / --time= / --bg= */
struct yetty_ylottie_render_result r =
    yetty_ylottie_render(content, len, NULL, 0, &config);
/* r.value.buffer is caller-owned; free with yetty_ydraw_drawable_list_destroy */

/* player path — parse once, render many frames */
struct yetty_ylottie_animation_ptr_result ar =
    yetty_ylottie_animation_create(content, len, &config);
struct yetty_ylottie_info info = yetty_ylottie_animation_info(ar.value);
for (float f = info.in_point; f < info.out_point; f += 1.0f)
    yetty_ylottie_animation_render_frame(ar.value, f, /*bg_abgr=*/0);
yetty_ylottie_animation_destroy(ar.value);

/* helpers */
yetty_ylottie_can_parse(content, len);  /* cheap sniff for ycat's detector */
yetty_ylottie_inspect(content, len);    /* composition header only */
```

Rendered buffers are self-contained — they do not reference the
animation, so the animation may be destroyed while emitted buffers are
still in flight.

## Consumers

- **ycat** — `yetty_ylottie_can_parse` in `../ycat/detect.c` routes Lottie
  JSON (including `.lottie`-named files) to `handler-lottie.c`, which
  renders a single still frame (the composition in-point) inline.
- **`tools/ylottie`** — plays the whole animation: one OSC clear +
  `YDRAW_BIN` envelope per frame, paced at the composition frame rate,
  with `--loop`, `--fps`, `--frame`, `--time` controls.

## Files

| file | role |
|------|------|
| `ylottie.c` | public entry points, arg parsing, scene-bounds policy |
| `ylottie-json.c` | arena-backed recursive-descent JSON parser → DOM |
| `ylottie-prop.c` | `{a,k}` property evaluation, keyframe easing, transforms, colour |
| `ylottie-path.c` | bezier / polystar flattening to polylines |
| `ylottie-paint.c` | layer + shape-group walk → SDF primitives and text |
| `ylottie-internal.h` | DOM types and cross-TU declarations |

## See also

- `../ysvg/README.md` — the sibling static-vector renderer this mirrors
- `../ythorvg/README.md` — ThorVG-based renderer that also loads Lottie (via `tvg::Animation`)
- `../ycat/README.md` — detection and inline display
- `../ydraw/README.md` — the drawable-list output format
