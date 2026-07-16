// Raster (FreeType R8 atlas) font shader — ydraw-layer glue.
//
// Instance-namespaced exactly like msdf-font.wgsl: the binder substitutes
// __NS__ with this instance's unique namespace at shader-merge time, so
// multiple raster faces (complex-script shaping) coexist as separate children
// of one layer alongside the MSDF fonts. Differs from MSDF only in the atlas
// format (R8 coverage — no median / pixel-range distance field): the glyph
// metadata layout, cell centring and region-UV math are identical.
//
// Per-instance values supplied by the binder:
//   uniforms.__NS___base_size       — pixel size the atlas was rasterized at
//   uniforms.__NS___cell_size       — atlas cell side in pixels
//   uniforms.__NS___atlas_cols      — cells per atlas row
//   uniforms.__NS___buffer_offset   — start of glyph metadata in storage
//   uniforms.__NS___texture_region  — vec4(u_min, v_min, u_max, v_max) of this
//                                     face's slice in the packed R8 atlas
//
// Glyph metadata layout (6 u32 / glyph) matches struct glyph_meta_gpu in
// raster-font.c:
//   [0] size_x   [1] size_y   [2] bearing_x   [3] bearing_y
//   [4] advance  [5] cell_idx (-1 for empty glyphs like space)

fn __NS___base_size() -> f32 {
    return uniforms.__NS___base_size;
}

fn __NS___glyph_size(glyph_index: u32) -> vec2<f32> {
    let base = uniforms.__NS___buffer_offset + glyph_index * 6u;
    return vec2<f32>(
        bitcast<f32>(storage_buffer[base + 0u]),
        bitcast<f32>(storage_buffer[base + 1u])
    );
}

// Alpha coverage for a glyph at normalised glyph-local coords (0..1).
// pixel_scale is accepted for signature-parity with MSDF; raster ignores it
// because FreeType already anti-aliases the coverage baked into the atlas.
fn __NS___glyph_sample(glyph_index: u32,
                       glyph_uv: vec2<f32>,
                       pixel_scale: f32) -> f32 {
    let meta_base = uniforms.__NS___buffer_offset + glyph_index * 6u;
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
    let cell_size_px = f32(uniforms.__NS___cell_size);
    let atlas_cols = uniforms.__NS___atlas_cols;
    let col = cell_idx % atlas_cols;
    let row = cell_idx / atlas_cols;

    // Sub-region of the packed R8 atlas this face occupies (normalised UV).
    let region = uniforms.__NS___texture_region;
    let region_origin_uv = region.xy;
    let atlas_size = vec2<f32>(textureDimensions(atlas_r8_texture, 0));

    // Cell origin within the face's own atlas, then mapped into the packed one.
    let cell_origin_px = vec2<f32>(f32(col), f32(row)) * cell_size_px;
    let padding = (vec2<f32>(cell_size_px) - glyph_size) * 0.5;
    let inner_min_px = cell_origin_px + padding;
    let inner_max_px = cell_origin_px + padding + glyph_size;

    let uv_min = region_origin_uv + inner_min_px / atlas_size;
    let uv_max = region_origin_uv + inner_max_px / atlas_size;
    let uv = clamp(glyph_uv, vec2<f32>(0.0), vec2<f32>(1.0));
    let sample_uv = mix(uv_min, uv_max, uv);

    return textureSampleLevel(atlas_r8_texture, atlas_r8_sampler, sample_uv, 0.0).r;
}
