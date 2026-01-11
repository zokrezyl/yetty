// Bumped Sinusoidal Warp with Texture
// Based on Shadertoy by Shane
// Original: https://www.shadertoy.com/view/4l2XWK
// Converted to WGSL - uses iChannel0 texture

// Warp function - layered sinusoidal feedback
fn W(p_in: vec2<f32>) -> vec2<f32> {
    var p = (p_in + 3.0) * 4.0;
    let t = iTime() / 2.0;

    for (var i = 0; i < 3; i++) {
        p = p + cos(p.yx * 3.0 + vec2<f32>(t, 1.57)) / 3.0;
        p = p + sin(p.yx + t + vec2<f32>(1.57, 0.0)) / 2.0;
        p = p * 1.3;
    }

    p = p + fract(sin(p + vec2<f32>(13.0, 7.0)) * 5e5) * 0.03 - 0.015;
    return (p % 2.0) - 1.0;
}

fn bumpFunc(p: vec2<f32>) -> f32 {
    return length(W(p)) * 0.7071;
}

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let res = iResolution();
    let uv = (fragCoord - res * 0.5) / res.y;

    let sp = vec3<f32>(uv, 0.0);
    let rd = normalize(vec3<f32>(uv, 1.0));
    let lp = vec3<f32>(cos(iTime()) * 0.5, sin(iTime()) * 0.2, -1.0);
    var sn = vec3<f32>(0.0, 0.0, -1.0);

    // Bump mapping
    let eps = vec2<f32>(4.0 / res.y, 0.0);
    let f = bumpFunc(sp.xy);
    let fx = (bumpFunc(sp.xy - eps.xy) - f) / eps.x;
    let fy = (bumpFunc(sp.xy - eps.yx) - f) / eps.x;
    sn = normalize(sn + vec3<f32>(fx, fy, 0.0) * 0.05);

    // Lighting
    var ld = lp - sp;
    let lDist = max(length(ld), 0.0001);
    ld = ld / lDist;

    var atten = 1.0 / (1.0 + lDist * lDist * 0.15);
    atten = atten * (f * 0.9 + 0.1);

    var diff = max(dot(sn, ld), 0.0);
    diff = pow(diff, 4.0) * 0.66 + pow(diff, 8.0) * 0.34;
    let spec = pow(max(dot(reflect(-ld, sn), -rd), 0.0), 12.0);

    // Sample texture with warped coordinates
    let texUV = sp.xy + W(sp.xy) / 8.0 + 0.5;
    var texCol = texture(iChannel0, texUV).xyz;
    texCol = texCol * texCol; // sRGB to linear
    texCol = smoothstep(vec3<f32>(0.05), vec3<f32>(0.75), pow(texCol, vec3<f32>(0.75, 0.8, 0.85)));

    // Final color
    var col = (texCol * (diff * vec3<f32>(1.0, 0.97, 0.92) * 2.0 + 0.5) + vec3<f32>(1.0, 0.6, 0.2) * spec * 2.0) * atten;

    // Environment mapping
    let refl = max(dot(reflect(rd, sn), vec3<f32>(1.0, 0.0, 0.0)), 0.0);
    col = col + col * pow(refl, 4.0) * vec3<f32>(0.25, 0.5, 1.0) * 3.0;

    return vec4<f32>(sqrt(clamp(col, vec3<f32>(0.0), vec3<f32>(1.0))), 1.0);
}
