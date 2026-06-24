// Animated cosine-palette gradient. The classic "rainbow that breathes":
// each channel is a phase-shifted cosine of position + time.
fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {
    let uv = fragCoord / iResolution.xy;
    let col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3<f32>(0.0, 2.0, 4.0));
    return vec4<f32>(col, 1.0);
}
