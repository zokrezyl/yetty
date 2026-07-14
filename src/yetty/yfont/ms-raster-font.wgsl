// Raster font shader — provides font_sample()
// Uses R8 atlas texture + per-glyph record buffer (4 floats per glyph:
// uv origin x/y, slot width in cells, pad — matches
// struct yetty_yfont_raster_glyph_uv in ms-raster-font.c)

fn font_sample(glyph_index: u32, local_px: vec2<f32>, cell_size: vec2<f32>) -> f32 {
    let base = raster_font_buffer_offset + glyph_index * 4u;
    let glyph_uv = vec2<f32>(
        bitcast<f32>(storage_buffer[base]),
        bitcast<f32>(storage_buffer[base + 1u])
    );

    if (glyph_uv.x < 0.0) {
        return 0.0;
    }

    let width_cells = max(bitcast<f32>(storage_buffer[base + 2u]), 1.0);
    let slot_size = vec2<f32>(cell_size.x * width_cells, cell_size.y);
    if (local_px.x < 0.0 || local_px.y < 0.0 ||
        local_px.x >= slot_size.x || local_px.y >= slot_size.y) {
        return 0.0;
    }

    let local_uv = local_px / slot_size;
    let region = raster_font_texture_region;
    let region_size = region.zw - region.xy;
    let atlas_size = vec2<f32>(f32(textureDimensions(atlas_r8_texture).x),
                               f32(textureDimensions(atlas_r8_texture).y));
    let glyph_size_uv = slot_size / atlas_size;
    let sample_uv = region.xy + glyph_uv * region_size + local_uv * glyph_size_uv;
    return textureSampleLevel(atlas_r8_texture, atlas_r8_sampler, sample_uv, 0.0).r;
}
