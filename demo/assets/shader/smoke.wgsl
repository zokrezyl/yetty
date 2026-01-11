// Simple fluid/smoke simulation
// BufferA: velocity/density field with advection
// Demonstrates feedback-based fluid dynamics

//--- BufferA
// Fluid simulation with mouse interaction

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let uv = fragCoord / iResolution();
    let res = iResolution();
    let px = 1.0 / res;
    
    // Sample previous frame
    var prev = texture(iChannel0, uv);
    
    // Decay
    prev = prev * 0.99;
    
    // Sample neighbors for diffusion
    let l = texture(iChannel0, uv - vec2<f32>(px.x, 0.0)).rgb;
    let r = texture(iChannel0, uv + vec2<f32>(px.x, 0.0)).rgb;
    let u = texture(iChannel0, uv + vec2<f32>(0.0, px.y)).rgb;
    let d = texture(iChannel0, uv - vec2<f32>(0.0, px.y)).rgb;
    
    // Diffuse
    var col = prev.rgb * 0.6 + (l + r + u + d) * 0.1;
    
    // Add buoyancy (smoke rises)
    let below = texture(iChannel0, uv - vec2<f32>(0.0, px.y * 2.0)).rgb;
    col = col + below * 0.02;
    
    // Add turbulence
    let noise = fract(sin(dot(uv + iTime() * 0.1, vec2<f32>(12.9898, 78.233))) * 43758.5453);
    let turbulence = (noise - 0.5) * 0.01;
    let turb_uv = uv + vec2<f32>(turbulence, turbulence * 0.5);
    col = mix(col, texture(iChannel0, turb_uv).rgb, 0.1);
    
    // Mouse adds smoke
    let mouse = iMouse();
    let mousePos = vec2<f32>(mouse.x, 1.0 - mouse.y);
    let dist = distance(uv, mousePos);
    
    if (dist < 0.08) {
        let intensity = smoothstep(0.08, 0.0, dist);
        // Color based on time
        let t = iTime() * 2.0;
        let smokeColor = vec3<f32>(
            sin(t) * 0.3 + 0.7,
            sin(t + 2.0) * 0.3 + 0.5,
            sin(t + 4.0) * 0.3 + 0.3
        );
        col = col + smokeColor * intensity * 0.3;
    }
    
    // Clamp
    col = clamp(col, vec3<f32>(0.0), vec3<f32>(1.0));
    
    return vec4<f32>(col, 1.0);
}

//--- Image
// Render the smoke with nice coloring

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let uv = fragCoord / iResolution();
    
    let smoke = texture(iChannel0, uv).rgb;
    
    // Dark background
    let bg = vec3<f32>(0.02, 0.02, 0.05);
    
    // Enhance contrast
    let density = length(smoke);
    var col = bg + smoke * 1.5;
    
    // Add subtle glow
    var glow = vec3<f32>(0.0);
    for (var i = 1; i <= 3; i++) {
        let offset = f32(i) * 0.01;
        glow += texture(iChannel0, uv + vec2<f32>(offset, 0.0)).rgb;
        glow += texture(iChannel0, uv - vec2<f32>(offset, 0.0)).rgb;
        glow += texture(iChannel0, uv + vec2<f32>(0.0, offset)).rgb;
        glow += texture(iChannel0, uv - vec2<f32>(0.0, offset)).rgb;
    }
    glow = glow / 12.0;
    col = col + glow * 0.2;
    
    return vec4<f32>(col, 1.0);
}
