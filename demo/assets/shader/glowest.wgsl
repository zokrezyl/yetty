// Glowest - Glowing forest with snow
// Converted from Shadertoy GLSL to WGSL

const PI: f32 = 3.14159265359;

fn hash33(p3_in: vec3<f32>) -> vec3<f32> {
    var p3 = fract(p3_in * vec3<f32>(0.1031, 0.1030, 0.0973));
    p3 = p3 + dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

fn hash12(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn rotate2d(r: f32) -> mat2x2<f32> {
    let c = cos(r);
    let s = sin(r);
    return mat2x2<f32>(c, -s, s, c);
}

fn smin(a: f32, b: f32, k_in: f32) -> f32 {
    let k = k_in * 16.0 / 3.0;
    let h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * h * (4.0 - h) * k * (1.0 / 16.0);
}

fn sdCappedCylinder(p: vec3<f32>, r: f32, h: f32) -> f32 {
    let d = abs(vec2<f32>(length(p.xz), p.y)) - vec2<f32>(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2<f32>(0.0)));
}

fn sdRoundCone(p: vec3<f32>, r1: f32, r2: f32, h: f32) -> f32 {
    let b = (r1 - r2) / h;
    let a = sqrt(1.0 - b * b);
    let q = vec2<f32>(length(p.xz), p.y);
    let k = dot(q, vec2<f32>(-b, a));
    if (k < 0.0) { return length(q) - r1; }
    if (k > a * h) { return length(q - vec2<f32>(0.0, h)) - r2; }
    return dot(q, vec2<f32>(a, b)) - r1;
}

fn sdHexPrism(p_in: vec3<f32>, h: vec2<f32>) -> f32 {
    let k = vec3<f32>(-0.8660254, 0.5, 0.57735);
    var p = abs(p_in);
    p = vec3<f32>(p.xy - 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy, p.z);
    let d = vec2<f32>(
        length(p.xy - vec2<f32>(clamp(p.x, -k.z * h.x, k.z * h.x), h.x)) * sign(p.y - h.x),
        p.z - h.y
    );
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2<f32>(0.0)));
}

fn sdVerticalCapsule(p: vec3<f32>, h: f32, r: f32) -> f32 {
    var q = p;
    q.y = q.y - clamp(q.y, 0.0, h);
    return length(q) - r;
}

fn sdTreeBranches(p_in: vec3<f32>, t: f32) -> f32 {
    let s = cos(PI * 0.25);
    var d = 100.0;
    var p = p_in;

    for (var i = 0; i < 2; i = i + 1) {
        for (var id = -1; id < 2; id = id + 1) {
            let y = p.y - s * f32(id);
            var r = vec3<f32>(p.x, y, p.z);

            let dir = select(-1.0, 1.0, id == 0);
            let rot = rotate2d(dir * PI * 0.25);
            r = vec3<f32>(rot * r.xy, r.z);

            r.x = r.x + sin(r.y * 3.0 + t) * 0.05;

            d = min(d, sdVerticalCapsule(r, 1.5, 0.02));
        }

        let rot2 = rotate2d(PI * 0.6);
        p = vec3<f32>(rot2 * p.xz, p.y).xzy;
        p.y = p.y + 0.2;
    }

    return d;
}

fn sdTreeLeaves(p_in: vec3<f32>, fid: vec2<f32>, t: f32) -> f32 {
    var p = p_in;
    let h1 = hash12(fid);
    var body: f32;
    
    if (h1 < 0.25) {
        body = length(p) - 1.5;
    } else if (h1 < 0.5) {
        body = sdRoundCone(p, 1.0, 0.2, 3.0);
    } else if (h1 < 0.75) {
        body = sdHexPrism(p, vec2<f32>(1.0, 1.0));
    } else {
        body = sdCappedCylinder(p, 1.0, 2.0);
    }

    let rot = rotate2d(sin(p.y * 1.0 + t * 0.1));
    p = vec3<f32>(rot * p.xz, p.y).xzy;
    
    let s = vec3<f32>(0.2);
    let id = round(p / s);
    var pt = p - s * id;
    let h = hash33(id);
    pt = pt + sin(h * 100.0 + t) * 0.05;

    return max(length(pt) - 0.001, body);
}

fn sdSnow(p_in: vec3<f32>, t: f32) -> f32 {
    var p = p_in;
    let fade_out = smoothstep(0.0, -30.0, p.y);
    p.y = p.y + t;

    let s = vec3<f32>(2.0);
    let id = round(p / s);
    let h = hash33(id);
    var pt = p - s * id;
    pt = pt + sin(h * 100.0 + t) * 0.2;

    return length(pt) + fade_out;
}

fn sdTree(p_in: vec3<f32>, id: vec2<f32>, t: f32) -> f32 {
    var p = p_in;
    p = vec3<f32>(p.x + sin(p.y + t) * 0.05, p.y, p.z + sin(p.y + t) * 0.05);

    let stem = sdVerticalCapsule(p, 3.0, 0.03);
    let branches = sdTreeBranches(p - vec3<f32>(0.0, 2.25, 0.0), t);
    let leaves = sdTreeLeaves(p - vec3<f32>(0.0, 2.75, 0.0), id, t);
    let snow = sdSnow(p - vec3<f32>(0.0, 3.0, 0.0), t);

    return smin(stem, smin(branches, min(snow, leaves), 0.02), 0.02);
}

fn map(p_in: vec3<f32>, t: f32) -> f32 {
    var d = 100.0;
    var p = p_in;

    p.y = p.y + sin(p.x + t * 0.5) * 0.2;
    p.y = p.y + sin(p.z + t * 0.5) * 0.2;

    // Trees
    {
        var pt = p;
        let s = vec2<f32>(5.0);
        let id = round(pt.xz / s);
        pt = vec3<f32>(pt.x - s.x * id.x, pt.y, pt.z - s.y * id.y);
        let rot = rotate2d(id.x * id.y);
        pt = vec3<f32>(rot * pt.xz, pt.y).xzy;
        d = min(d, sdTree(pt, id, t));
    }

    // Floating hexagons
    {
        var pt = p;
        let s = vec2<f32>(0.2);
        let id = vec2<i32>(round(pt.xz / s));
        pt = vec3<f32>(pt.x - s.x * f32(id.x), pt.y, pt.z - s.y * f32(id.y));

        let dir = select(1.0, -1.0, id.x % 2 == 0 && id.y % 2 == 0);
        let rot_xz = rotate2d(f32(id.x) + dir * t);
        let rot_xy = rotate2d(f32(id.y) + dir * t);
        let rot_yz = rotate2d(f32(id.x) + f32(id.y) + dir * t);
        
        pt = vec3<f32>(rot_xz * pt.xz, pt.y).xzy;
        pt = vec3<f32>(rot_xy * pt.xy, pt.z);
        pt = vec3<f32>(pt.x, rot_yz * pt.yz);

        let h = hash33(vec3<f32>(f32(id.x), f32(id.y), f32(id.x)));
        d = min(d, sdHexPrism(pt, vec2<f32>((sin(h.x * 100.0 + t) * 0.5 + 0.5) * 0.02)));
    }

    return d;
}

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let t = iTime();
    let res = iResolution();
    var col = vec3<f32>(0.0);
    let uv = (2.0 * fragCoord - res) / res.y;

    var ro = vec3<f32>(sin(t) * 0.3 + 2.0, 0.75, -t * 0.8);
    var rd = normalize(vec3<f32>(uv, -1.0));

    ro.y = ro.y - sin(ro.x + t * 0.5) * 0.2;
    ro.y = ro.y - sin(ro.z + t * 0.5) * 0.2;

    let rot = rotate2d(PI * 0.1 + sin(t * 0.5) * PI * 0.05);
    rd = vec3<f32>(rd.x, rot * rd.yz);

    var d = 0.0;
    for (var i = 0; i < 30; i = i + 1) {
        let p = ro + d * rd;
        var dt = map(p, t);
        dt = abs(dt);
        col = col + (sin(vec3<f32>(1.0, 2.0, 3.0) + p.x * 0.1 + p.z * 0.01 + t * 0.1) * 0.2 + 0.2) / (dt + 0.01);
        d = d + dt * 0.8;
    }

    col = tanh(col * 0.01);

    return vec4<f32>(col, 1.0);
}
