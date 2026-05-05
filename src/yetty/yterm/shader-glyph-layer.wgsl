// =============================================================================
// Shader-Glyph Layer — animated procedural glyphs.
// =============================================================================
//
// Reads the same cell buffer the text-layer uploads (12 bytes per cell) and
// renders an animated procedural for every cell whose glyph_index >= the
// shader-glyph base. All other cells output transparent so the layer composes
// cleanly on top of text-layer.
//
// Per-glyph functions and the dispatcher are not in this file. The layer's
// C code reads src/yetty/yfont/glyph-shaders/0xNNNN-name.wgsl files at startup,
// generates a `render_shader_glyph(local_id, uv, time, fg, bg, pixel_pos) -> vec3<f32>`
// dispatcher, and substitutes the marker below at shader-load time.
//
// Generated constants (prepended by binder):
//   uniforms.shader_glyph_grid_size
//   uniforms.shader_glyph_cell_size
//   uniforms.shader_glyph_time
//   uniforms.shader_glyph_visual_zoom_scale
//   uniforms.shader_glyph_visual_zoom_off
//   shader_glyph_cells_offset

// RENDER_LAYER_BINDINGS_PLACEHOLDER

struct VertexInput  { @location(0) position: vec2<f32>, };
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    // Grid-local pixel position. See text-layer.wgsl for the rationale —
    // @builtin(position) in fragment is in framebuffer space and doesn't
    // line up with the grid origin once the pane viewport sits at
    // (vp.x, vp.y) != (0,0).
    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.position, 0.0, 1.0);

    let grid_size = uniforms.shader_glyph_grid_size;
    let cell_size = uniforms.shader_glyph_cell_size;
    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;
    output.grid_pixel = vec2<f32>(
        (input.position.x * 0.5 + 0.5) * grid_pixel_w,
        (0.5 - input.position.y * 0.5) * grid_pixel_h
    );
    return output;
}

// 12-byte cell layout — same as text-layer.wgsl.
fn read_cell_glyph(cell_index: u32) -> u32 {
    return storage_buffer[shader_glyph_cells_offset + cell_index * 3u];
}

fn read_cell_fg(cell_index: u32) -> vec3<f32> {
    let packed = storage_buffer[shader_glyph_cells_offset + cell_index * 3u + 1u];
    return vec3<f32>(
        f32( packed        & 0xFFu) / 255.0,
        f32((packed >>  8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0
    );
}

fn read_cell_bg(cell_index: u32) -> vec3<f32> {
    let packed1 = storage_buffer[shader_glyph_cells_offset + cell_index * 3u + 1u];
    let packed2 = storage_buffer[shader_glyph_cells_offset + cell_index * 3u + 2u];
    return vec3<f32>(
        f32((packed1 >> 24u) & 0xFFu) / 255.0,
        f32( packed2         & 0xFFu) / 255.0,
        f32((packed2 >>  8u) & 0xFFu) / 255.0
    );
}

const SHADER_GLYPH_BASE: u32 = 0x80000000u;

fn shader_glyph_local_id(glyph_index: u32) -> u32 {
    return 0xFFFFFFFFu - glyph_index;
}

// SHADER_GLYPHS_PLACEHOLDER

// =============================================================================
// Fragment Shader
// =============================================================================
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let grid_size = uniforms.shader_glyph_grid_size;
    let cell_size = uniforms.shader_glyph_cell_size;
    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;

    let vz_scale = uniforms.shader_glyph_visual_zoom_scale;
    let vz_off   = uniforms.shader_glyph_visual_zoom_off;
    let vz_center = vec2<f32>(grid_pixel_w * 0.5, grid_pixel_h * 0.5);
    let pixel_pos = (input.grid_pixel - vz_center) / max(vz_scale, 0.0001)
                  + vz_center + vz_off;

    if (pixel_pos.x < 0.0 || pixel_pos.y < 0.0 ||
        pixel_pos.x >= grid_pixel_w || pixel_pos.y >= grid_pixel_h) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let cell_col = floor(pixel_pos.x / cell_size.x);
    let cell_row = floor(pixel_pos.y / cell_size.y);
    let cell_index = u32(cell_row) * u32(grid_size.x) + u32(cell_col);

    let glyph = read_cell_glyph(cell_index);
    if (glyph < SHADER_GLYPH_BASE) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let local_id = shader_glyph_local_id(glyph);
    let fg = read_cell_fg(cell_index);
    let bg = read_cell_bg(cell_index);

    let local_uv = vec2<f32>(
        (pixel_pos.x - cell_col * cell_size.x) / cell_size.x,
        (pixel_pos.y - cell_row * cell_size.y) / cell_size.y
    );

    let t = uniforms.shader_glyph_time;
    let color = render_shader_glyph(local_id, local_uv, t, fg, bg, pixel_pos);
    return vec4<f32>(color, 1.0);
}
