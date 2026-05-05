# MSDF-WGSL

GPU-based MSDF (Multi-channel Signed Distance Field) font atlas generation using WebGPU compute shaders.

## Status: Approaching parity with msdfgen

Pipeline works end-to-end; the output now closely matches the CPU msdfgen reference for most glyphs. Run `tools/cdb-diff` against the CPU output to see remaining divergence per glyph.

### What Works

- WebGPU compute shader pipeline for MSDF generation
- FreeType glyph outline decomposition (line segments + quadratic beziers)
- Glyph serialization to GPU buffers (points, metadata)
- **msdfgen-compatible edge coloring** (port of `edgeColoringSimple`) — each
  segment is assigned YELLOW/MAGENTA/CYAN so adjacent runs across a corner
  share one channel and `median3` recovers sharp corners. Smooth contours
  (no corners) get WHITE; single-corner contours use the teardrop pattern.
- **Correct sign convention at quadratic-segment t=0** (the original code
  was inverted relative to t=1 / interior / line, which produced sign
  discontinuities at every corner where segment N's t=0 met segment N-1's t=1).
- Atlas texture generation and readback
- CDB file export for use with yetty's msdf-font-cache
- Render-atlas viewer tool for debugging
- Headless CLI (`tools/gen-msdf-gpu/yetty-ymsdf-gen-gpu`) for generating
  CDBs without a window — useful for diffing against the CPU output.

### Diff metrics (DejaVuSansMNerdFontMono-Regular @ 32px, range=4)

Versus the CPU msdfgen-based reference, on the 1404 codepoints both
methods cover (using `tools/msdf/cdb-diff`):

| metric                       | value      |
|------------------------------|------------|
| glyphs with `max_diff` < 0.5 | **99.2%**  |
| glyphs with `mean_diff` < 1% | **89%**    |
| glyphs with `mean_diff` < 5% | **99.6%**  |
| bit-perfect matches          | 32 glyphs  |
| avg `max_diff`               | 0.24       |
| avg `mean_diff`              | 0.005      |
| avg bad-pixel %              | 1.4%       |

Remaining differences on the worst 0.8% of glyphs are isolated
single-pixel disagreements at curve anti-aliasing — f32 GPU math vs.
msdfgen's f64 reference.

### MSDF error correction (winding-based)

The shader runs an in-pass error-correction step that kills the
historic wedge artefacts on C/G/J/S/etc. concave openings. While
walking the segments it accumulates two extra quantities per pixel:

- a plain `min_abs_sdf` — the closest distance to *any* segment
  (no edge-colour mask);
- `winding_count` — signed crossings of the +x ray from the pixel
  with each segment, computed on segments using the endpoint-side
  convention `(p0.y > p.y) != (p_end.y > p.y)`. That correctly
  ignores tangent kisses (which would otherwise double-count from
  `disc≈0` in the quadratic root finder) and resolves shared
  vertices on the ray with no double-count.

After the segment loop, sign-from-winding × `min_abs_sdf` gives a
*geometrically* correct reference distance. When `median3(R,G,B)`
disagrees with that reference's sign, RGB is overwritten with it.
Pixels that already have the right sign are unchanged, so corner
anti-aliasing from the per-channel MSDF is preserved.

## Suspected Issues

### 1. Quadratic Bezier Distance Calculation

The `distance_to_quad()` function in `shaders/msdf_gen.wgsl` solves a cubic equation to find the closest point on the curve. Potential issues:

- **Cubic solver edge cases**: The depressed cubic solver may have numerical precision issues near discriminant boundaries (WGSL is f32; msdfgen uses f64)
- **Special-case degeneracy**: msdfgen's `solveCubic` factors out a t=0 root when `|d| ≈ 0` and falls back to a quadratic when `|a| ≈ 0`. The shader's solver has the `|a|≈0` fallbacks but not the `|d|≈0` factoring.

### 2. Corner/Junction Handling — partly fixed

Sign discontinuities at corners between adjacent segments are largely
addressed by the corrected t=0 sign convention plus edge coloring. What
remains is msdfgen's MSDF error-correction pass — see *Remaining
artefacts* in the status section above.

### 3. Endpoint Behavior — fixed for quadratics at t=0

The original code at `distance_to_quad`'s t=0 used
`-sign(cross(ab, qa))` (where `qa = p0 - origin`), which is the
*opposite* sign convention from `distance_to_line`, the quadratic's t=1
endpoint, and the quadratic's interior case. Now corrected to
`-sign(cross(ab, origin - p0))` so all four cases agree.

### 4. Winding Direction

The per-contour winding value is computed and stored but the shader
doesn't apply it explicitly — the sign already comes from each segment's
cross product, and on a properly-orientated shape (`Shape::orientContours`
in msdfgen, equivalent winding-based reordering on our side) that is
sufficient. Multi-contour shapes with overlapping contours (rare in
fonts) are not yet handled.

## Architecture

```
msdf-wgsl/
├── src/
│   └── msdf-wgsl.cpp      # Main library - FreeType integration, GPU pipeline
├── shaders/
│   └── msdf_gen.wgsl      # Compute shader - distance calculations
├── gen-cdb/
│   └── main.cpp           # CLI tool to generate CDB files
├── render-atlas/
│   └── main.cpp           # Viewer tool for debugging atlas
└── README.md
```

## Key Functions in Shader

### `distance_to_line(p0, p1, origin) -> vec3<f32>`
Returns (signed_distance, orthogonality, parameter_t)

### `distance_to_quad(p0, p1, p2, origin) -> vec3<f32>`
Solves cubic equation for closest point on quadratic bezier.
Returns (signed_distance, orthogonality, parameter_t)

### Sign Convention
- Negative distance = inside the glyph
- Positive distance = outside the glyph
- Sign determined by cross product of edge tangent with vector to origin

## Next Steps to Debug

1. **Isolate the bezier issue**: Create test glyphs with known simple curves and verify the math

2. **Visualize raw distances**: Modify viewer to show R/G/B channels separately to see which channel has artifacts

3. **Compare with reference**: Generate same glyphs with msdfgen and compare pixel values

4. **Add debug output**: Print intermediate values for a specific pixel to trace the calculation

5. **Review cubic solver**: The `solve_cubic_depressed()` function handles three cases based on discriminant - verify each branch

## Building

```bash
make build-desktop-ytrace-release
```

This builds, among other things, both MSDF generators side-by-side:

- `yetty-ymsdf-gen` (CPU/msdfgen reference)
- `yetty-ymsdf-gen-gpu` (this directory's WGSL compute shader path)

…plus the diff and viewer tools.

## Usage

```bash
# CPU reference
./build-desktop-ytrace-release/src/yetty/ymsdf-gen/yetty-ymsdf-gen \
    --size 32 --range 4 \
    assets/fonts/DejaVuSansMNerdFontMono-Regular.ttf \
    /some/cpu/output/dir

# GPU (this implementation)
./build-desktop-ytrace-release/tools/gen-msdf-gpu/yetty-ymsdf-gen-gpu \
    --shader build-desktop-ytrace-release/assets/shaders/msdf_gen.wgsl \
    --size 32 --range 4 \
    assets/fonts/DejaVuSansMNerdFontMono-Regular.ttf \
    /some/gpu/output/dir/font.cdb

# Compare them — ranks glyphs by per-pixel divergence
./build-desktop-ytrace-release/tools/cdb-diff/cdb-diff \
    -n 30 -p \
    /some/cpu/output/dir/font.cdb \
    /some/gpu/output/dir/font.cdb

# Inspect a single glyph from either CDB at full resolution
./build-desktop-ytrace-release/tools/cdb-viewer/cdb-viewer \
    -P -r 0x43:0x43 /some/output/dir/font.cdb
```
