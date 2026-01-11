// Clouds - based on Shadertoy "Clouds" by iq (Inigo Quilez)
// https://www.shadertoy.com/view/4l2XWK
// Converted to WGSL for yetty

fn hash(n: f32) -> f32 {
    return fract(sin(n) * 43758.5453123);
}

fn noise(x: vec3<f32>) -> f32 {
    let p = floor(x);
    let f = fract(x);
    let u = f * f * (3.0 - 2.0 * f);
    
    let n = p.x + p.y * 57.0 + 113.0 * p.z;
    
    return mix(
        mix(mix(hash(n + 0.0), hash(n + 1.0), u.x),
            mix(hash(n + 57.0), hash(n + 58.0), u.x), u.y),
        mix(mix(hash(n + 113.0), hash(n + 114.0), u.x),
            mix(hash(n + 170.0), hash(n + 171.0), u.x), u.y), u.z);
}

fn fbm(p: vec3<f32>) -> f32 {
    var f = 0.0;
    var q = p;
    f += 0.5000 * noise(q); q = q * 2.02;
    f += 0.2500 * noise(q); q = q * 2.03;
    f += 0.1250 * noise(q); q = q * 2.01;
    f += 0.0625 * noise(q);
    return f;
}

fn map(p: vec3<f32>, t: f32) -> f32 {
    let wind = vec3<f32>(t * 0.2, 0.0, t * 0.1);
    return fbm(p + wind) - 0.5;
}

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let res = iResolution();
    let uv = (fragCoord - 0.5 * res) / res.y;
    let t = iTime();
    
    // Camera
    let ro = vec3<f32>(0.0, 1.5, -3.0);
    let rd = normalize(vec3<f32>(uv.x, uv.y + 0.2, 1.5));
    
    // Sky gradient
    var col = vec3<f32>(0.4, 0.6, 0.9) - rd.y * 0.4;
    
    // Sun
    let sun = vec3<f32>(0.5, 0.3, 0.8);
    let sunDir = normalize(sun);
    let sunDot = max(dot(rd, sunDir), 0.0);
    col += vec3<f32>(1.0, 0.8, 0.5) * pow(sunDot, 32.0);
    col += vec3<f32>(1.0, 0.6, 0.3) * pow(sunDot, 8.0) * 0.3;
    
    // Raymarch clouds
    var cloudCol = vec3<f32>(0.0);
    var cloudAlpha = 0.0;
    
    let tmin = 0.0;
    let tmax = 20.0;
    var tCur = tmin;
    
    for (var i = 0; i < 64; i++) {
        if (cloudAlpha > 0.95 || tCur > tmax) { break; }
        
        let pos = ro + rd * tCur;
        let density = map(pos * 0.3, t);
        
        if (density > 0.0) {
            let dens = clamp(density * 0.5, 0.0, 1.0);
            
            // Lighting
            let lightPos = pos + sunDir * 0.5;
            let lightDens = map(lightPos * 0.3, t);
            let shadow = exp(-lightDens * 2.0);
            
            let cloudLight = vec3<f32>(1.0, 0.95, 0.9) * shadow + vec3<f32>(0.3, 0.4, 0.6) * (1.0 - shadow);
            
            cloudCol += cloudLight * dens * (1.0 - cloudAlpha);
            cloudAlpha += dens * (1.0 - cloudAlpha);
        }
        
        tCur += max(0.1, tCur * 0.02);
    }
    
    // Blend clouds with sky
    col = mix(col, cloudCol, cloudAlpha);
    
    // Tone mapping
    col = col / (col + vec3<f32>(1.0));
    col = pow(col, vec3<f32>(0.4545));
    
    return vec4<f32>(col, 1.0);
}
