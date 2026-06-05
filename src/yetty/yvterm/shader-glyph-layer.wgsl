// =============================================================================
// Shader-Glyph Layer — animated procedural glyphs, instanced per cell.
// =============================================================================
//
// Reads two storage buffers populated by shader-glyph-layer.c:
//   cells     : the full vterm cell array (16 bytes/cell), same layout as
//                text-layer.wgsl. Used to fetch fg/bg for the rendered cell.
//   instances : packed (cell_index, local_id) pairs, one per shader-glyph
//                cell visible this frame.
//
// The vertex shader fetches one entry per draw instance and emits a quad
// over exactly that cell's pixel rect — fragment shader fires only on
// shader-glyph pixels (cell_w × cell_h fragments per cell) instead of
// every pixel of the pane. Each fragment returns opaque RGBA so the
// in-place repaint over the previous frame's content is clean.
//
// Per-glyph shader bodies and the `render_shader_glyph(...)` switch
// dispatcher are spliced in at the SHADER_GLYPHS_PLACEHOLDER marker by
// shader-glyph-layer.c at load time.
//
// Generated constants (prepended by binder):
//   uniforms.shader_glyph_grid_size
//   uniforms.shader_glyph_cell_size
//   uniforms.shader_glyph_time
//   uniforms.shader_glyph_visual_zoom_scale
//   uniforms.shader_glyph_visual_zoom_off
//   uniforms.shader_glyph_root_row
//   shader_glyph_cells_offset
//   shader_glyph_instances_offset

// RENDER_LAYER_BINDINGS_PLACEHOLDER

struct VertexInput  { @location(0) position: vec2<f32>, };
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    // 0..1 inside the cell. Linear interpolated → per-pixel uv for the
    // glyph shader.
    @location(0) @interpolate(linear) local_uv: vec2<f32>,
    // Flat across the quad — every fragment in the cell sees the same id.
    @location(1) @interpolate(flat) local_id: u32,
    // Cell index for fg/bg fetch in the fragment shader.
    @location(2) @interpolate(flat) cell_index: u32,
    // Pixel position passed through for tile-coherent shader effects
    // (plasma, wave, sparkle — see glyph-shaders/README.md).
    @location(3) @interpolate(linear) pixel_pos: vec2<f32>,
};

// 16-byte cell layout (4 u32) — same as text-layer.wgsl. Stride must equal
// sizeof(VTermScreenCell)/4; only u32[1]/u32[2] hold fg/bg.
fn read_cell_fg(cell_index: u32) -> vec3<f32> {
    let packed = storage_buffer[shader_glyph_cells_offset + cell_index * 4u + 1u];
    return vec3<f32>(
        f32( packed        & 0xFFu) / 255.0,
        f32((packed >>  8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0
    );
}

fn read_cell_bg(cell_index: u32) -> vec3<f32> {
    let packed1 = storage_buffer[shader_glyph_cells_offset + cell_index * 4u + 1u];
    let packed2 = storage_buffer[shader_glyph_cells_offset + cell_index * 4u + 2u];
    return vec3<f32>(
        f32((packed1 >> 24u) & 0xFFu) / 255.0,
        f32( packed2         & 0xFFu) / 255.0,
        f32((packed2 >>  8u) & 0xFFu) / 255.0
    );
}

// 8-byte instance layout (must match shader_glyph_instance in
// shader-glyph-layer.c):
//   u32[0] = cell_index    (ABSOLUTE buffer index = (root_row+row)*cols + col)
//   u32[1] = local_id      (0..PUA window size; dispatcher key)
fn read_instance_cell_index(inst: u32) -> u32 {
    return storage_buffer[shader_glyph_instances_offset + inst * 2u];
}

fn read_instance_local_id(inst: u32) -> u32 {
    return storage_buffer[shader_glyph_instances_offset + inst * 2u + 1u];
}

@vertex
fn vs_main(input: VertexInput, @builtin(instance_index) inst: u32) -> VertexOutput {
    var output: VertexOutput;

    let grid_size = uniforms.shader_glyph_grid_size;
    let cell_size = uniforms.shader_glyph_cell_size;
    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;

    // cell_index is the ABSOLUTE index into libvterm's 2*rows-tall buffer
    // (fg/bg are fetched there). Subtract root_row*cols to map it onto the
    // visible screen, the inverse of text-layer.wgsl adding root_row. The C
    // scan only emits cells with abs_row >= root_row, so this never underflows.
    let cell_index = read_instance_cell_index(inst);
    let cols = u32(grid_size.x);
    let cell_col_i = cell_index % cols;
    let cell_row_i = (cell_index / cols) - uniforms.shader_glyph_root_row;
    let cell_col = f32(cell_col_i);
    let cell_row = f32(cell_row_i);

    // Quad VB is (-1,-1)..(1,1); map to [0,1] inside this cell.
    let corner_uv = input.position * 0.5 + 0.5;

    // Pixel position of this vertex within the grid.
    let px = (cell_col + corner_uv.x) * cell_size.x;
    let py = (cell_row + corner_uv.y) * cell_size.y;

    // Visual zoom: forward transform — same math as text-layer.wgsl's
    // pixel_pos calculation, just applied to vertex positions instead of
    // per-fragment. Inverse-of-inverse so the cell rendered at the same
    // place text-layer paints it.
    //
    // text-layer: source_px = (screen_px - vz_center) / vz_scale + vz_center + vz_off
    // we want   : screen_px = (source_px - vz_center - vz_off) * vz_scale + vz_center
    let vz_scale = uniforms.shader_glyph_visual_zoom_scale;
    let vz_off   = uniforms.shader_glyph_visual_zoom_off;
    let vz_center = vec2<f32>(grid_pixel_w * 0.5, grid_pixel_h * 0.5);
    let source_px = vec2<f32>(px, py);
    let screen_px = (source_px - vz_center - vz_off) * vz_scale + vz_center;

    // Convert to NDC. y flipped: pixel y grows down, NDC y grows up.
    let ndc_x = screen_px.x / grid_pixel_w * 2.0 - 1.0;
    let ndc_y = 1.0 - screen_px.y / grid_pixel_h * 2.0;
    output.position = vec4<f32>(ndc_x, ndc_y, 0.0, 1.0);

    output.local_uv = corner_uv;
    output.local_id = read_instance_local_id(inst);
    output.cell_index = cell_index;
    output.pixel_pos = source_px;  // source-space, matches old shader's input
    return output;
}

// SHADER_GLYPHS_PLACEHOLDER

// =============================================================================
// Fragment Shader
// =============================================================================
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let fg = read_cell_fg(input.cell_index);
    let bg = read_cell_bg(input.cell_index);
    let t  = uniforms.shader_glyph_time;
    let color = render_shader_glyph(input.local_id, input.local_uv, t, fg, bg, input.pixel_pos);
    // Opaque RGBA: the layer is meant to overwrite the cell, not blend
    // with whatever the text-layer left there.
    return vec4<f32>(color, 1.0);
}
