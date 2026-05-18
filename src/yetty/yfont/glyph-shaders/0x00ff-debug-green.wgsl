// local_id 0x00ff - debug-green
// Diagnostic glyph: outputs solid green regardless of input. If you can SEE
// green at U+1000FF cells, the layer is rendering and compositing correctly.
// If you see something else, the bug is upstream of the per-glyph shader.
fn shader_glyph_255(local_uv: vec2<f32>, time: f32, fg: vec3<f32>, bg: vec3<f32>, pixel_pos: vec2<f32>) -> vec3<f32> {
    return vec3<f32>(0.0, 1.0, 0.0);
}
