// Raster (FreeType R8 atlas) font shader — ydraw-layer glue.
//
// Mirrors msdf-font.wgsl's interface so ydraw-layer can call the same three
// helper names regardless of which backend is active. Glyph metadata layout
// matches struct glyph_meta_gpu in raster-font.c:
//   [0] size_x       bitmap width  in pixels at base_size
//   [1] size_y       bitmap height in pixels at base_size
//   [2] bearing_x    (unused — bearing is pre-applied on the CPU)
//   [3] bearing_y    (unused — bearing is pre-applied on the CPU)
//   [4] advance
//   [5] cell_idx     atlas cell index (-1 for empty glyphs)
//
// Bitmaps are centered within cell_size×cell_size cells just like MSDF, so
// the UV logic is identical; only the atlas format (R8 vs RGBA8) and the
// final distance-field math differ.

fn font_base_size() -> f32 {
    return uniforms.raster_font_base_size;
}

fn font_glyph_size(glyph_index: u32) -> vec2<f32> {
    let base = raster_font_buffer_offset + glyph_index * 6u;
    return vec2<f32>(
        bitcast<f32>(storage_buffer[base + 0u]),
        bitcast<f32>(storage_buffer[base + 1u])
    );
}

// Alpha coverage for a glyph at normalized glyph-local coords (0..1).
// pixel_scale is accepted for signature-parity with MSDF; raster ignores it.
fn font_glyph_sample(glyph_index: u32,
                     glyph_uv: vec2<f32>,
                     pixel_scale: f32) -> f32 {
    let meta_base = raster_font_buffer_offset + glyph_index * 6u;
    let glyph_size = vec2<f32>(
        bitcast<f32>(storage_buffer[meta_base + 0u]),
        bitcast<f32>(storage_buffer[meta_base + 1u])
    );
    if (glyph_size.x <= 0.0 || glyph_size.y <= 0.0) {
        return 0.0;
    }
    let cell_idx_f = bitcast<f32>(storage_buffer[meta_base + 5u]);
    if (cell_idx_f < 0.0) {
        return 0.0;
    }

    let cell_idx = u32(cell_idx_f);
    let cell_size_px = f32(uniforms.raster_font_cell_size);
    let atlas_cols = uniforms.raster_font_atlas_cols;
    let col = cell_idx % atlas_cols;
    let row = cell_idx / atlas_cols;

    let atlas_size = vec2<f32>(textureDimensions(atlas_r8_texture, 0));
    let cell_origin_px = vec2<f32>(f32(col), f32(row)) * cell_size_px;
    let padding = (vec2<f32>(cell_size_px) - glyph_size) * 0.5;
    let uv_min = (cell_origin_px + padding) / atlas_size;
    let uv_max = (cell_origin_px + padding + glyph_size) / atlas_size;
    let uv = clamp(glyph_uv, vec2<f32>(0.0), vec2<f32>(1.0));
    let sample_uv = mix(uv_min, uv_max, uv);

    return textureSampleLevel(atlas_r8_texture, atlas_r8_sampler,
                              sample_uv, 0.0).r;
}
