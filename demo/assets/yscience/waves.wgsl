// Two-source wave interference, evaluated live per pixel — the classic
// double-slit picture. Two coherent point sources emit circular waves
// u_i = sin(k*r_i - omega*t)/sqrt(r_i); the screen shows the superposed
// field as a diverging colormap (blue trough, warm crest). Hyperbolic
// nodal lines (destructive interference) stay dark. The pointer drags the
// second source, detuning the fringe spacing in real time.
fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {
    let plane_point = (fragCoord - iResolution.xy * 0.5) / iResolution.y;

    let source_one = vec2<f32>(-0.22, 0.0);
    let mouse_point = (iMouse.xy - iResolution.xy * 0.5) / iResolution.y;
    let source_two = select(vec2<f32>(0.22, 0.0), mouse_point, iMouse.x > 0.0);

    let wavenumber = 55.0;
    let angular_frequency = 5.0;

    let radius_one = distance(plane_point, source_one) + 0.015;
    let radius_two = distance(plane_point, source_two) + 0.015;
    let wave_one = sin(wavenumber * radius_one - angular_frequency * iTime)
                   * inverseSqrt(radius_one * 24.0);
    let wave_two = sin(wavenumber * radius_two - angular_frequency * iTime)
                   * inverseSqrt(radius_two * 24.0);
    let field = clamp(wave_one + wave_two, -1.0, 1.0);

    // Diverging map: deep blue < 0 < warm amber, near-black at the nodes.
    let crest = max(field, 0.0);
    let trough = max(-field, 0.0);
    let color = vec3<f32>(0.06, 0.08, 0.10)
              + crest * vec3<f32>(0.95, 0.62, 0.22)
              + trough * vec3<f32>(0.18, 0.45, 0.85);
    return vec4<f32>(color, 1.0);
}
