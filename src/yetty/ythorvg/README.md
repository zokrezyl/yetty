# ythorvg — SVG + Lottie via ThorVG → ydraw primitives

`ythorvg` renders SVG and Lottie (Bodymovin) content by plugging a custom
backend into [ThorVG](https://github.com/thorvg/thorvg): instead of
rasterising, it emits ydraw SDF primitives into a
`yetty_ydraw_drawable_list`. The implementation is C++ (ThorVG's
`RenderMethod` interface) but the public surface is a C API
(`include/yetty/ythorvg/ythorvg.h`), so the rest of the codebase never sees
C++. Its consumer is the `tools/ythorvg` CLI.

## How it works

`YDrawRenderMethod` (`ydraw-render-method.{hpp,cpp}`) subclasses
`tvg::RenderMethod`. ThorVG's `prepare()` calls snapshot each shape (path
commands, world transform, fill/stroke/gradient, dash pattern) into a
`YDrawRenderData`; `renderShape()` then tries progressively cheaper
emissions:

1. exact ellipse / circle detection → SDF ellipse/circle
2. axis-aligned (optionally rounded) rectangle → SDF box
3. two-stop gradient box (linear/radial approximated by its endpoint colours)
4. polygon → fan-triangulated mesh fill + edge segments for the stroke
5. general path → flattened segments (dash-aware)

Compositing/masking (`target`/`beginComposite`) and render effects are stub
overrides — content relying on masks or filters degrades. Gradients collapse
to their two endpoint colours.

`ythorvg.cpp` wraps this in the C API: it owns the ThorVG engine lifetime
(ref-counted `tvg::Initializer::init/term` across all renderers) and drives
a `tvg::Picture` inside a `tvg::Animation` so Lottie frame stepping works.
MIME auto-detection sniffs `<svg`/`<?xml` vs Lottie's `"v"` + `"layers"`
keys.

## Public API

```c
#include <yetty/ythorvg/ythorvg.h>

struct yetty_ythorvg_renderer_ptr_result r =
    yetty_ythorvg_renderer_create(buffer);   /* buffer borrowed, must outlive */
yetty_ythorvg_renderer_set_target(r.value, 800, 600);
yetty_ythorvg_renderer_render(r.value, data, size,
                              NULL /* auto-detect "svg"/"lottie" */,
                              &width, &height);          /* frame 0 */
yetty_ythorvg_renderer_render_frame(r.value, 12.0f);     /* Lottie only */
float frames = yetty_ythorvg_renderer_total_frames(r.value);
yetty_ythorvg_renderer_destroy(r.value);
```

## Layout of the module

| file | role |
|------|------|
| `ythorvg.cpp` | C API wrapper, engine ref-counting, Animation/Picture driving, MIME sniff |
| `ydraw-render-method.cpp` / `.hpp` | the `tvg::RenderMethod` backend: shape detection, triangulation, dashes, gradients |

Build: `yetty_ythorvg` (C++20 static lib), gated by
`YETTY_ENABLE_FEATURE_YTHORVG` **and** `YETTY_ENABLE_LIB_THORVG`. It
compiles against ThorVG's internal headers (`tvgRender.h`, `tvgPaint.h`)
shipped by the 3rdparty tarball.

## Status

Fully implemented (module-map entries describing it as a C-interface-only
stub are stale) — ported from the C++ POC with `YDrawBuffer` replaced by
`yetty_ydraw_drawable_list`. Known gaps: no compositing/masking, no render
effects, two-colour gradient approximation.

## Consumers

- **tools/ythorvg** — CLI: renders an SVG or Lottie frame and emits the
  buffer as a `YETTY_DCS_YDRAW_BIN` sequence (`--lottie --frame N`,
  `--clear`, `-w/-h`).

Note: ycat does **not** route through ythorvg — its SVG and Lottie handlers
use the independent [../ysvg/README.md](../ysvg/README.md) and
[../ylottie/README.md](../ylottie/README.md) modules. ythorvg is the
ThorVG-fidelity alternative path.

## Related

- [../ydraw-core/README.md](../ydraw-core/README.md) — the buffer being
  filled
- [../ysdf/README.md](../ysdf/README.md) — SDF primitive types emitted
