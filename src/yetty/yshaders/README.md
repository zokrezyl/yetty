# yshaders — WGSL shader assets and build-time staging

yshaders contains no C code: it is a tree of WGSL files plus the CMake logic
that stages every shader asset — its own and other modules' — into
`<build>/assets/shaders/` (and the Android assets dir). At runtime the
engine resolves this directory through the config key `paths/shaders` and
loads individual `.wgsl` files from it.

## What CMake stages

`CMakeLists.txt` defines the `copy-shaders` target:

1. **Everything under this directory** (recursive glob, subdirectory
   structure preserved).
2. **Module shaders owned elsewhere**, copied flat into `assets/shaders/`:
   `yfont/{ms-raster,raster,ms-msdf,msdf}-font.wgsl`, `yscene/yscene.wgsl`,
   `yvterm/grid-sdf-layer.wgsl`, `yvterm/grid-text.wgsl`, `yrender/blend.wgsl`,
   `ysdf/ysdf.gen.wgsl` (the generated SDF library),
   `ymsdf-wgsl/shaders/msdf_gen.wgsl` (the GPU MSDF compute shader).
3. **Per-glyph shaders** from `yfont/glyph-shaders/*.wgsl` into
   `assets/shaders/glyph-shaders/` — the shader-glyph layer scans that
   directory at runtime and splices each file into its layer shader.

For Android the same set is additionally copied at configure time so the
APK packaging sees it; for webasm the staged assets are what the fetch path
serves.

## Live assets owned here

| file | consumed by |
|------|-------------|
| `effects-lib.wgsl` | the runtime effects library — `fx_post_apply()` (post-color: scanlines, crt, chromatic, matrix, thermal, glitch, …) and `fx_coord_apply()` (coordinate distortion: fisheye, swirl, jello, melt, …), each taking an effect index (0 = none) + 6 params. Prepended to the vterm text shader (`../yvterm/vterm.c`) and attached as a binder child resource set by `../yscene/scene.c`; both call sites must stay in sync with its signatures. Effect indices match the OSC protocol and `demo/scripts/effects/`. |

## Reference / legacy content

The rest of the tree predates the current binder-based architecture (it
references "card" storage and the old group-0/group-1 bind layout) or holds
the split-out sources of what is now merged elsewhere. Nothing in the C tree
loads these today, but they are still copied to assets:

| path | note |
|------|------|
| `effects/`, `post-effects/`, `pre-effects/` | one file per effect — the sources `effects-lib.wgsl` was consolidated from |
| `glyphs/` | Shadertoy-style glyph shader collection; the runtime set lives in `../yfont/glyph-shaders/`, and the [../yshadertoy](../yshadertoy/README.md) demo gallery was adapted from the `0xeff*` entries here |
| `gpu-screen.wgsl`, `terminal-screen.wgsl`, `cursor.wgsl` | earlier monolithic screen / cursor shaders |
| `ydraw-overlay.wgsl`, `ygui-overlay.wgsl`, `image-atlas-copy.wgsl` | earlier overlay / atlas-compute passes |
| `msdf_gen.wgsl` | stale copy — the staged, live one is `../ymsdf-wgsl/shaders/msdf_gen.wgsl` |
| `font/` | earlier font shading variants (bitmap, coverage, msdf, raster, vector-sdf); live font shaders are `../yfont/*.wgsl` |
| `lib/` | earlier shared snippets (distfunctions, text, util, yfsvm, `sdf-types.gen.wgsl`); the live generated SDF library is `../ysdf/ysdf.gen.wgsl`, the live VM shader is `../yfsvm/yfsvm.gen.wgsl` |
| `yvideo/bgra-to-yuv420.wgsl` | BGRA→YUV420 compute shader; `../yvcodec` currently converts on the CPU |

**Status:** this module is deliberately thin — an asset-staging point plus
one live library. When adding a new module shader, add it to the
`MODULE_SHADERS` list here (and keep the webasm embed list in
`build-tools/yetty/targets/shared.cmake` in sync, as the ysdf comment in the
CMakeLists notes).

## See also

- [../yrender/README.md](../yrender/README.md) — how shaders are compiled
  and bound (binder, resource sets).
- [../ysdf/README.md](../ysdf/README.md) — the generated SDF WGSL staged here.
- [../yfont/README.md](../yfont/README.md) — font shaders and glyph shaders.
- [../../../docs/webgpu-architecture.md](../../../docs/webgpu-architecture.md),
  [../../../docs/webgpu.md](../../../docs/webgpu.md).
