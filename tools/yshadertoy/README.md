# yshadertoy — Shadertoy → WebGPU importer

Downloads a Shadertoy shader (via `curl`), resolves its render passes and
channel assets, converts each pass from GLSL to WGSL through Dawn's shader
toolchain, and — for single-pass, texture-free shaders — **draws it straight
into the current yetty terminal**:

```
Shadertoy GLSL ─[wrap]→ Vulkan GLSL ─[glslang]→ SPIR-V ─[tint]→ WGSL ─[ycat]→ yetty
```

Run it inside a yetty session and the shader appears inline as an animated
`yshadertoy` figure. `--no-render` skips that and just writes the files.

Tint has no GLSL front end, so the GLSL→SPIR-V hop uses Khronos `glslangValidator`
and the SPIR-V→WGSL hop uses Dawn's `tint` CLI (shipped in the dawn-exotic
release tarball, built with `TINT_BUILD_SPV_READER` / `TINT_BUILD_CMD_TOOLS`).

## Dependencies

- `curl` — every network fetch shells out to it.
- `glslangValidator` — from the distro `glslang-tools` package. Only needed for
  the conversion step (`--no-convert` skips it).
- `tint` — Dawn's CLI. Optional: if not found, the tool still emits the wrapped
  GLSL and validated SPIR-V and reports that the WGSL step was skipped. Point at
  it with `--tint PATH` or `$TINT`.

Pure Python 3 standard library otherwise — no third-party packages.

## Usage

```sh
# The target workflow — run inside yetty, shader appears inline:
export SHADERTOY_APPKEY=xxxxxxxx          # free key: shadertoy.com/howto#q2
./yshadertoy.py https://www.shadertoy.com/view/MdX3zr

# From a Shadertoy id or URL (needs the app key above)
./yshadertoy.py MdX3zr -o out/MdX3zr

# Substitute your own texture for a channel instead of downloading Shadertoy's
./yshadertoy.py https://www.shadertoy.com/view/MdX3zr \
    --channel0 ~/textures/noise.png

# From a local `mainImage` GLSL file (no download, no key) — handy offline
./yshadertoy.py --from-file myshader.frag -o out/myshader

# Fetch + assets only, or convert only one pass
./yshadertoy.py MdX3zr --no-convert
./yshadertoy.py MdX3zr --pass 'Buffer A'
```

The app key comes from `--api-key` or `$SHADERTOY_APPKEY`. A shader id or a
`/view/<id>` URL both work.

## Output layout

```
<out>/
  shader.json        # raw API response (download mode only)
  manifest.json      # normalized: passes, channels, assets, uniforms, tint path
  assets/            # downloaded media, or copied --channelN overrides
  passes/
    Image.glsl       # reconstructed compilable Vulkan GLSL
    Image.spv        # SPIR-V (glslang)
    Image.wgsl       # full WGSL module (tint) — the general/M5 form
    Image.yetty.wgsl # yetty-native `mainImage` — what ycat renders (no-texture passes)
    BufferA.glsl ...
```

## What the wrapper reconstructs

Shadertoy runs each pass with an implicit prelude the raw code depends on. The
tool rebuilds it so the pass compiles unchanged:

- a std140 uniform block with the standard inputs (`iResolution`, `iTime`,
  `iTimeDelta`, `iFrameRate`, `iFrame`, `iSampleRate`, `iMouse`, `iDate`,
  `iChannelResolution[4]`, `iChannelTime[4]`);
- `iChannel0..3` sampler declarations, typed per bound input
  (`sampler2D` / `samplerCube` / `sampler3D`);
- bare-name `#define` aliases (and `iGlobalTime`, `texture2D`, `textureCube`
  compatibility shims);
- a `main()` that flips `fragCoord` to Shadertoy's bottom-left origin and calls
  `mainImage`.

The `Common` pass is prepended to every other pass. `sound` and `cubemap`
passes are recorded in the manifest but skipped (not visual fragment passes).

## Rendering the result in yetty

yetty's `yshadertoy` prim accepts a self-contained WGSL
`mainImage(fragCoord, iResolution, iTime, iMouse) -> vec4<f32>` (uniforms as
parameters, no bindings). Tint emits a full module with a `main` entry point and
its own binding declarations instead, so the tool produces `Image.yetty.wgsl`:
it compiles the pass with the uniforms as plain globals (tint lowers them to
`var<private>`), strips tint's `@fragment` entry, and appends a `mainImage`
adapter that feeds yetty's parameters into those globals via tint's `main_inner`.
`ycat Image.yetty.wgsl` (run automatically inside yetty) draws it.

**Supported today:** single-pass, texture-free shaders. A shader that samples
`iChannel*` or uses Buffer A–D forces real texture bindings / multipass targets,
which this path can't feed — those passes are converted to the full `Image.wgsl`
module and skipped for rendering (`yetty   -> (skipped: … M5)`). Ingesting the
full module (channel textures + Buffer ping-pong) is the `yshadertoy` module
extension. The `manifest.json` carries the pass graph, channel types and asset
paths that
extension needs.
