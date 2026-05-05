// =============================================================================
// Terminal Screen Shader - Text Grid
// =============================================================================
// Generated constants (prepended by binder):
//   uniforms.text_grid_*          — grid uniforms
//   raster_font_buffer_offset     — u32 offset of glyph UVs in storage_buffer
//   text_grid_buffer_offset       — u32 offset of cells in storage_buffer
//   raster_font_texture_region    — vec4 UV region in atlas
//   atlas_r8_texture / atlas_r8_sampler

// RENDER_LAYER_BINDINGS_PLACEHOLDER

struct VertexInput {
    @location(0) position: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    // Grid-local pixel position. @builtin(position) in fragment is the
    // framebuffer pixel — once the pane viewport sits at offset (vp.x, vp.y)
    // != (0,0), framebuffer coords no longer line up with the grid origin
    // and every fragment falls into the "outside grid" branch below. The
    // vertex shader maps the NDC quad ([-1,1]) onto the grid's pixel area
    // explicitly so cell lookup is independent of where the pane sits in
    // the big surface. Linear interpolation is what we want — no
    // perspective in this 2D pass.
    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.position, 0.0, 1.0);

    let grid_size = uniforms.text_grid_grid_size;
    let cell_size = uniforms.text_grid_cell_size;
    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;

    // NDC.x: -1 → 0, 1 → grid_pixel_w
    // NDC.y:  1 → 0, -1 → grid_pixel_h (framebuffer y is top-down)
    output.grid_pixel = vec2<f32>(
        (input.position.x * 0.5 + 0.5) * grid_pixel_w,
        (0.5 - input.position.y * 0.5) * grid_pixel_h
    );
    return output;
}

// VTermScreenCell layout (12 bytes):
//   u32[0]: glyph_index
//   u32[1]: fg.r | fg.g<<8 | fg.b<<16 | bg.r<<24
//   u32[2]: bg.g | bg.b<<8 | attrs<<16
fn read_cell_glyph(cell_index: u32) -> u32 {
    return storage_buffer[text_grid_buffer_offset + cell_index * 3u];
}

fn read_cell_fg(cell_index: u32) -> vec3<f32> {
    let packed = storage_buffer[text_grid_buffer_offset + cell_index * 3u + 1u];
    return vec3<f32>(
        f32(packed & 0xFFu) / 255.0,
        f32((packed >> 8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0
    );
}

fn read_cell_bg(cell_index: u32) -> vec3<f32> {
    let packed1 = storage_buffer[text_grid_buffer_offset + cell_index * 3u + 1u];
    let packed2 = storage_buffer[text_grid_buffer_offset + cell_index * 3u + 2u];
    return vec3<f32>(
        f32((packed1 >> 24u) & 0xFFu) / 255.0,
        f32(packed2 & 0xFFu) / 255.0,
        f32((packed2 >> 8u) & 0xFFu) / 255.0
    );
}

// font_sample is provided by the child font shader (raster or msdf)

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let grid_size = uniforms.text_grid_grid_size;
    let cell_size = uniforms.text_grid_cell_size;

    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;

    // Visual zoom — transform the grid-local pixel into the *source* pixel the
    // cell/glyph math should evaluate at. Because MSDF/SDF are re-evaluated
    // per fragment, the edges stay sharp at any scale. When scale==1 this is
    // the identity transform (no runtime cost beyond a multiply + add).
    let vz_scale = uniforms.text_grid_visual_zoom_scale;
    let vz_off   = uniforms.text_grid_visual_zoom_off;
    let vz_center = vec2<f32>(grid_pixel_w * 0.5, grid_pixel_h * 0.5);
    let pixel_pos = (input.grid_pixel - vz_center) / max(vz_scale, 0.0001)
                  + vz_center + vz_off;

    if (pixel_pos.x < 0.0 || pixel_pos.y < 0.0 ||
        pixel_pos.x >= grid_pixel_w || pixel_pos.y >= grid_pixel_h) {
        // Visual zoom (or future panning) dragged us outside the grid —
        // emit alpha=0 so the pane background composes underneath instead
        // of being overwritten with an opaque fallback colour.
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let cell_col = floor(pixel_pos.x / cell_size.x);
    let cell_row = floor(pixel_pos.y / cell_size.y);
    // libvterm allocates the cell buffer 2*rows tall (double-allocation
    // circular buffer). text_grid_root_row tells us where row 0 of the
    // visible screen sits within that buffer; libvterm bumps it on full-
    // screen scroll-up instead of memmoving rows-1 rows.
    let cell_index = (u32(cell_row) + uniforms.text_grid_root_row) * u32(grid_size.x) + u32(cell_col);

    let local_px = vec2<f32>(
        pixel_pos.x - cell_col * cell_size.x,
        pixel_pos.y - cell_row * cell_size.y
    );

    let glyph = read_cell_glyph(cell_index);
    let fg_color = read_cell_fg(cell_index);

    // Glyph alpha — 0 if no glyph (empty cell or shader-glyph cell), else
    // the font's coverage for this fragment.
    var glyph_alpha = 0.0;
    if (glyph != 0u && glyph < 0x80000000u) {
        glyph_alpha = font_sample(glyph, local_px, cell_size);
    }

    var out_color = fg_color;
    var out_alpha = glyph_alpha;

    // Cursor — opaque inverted block / underline / bar at the cell.
    let cursor_pos = uniforms.text_grid_cursor_pos;
    if (uniforms.text_grid_cursor_visible > 0.5 &&
        u32(cell_col) == u32(cursor_pos.x) &&
        u32(cell_row) == u32(cursor_pos.y)) {
        let local_uv = local_px / cell_size;
        let shape = i32(uniforms.text_grid_cursor_shape);
        var draw_cursor = false;
        if (shape == 2) {
            draw_cursor = local_uv.y > 0.85;
        } else if (shape == 3) {
            draw_cursor = local_uv.x < 0.1;
        } else {
            draw_cursor = true;
        }
        if (draw_cursor) {
            out_color = vec3<f32>(1.0, 1.0, 1.0) - fg_color;
            out_alpha = 1.0;
        }
    }

    // Empty / non-glyph fragments output alpha=0 so the pane background
    // layer composites underneath. With alpha=1 we'd overwrite everything
    // below and the pane bg would never be visible.
    return vec4<f32>(out_color, out_alpha);
}
