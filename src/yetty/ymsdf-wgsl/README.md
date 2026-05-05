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
methods cover (using `tools/cdb-diff/cdb-diff`):

| metric                    | value     |
|---------------------------|-----------|
| glyphs with mean_diff <1% | 60%       |
| glyphs with mean_diff <5% | 96%       |
| bit-perfect matches       | 32 glyphs |
| avg max_diff              | 0.60      |
| avg mean_diff             | 0.014     |
| avg bad-pixel %           | 2.4%      |

`max_diff` reaching 1.0 on a glyph usually means a *single* corner pixel
disagrees by 100% (edge band vs. far-out) — the bulk of the bitmap matches.

### Remaining artefacts

Some curved glyphs (most visibly **C**, **G**, **J**, **S**) still show a
small triangular wedge of spurious "inside" pixels in concave openings.
The cause is the same one msdfgen addresses with its MSDF error-correction
pass — pixels where, due to a single channel picking a far segment with
the "wrong" sign, the rendered `median3` lands on the wrong side of 0.5.

Two fixes would close the remaining gap:

1. Port msdfgen's `MSDFErrorCorrection::distanceField` post-pass — it
   detects pixels where rendered `median3` disagrees with the winding-
   expected sign and rewrites those pixel values.
2. Add msdfgen-style pseudo-distance handling at corners. (A first
   attempt regressed: without error correction the pseudo-distance also
   leaks "near edge" values into far-outside pixels along tangent
   extensions. Pseudo-distance and error correction need to ship together.)

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
