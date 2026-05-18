# Shader-glyph procedurals

Each `.wgsl` file in this directory defines one animated procedural glyph.
The `shader-glyph-layer` reads them at startup, sorts by local-id, and
splices a generated `render_shader_glyph(...)` switch dispatcher into
`src/yetty/yterm/shader-glyph-layer.wgsl` at the
`// SHADER_GLYPHS_PLACEHOLDER` marker.

Adding a glyph = drop a `.wgsl` file here and rebuild. No codegen step,
no list to update.

## File naming

```
0xNNNN-<name>.wgsl
```

`NNNN` is the local-id (lowercase hex). The runtime stores the glyph in a
cell using `glyph_index = 0xFFFFFFFF - local_id`. Codepoint U+100000 + N
maps to local-id N (see `text-layer.c::resolve_glyph`).

## Function contract

```wgsl
fn shader_glyph_<local_id>(
    uv: vec2<f32>,        // 0..1 within the cell
    time: f32,            // seconds since layer creation
    fg: vec3<f32>,        // cell foreground color
    bg: vec3<f32>,        // cell background color
    pixel_pos: vec2<f32>  // absolute pixel position (for tile-coherent effects)
) -> vec3<f32>
```

Return final RGB. The layer's fragment shader writes `vec4(rgb, 1.0)` for
shader-glyph cells, so anything you return is opaque on top of the cell.

Helpers belong in the same file, prefixed `shader_glyph_<local_id>_`,
e.g. `shader_glyph_3_hash`. Names must not collide across files — the
generated dispatcher merges all bodies into one WGSL module.

## Tile-coherent effects

For animations that should flow across tiled cells (plasma, wave,
sparkle), consume `pixel_pos`. For per-cell effects (spinner, heart),
just use `uv` and `time`.

## Adding the input route

To call a new glyph from a terminal, the codepoint must be in the
supplementary PUA-B range U+100000..U+100FFF. By default
`text-layer.c::resolve_glyph` maps the range 1:1 to local-ids. Adjust
`YETTY_SHADER_GLYPH_PUA_END` in `include/yetty/yterm/shader-glyph-layer.h`
if you need more than 4096 slots.

PUA-B was chosen after measuring the bundled DejaVuSansM Nerd Font:
  BMP PUA  (E000..F8FF) :  3,488 codepoints used (Powerline + Codicons)
  PUA-A    (F0000..FFFFD):  6,896 codepoints used (Material Design Icons,
                            U+F0001..U+F1AF0)
  PUA-B    (100000..10FFFD): 0 codepoints used
Earlier base addresses (U+E000, then U+F0000) collided with one of these
icon blocks; any Nerd Font icon whose codepoint matched a yetty shader
local-id stamped that cell with the shader-glyph marker, pinning the
60 Hz animation timer on for as long as the icon was visible.
