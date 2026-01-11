// Julia - Distance 1
// The MIT License - Copyright © 2013 Inigo Quilez
// https://www.shadertoy.com/view/Mss3R8
// Distance to a traditional Julia set for f(z)=z²+c
// https://iquilezles.org/articles/distancefractals

const AA: i32 = 2;

fn calc(p: vec2<f32>, time: f32) -> f32 {
    let res = iResolution();
    
    // non p dependent
    let ltime = 0.5 - 0.5 * cos(time * 0.06);
    let zoom = pow(0.9, 50.0 * ltime);
    let cen = vec2<f32>(0.2655, 0.301) + zoom * 0.8 * cos(4.0 + 2.0 * ltime);
    
    let c = vec2<f32>(-0.745, 0.186) - 0.045 * zoom * (1.0 - ltime * 0.5);

    let pp = (2.0 * p - res) / res.y;
    var z = cen + (pp - cen) * zoom;
   
    // only derivative length version
    var ld2 = 1.0;
    var lz2 = dot(z, z);
    for (var i = 0; i < 256; i = i + 1) {
        ld2 = ld2 * 4.0 * lz2;
        z = vec2<f32>(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        lz2 = dot(z, z);
        if (lz2 > 200.0) { break; }
    }
    let d = sqrt(lz2 / ld2) * log(lz2);
    
    return sqrt(clamp((150.0 / zoom) * d, 0.0, 1.0));
}

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let res = iResolution();
    let time = iTime();
    
    var scol = 0.0;
    for (var j = 0; j < AA; j = j + 1) {
        for (var i = 0; i < AA; i = i + 1) {
            let offset = -0.5 + vec2<f32>(f32(i), f32(j)) / f32(AA);
            scol = scol + calc(fragCoord + offset, time);
        }
    }
    scol = scol / f32(AA * AA);
    
    var vcol = pow(vec3<f32>(scol), vec3<f32>(0.9, 1.1, 1.4));
    
    let uv = fragCoord / res;
    vcol = vcol * (0.7 + 0.3 * pow(16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.25));
    
    return vec4<f32>(vcol, 1.0);
}
