// A rotating swirl: polar coordinates with an angle that grows with radius
// and time, banded into rings. Pointer position (iMouse) shifts the centre.
fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {
    let center = select(iResolution.xy * 0.5, iMouse.xy, iMouse.x > 0.0);
    let p = (fragCoord - center) / iResolution.y;
    let radius = length(p);
    let angle = atan2(p.y, p.x) + radius * 6.0 - iTime * 1.5;
    let bands = 0.5 + 0.5 * sin(angle * 5.0);
    let glow = smoothstep(0.6, 0.0, radius);
    let col = vec3<f32>(0.42, 0.66, 0.57) * bands + vec3<f32>(0.05, 0.08, 0.10);
    return vec4<f32>(col * (0.4 + glow), 1.0);
}
