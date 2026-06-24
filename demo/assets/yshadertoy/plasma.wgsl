// Plasma: a sum of sine waves over position and time. The interference of a
// few cheap sinusoids gives the wobbling marbled look the demoscene loves.
fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {
    let uv = fragCoord / iResolution.xy * 8.0;
    var v = 0.0;
    v = v + sin(uv.x + iTime);
    v = v + sin(0.5 * (uv.y + iTime));
    v = v + sin(0.3 * (uv.x + uv.y + iTime));
    let r = length(uv - 4.0);
    v = v + sin(r - iTime * 2.0);
    let col = 0.5 + 0.5 * cos(vec3<f32>(v, v + 2.0, v + 4.0) * 1.5);
    return vec4<f32>(col, 1.0);
}
