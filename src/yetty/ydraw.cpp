#include <yetty/ydraw.h>
#include <yetty/wgpu-compat.h>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <regex>

namespace yetty {

//-----------------------------------------------------------------------------
// WGSL Shader with 2D and 3D SDF primitives (Inigo Quilez - iquilezles.org)
//-----------------------------------------------------------------------------

static const char* YDRAW_SHADER = R"(
struct Uniforms {
    rect: vec4<f32>,
    resolution: vec2<f32>,
    time: f32,
    num_primitives: f32,
}

struct YDrawStyle {
    fill: vec4<f32>,
    stroke: vec4<f32>,
    stroke_width: f32,
    round: f32,
    rotate: f32,
    _pad: f32,
}

struct Primitive {
    ptype: u32,
    params: array<f32, 15>,
    style: YDrawStyle,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var<storage, read> primitives: array<Primitive>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var p = array<vec2<f32>, 6>(
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    let pos = p[vi];
    var o: VertexOutput;
    o.position = vec4(u.rect.x + pos.x * u.rect.z, u.rect.y - pos.y * u.rect.w, 0.0, 1.0);
    o.uv = pos;
    return o;
}

// =============================================================================
// Helper functions
// =============================================================================

fn dot2(v: vec2<f32>) -> f32 { return dot(v, v); }
fn dot3(v: vec3<f32>) -> f32 { return dot(v, v); }

fn rotate2d(p: vec2<f32>, angle: f32) -> vec2<f32> {
    let c = cos(angle);
    let s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

fn ndot(a: vec2<f32>, b: vec2<f32>) -> f32 { return a.x*b.x - a.y*b.y; }

// =============================================================================
// 2D SDF Primitives (iquilezles.org)
// =============================================================================

fn sdCircle(p: vec2<f32>, r: f32) -> f32 {
    return length(p) - r;
}

fn sdBox2D(p: vec2<f32>, b: vec2<f32>) -> f32 {
    let d = abs(p) - b;
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

fn sdRoundedBox2D(p: vec2<f32>, b: vec2<f32>, r: f32) -> f32 {
    let d = abs(p) - b + vec2(r);
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
}

fn sdSegment(p: vec2<f32>, a: vec2<f32>, b: vec2<f32>) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

fn sdTriangle(p: vec2<f32>, p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>) -> f32 {
    let e0 = p1 - p0;
    let e1 = p2 - p1;
    let e2 = p0 - p2;
    let v0 = p - p0;
    let v1 = p - p1;
    let v2 = p - p2;
    let pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    let pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    let pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    let s = sign(e0.x * e2.y - e0.y * e2.x);
    let d = min(min(
        vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
        vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
        vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

fn sdBezier(pos: vec2<f32>, A: vec2<f32>, B: vec2<f32>, C: vec2<f32>) -> f32 {
    let a = B - A;
    let b = A - 2.0 * B + C;
    let c = a * 2.0;
    let d = A - pos;
    let kk = 1.0 / dot(b, b);
    let kx = kk * dot(a, b);
    let ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
    let kz = kk * dot(d, a);
    var res = 0.0;
    let p = ky - kx * kx;
    let p3 = p * p * p;
    let q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
    var h = q * q + 4.0 * p3;
    if (h >= 0.0) {
        h = sqrt(h);
        let x = (vec2(h, -h) - q) / 2.0;
        let uv = sign(x) * pow(abs(x), vec2(1.0 / 3.0));
        let t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
        res = dot2(d + (c + b * t) * t);
    } else {
        let z = sqrt(-p);
        let v = acos(q / (p * z * 2.0)) / 3.0;
        let m = cos(v);
        let n = sin(v) * 1.732050808;
        let t = clamp(vec3(m + m, -n - m, n - m) * z - kx, vec3(0.0), vec3(1.0));
        res = min(dot2(d + (c + b * t.x) * t.x), dot2(d + (c + b * t.y) * t.y));
    }
    return sqrt(res);
}

fn sdArc(p: vec2<f32>, sc: vec2<f32>, ra: f32, rb: f32) -> f32 {
    var q = p;
    q.x = abs(q.x);
    if (sc.y * q.x > sc.x * q.y) {
        return length(q - sc * ra) - rb;
    }
    return abs(length(q) - ra) - rb;
}

fn sdEllipse(p: vec2<f32>, ab: vec2<f32>) -> f32 {
    var q = abs(p);
    var r = ab;
    if (q.x > q.y) { q = q.yx; r = r.yx; }
    let l = r.y * r.y - r.x * r.x;
    let m = r.x * q.x / l;
    let m2 = m * m;
    let n = r.y * q.y / l;
    let n2 = n * n;
    let c = (m2 + n2 - 1.0) / 3.0;
    let c3 = c * c * c;
    let qq = c3 + m2 * n2 * 2.0;
    let d = c3 + m2 * n2;
    let g = m + m * n2;
    var co: f32;
    if (d < 0.0) {
        let h = acos(qq / c3) / 3.0;
        let s = cos(h);
        let t = sin(h) * sqrt(3.0);
        let rx = sqrt(-c * (s + t + 2.0) + m2);
        let ry = sqrt(-c * (s - t + 2.0) + m2);
        co = (ry + sign(l) * rx + abs(g) / (rx * ry) - m) / 2.0;
    } else {
        let h = 2.0 * m * n * sqrt(d);
        let s = sign(qq + h) * pow(abs(qq + h), 1.0 / 3.0);
        let uu = sign(qq - h) * pow(abs(qq - h), 1.0 / 3.0);
        let rx = -s - uu - c * 4.0 + 2.0 * m2;
        let ry = (s - uu) * sqrt(3.0);
        let rm = sqrt(rx * rx + ry * ry);
        co = (ry / sqrt(rm - rx) + 2.0 * g / rm - m) / 2.0;
    }
    let rr = r * vec2(co, sqrt(1.0 - co * co));
    return length(rr - q) * sign(q.y - rr.y);
}

fn evalCubicBezier(p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>, p3: vec2<f32>, t: f32) -> vec2<f32> {
    let c = -p0 + p1;
    let b = p0 - 2.0 * p1 + p2;
    let a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
    return p0 + t * (3.0 * c + t * (3.0 * b + t * a));
}

fn sdCubicBezier(pos: vec2<f32>, p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>, p3: vec2<f32>) -> f32 {
    var bestDist = 1e10;
    var tBest = 0.0;
    let steps = 16;
    for (var i = 0; i <= steps; i = i + 1) {
        let t = f32(i) / f32(steps);
        let pt = evalCubicBezier(p0, p1, p2, p3, t);
        let d = length(pos - pt);
        if (d < bestDist) { bestDist = d; tBest = t; }
    }
    let dt = 1.0 / f32(steps);
    var tLo = max(0.0, tBest - dt);
    var tHi = min(1.0, tBest + dt);
    for (var j = 0; j < 8; j = j + 1) {
        let tMid = (tLo + tHi) * 0.5;
        let tA = (tLo + tMid) * 0.5;
        let tB = (tMid + tHi) * 0.5;
        let dA = length(pos - evalCubicBezier(p0, p1, p2, p3, tA));
        let dB = length(pos - evalCubicBezier(p0, p1, p2, p3, tB));
        if (dA < dB) { tHi = tMid; } else { tLo = tMid; }
    }
    return length(pos - evalCubicBezier(p0, p1, p2, p3, (tLo + tHi) * 0.5));
}

fn evalEllipseArc(rx: f32, ry: f32, rot: f32, theta: f32) -> vec2<f32> {
    let c = cos(rot); let s = sin(rot);
    let px = rx * cos(theta); let py = ry * sin(theta);
    return vec2(c * px - s * py, s * px + c * py);
}

fn sdEllipseArc(pos: vec2<f32>, rx: f32, ry: f32, rot: f32, theta1: f32, theta2: f32) -> f32 {
    var t1 = theta1; var t2 = theta2;
    if (t2 < t1) { t2 = t2 + 6.28318530718; }
    var bestDist = 1e10;
    var tBest = 0.0;
    let steps = 24;
    for (var i = 0; i <= steps; i = i + 1) {
        let t = f32(i) / f32(steps);
        let theta = t1 + t * (t2 - t1);
        let d = length(pos - evalEllipseArc(rx, ry, rot, theta));
        if (d < bestDist) { bestDist = d; tBest = t; }
    }
    let dt = 1.0 / f32(steps);
    var tLo = max(0.0, tBest - dt);
    var tHi = min(1.0, tBest + dt);
    for (var j = 0; j < 8; j = j + 1) {
        let tMid = (tLo + tHi) * 0.5;
        let tA = (tLo + tMid) * 0.5; let tB = (tMid + tHi) * 0.5;
        let dA = length(pos - evalEllipseArc(rx, ry, rot, t1 + tA * (t2 - t1)));
        let dB = length(pos - evalEllipseArc(rx, ry, rot, t1 + tB * (t2 - t1)));
        if (dA < dB) { tHi = tMid; } else { tLo = tMid; }
    }
    return length(pos - evalEllipseArc(rx, ry, rot, t1 + (tLo + tHi) * 0.5 * (t2 - t1)));
}

// =============================================================================
// 3D SDF Primitives (iquilezles.org)
// =============================================================================

fn sdSphere(p: vec3<f32>, r: f32) -> f32 {
    return length(p) - r;
}

fn sdBox3D(p: vec3<f32>, b: vec3<f32>) -> f32 {
    let q = abs(p) - b;
    return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}

fn sdBoxFrame(p: vec3<f32>, b: vec3<f32>, e: f32) -> f32 {
    let q = abs(p) - b;
    let w = abs(q + e) - e;
    return min(min(
        length(max(vec3(q.x, w.y, w.z), vec3(0.0))) + min(max(q.x, max(w.y, w.z)), 0.0),
        length(max(vec3(w.x, q.y, w.z), vec3(0.0))) + min(max(w.x, max(q.y, w.z)), 0.0)),
        length(max(vec3(w.x, w.y, q.z), vec3(0.0))) + min(max(w.x, max(w.y, q.z)), 0.0));
}

fn sdTorus(p: vec3<f32>, t: vec2<f32>) -> f32 {
    let q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

fn sdCappedTorus(p: vec3<f32>, sc: vec2<f32>, ra: f32, rb: f32) -> f32 {
    var q = p;
    q.x = abs(q.x);
    let k = select(length(q.xy), dot(q.xy, sc), sc.y * q.x > sc.x * q.y);
    return sqrt(dot3(q) + ra * ra - 2.0 * ra * k) - rb;
}

fn sdCylinder(p: vec3<f32>, r: f32, h: f32) -> f32 {
    let d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2(0.0)));
}

fn sdCappedCylinder(p: vec3<f32>, h: f32, r: f32) -> f32 {
    let d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2(0.0)));
}

fn sdRoundedCylinder(p: vec3<f32>, ra: f32, rb: f32, h: f32) -> f32 {
    let d = vec2(length(p.xz) - ra + rb, abs(p.y) - h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2(0.0))) - rb;
}

fn sdCapsule(p: vec3<f32>, a: vec3<f32>, b: vec3<f32>, r: f32) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

fn sdVerticalCapsule(p: vec3<f32>, h: f32, r: f32) -> f32 {
    var q = p;
    q.y = q.y - clamp(q.y, 0.0, h);
    return length(q) - r;
}

fn sdCone(p: vec3<f32>, c: vec2<f32>, h: f32) -> f32 {
    let q = h * vec2(c.x / c.y, -1.0);
    let w = vec2(length(p.xz), p.y);
    let a = w - q * clamp(dot(w, q) / dot(q, q), 0.0, 1.0);
    let b = w - q * vec2(clamp(w.x / q.x, 0.0, 1.0), 1.0);
    let k = sign(q.y);
    let d = min(dot(a, a), dot(b, b));
    let s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

fn sdCappedCone(p: vec3<f32>, h: f32, r1: f32, r2: f32) -> f32 {
    let q = vec2(length(p.xz), p.y);
    let k1 = vec2(r2, h);
    let k2 = vec2(r2 - r1, 2.0 * h);
    let ca = vec2(q.x - min(q.x, select(r2, r1, q.y < 0.0)), abs(q.y) - h);
    let cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);
    let s = select(1.0, -1.0, cb.x < 0.0 && ca.y < 0.0);
    return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

fn sdRoundCone(p: vec3<f32>, r1: f32, r2: f32, h: f32) -> f32 {
    let b = (r1 - r2) / h;
    let a = sqrt(1.0 - b * b);
    let q = vec2(length(p.xz), p.y);
    let k = dot(q, vec2(-b, a));
    if (k < 0.0) { return length(q) - r1; }
    if (k > a * h) { return length(q - vec2(0.0, h)) - r2; }
    return dot(q, vec2(a, b)) - r1;
}

fn sdPlane(p: vec3<f32>, n: vec3<f32>, h: f32) -> f32 {
    return dot(p, n) + h;
}

fn sdHexPrism(p: vec3<f32>, h: vec2<f32>) -> f32 {
    let k = vec3(-0.8660254, 0.5, 0.57735);
    var q = abs(p);
    q = vec3(q.x - 2.0 * min(dot(k.xy, q.xy), 0.0) * k.x,
             q.y - 2.0 * min(dot(k.xy, q.xy), 0.0) * k.y,
             q.z);
    let d = vec2(length(q.xy - vec2(clamp(q.x, -k.z * h.x, k.z * h.x), h.x)) * sign(q.y - h.x), q.z - h.y);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2(0.0)));
}

fn sdTriPrism(p: vec3<f32>, h: vec2<f32>) -> f32 {
    let q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + p.y * 0.5, -p.y) - h.x * 0.5);
}

fn sdOctahedron(p: vec3<f32>, s: f32) -> f32 {
    let q = abs(p);
    let m = q.x + q.y + q.z - s;
    var r: vec3<f32>;
    if (3.0 * q.x < m) { r = q.xyz; }
    else if (3.0 * q.y < m) { r = q.yzx; }
    else if (3.0 * q.z < m) { r = q.zxy; }
    else { return m * 0.57735027; }
    let k = clamp(0.5 * (r.z - r.y + s), 0.0, s);
    return length(vec3(r.x, r.y - s + k, r.z - k));
}

fn sdPyramid(p: vec3<f32>, h: f32) -> f32 {
    let m2 = h * h + 0.25;
    var q = p;
    q.x = abs(q.x); q.z = abs(q.z);
    if (q.z > q.x) { q = vec3(q.z, q.y, q.x); }
    q = vec3(q.x - 0.5, q.y, q.z - 0.5);
    let a = vec3(q.x - q.z * clamp((q.x - q.y) * 0.5 / m2, 0.0, 0.5), q.y - h * clamp((q.y + 0.5 * h) / (h * h), 0.0, 1.0), q.z);
    let b = vec3(q.z, q.y - h, q.x - 0.5);
    let s = max(-q.x, 0.0);
    return sqrt(min(dot(a, a), dot(b, b) + s * s)) * sign(max(q.y * m2 - q.x * h * 0.5, q.x));
}

fn sdEllipsoid(p: vec3<f32>, r: vec3<f32>) -> f32 {
    let k0 = length(p / r);
    let k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}

fn sdRhombus(p: vec3<f32>, la: f32, lb: f32, h: f32, ra: f32) -> f32 {
    var q = abs(p);
    let b = vec2(la, lb);
    let f = clamp((ndot(b, b - 2.0 * q.xz)) / dot(b, b), -1.0, 1.0);
    let qq = vec2(length(q.xz - 0.5 * b * vec2(1.0 - f, 1.0 + f)) * sign(q.x * b.y + q.z * b.x - b.x * b.y) - ra, q.y - h);
    return min(max(qq.x, qq.y), 0.0) + length(max(qq, vec2(0.0)));
}

fn sdLink(p: vec3<f32>, le: f32, r1: f32, r2: f32) -> f32 {
    let q = vec3(p.x, max(abs(p.y) - le, 0.0), p.z);
    return length(vec2(length(q.xy) - r1, q.z)) - r2;
}

// =============================================================================
// Evaluate 2D Primitive
// =============================================================================

fn evalPrimitive2D(p: vec2<f32>, prim: Primitive) -> f32 {
    var q = p - vec2(prim.params[0], prim.params[1]);

    if (prim.style.rotate != 0.0) {
        q = rotate2d(q, prim.style.rotate * 3.14159265 / 180.0);
    }

    var d = 1e10;

    switch (prim.ptype) {
        case 0u: { d = sdCircle(q, prim.params[2]); }
        case 1u: {
            let size = vec2(prim.params[2], prim.params[3]);
            if (prim.style.round > 0.0) {
                d = sdRoundedBox2D(q, size, prim.style.round);
            } else {
                d = sdBox2D(q, size);
            }
        }
        case 2u: {
            let a = vec2(prim.params[2], prim.params[3]);
            let b = vec2(prim.params[4], prim.params[5]);
            d = sdSegment(q, a - vec2(prim.params[0], prim.params[1]),
                             b - vec2(prim.params[0], prim.params[1]));
        }
        case 3u: {
            let p0 = vec2(prim.params[2], prim.params[3]);
            let p1 = vec2(prim.params[4], prim.params[5]);
            let p2 = vec2(prim.params[6], prim.params[7]);
            d = sdTriangle(q, p0 - vec2(prim.params[0], prim.params[1]),
                              p1 - vec2(prim.params[0], prim.params[1]),
                              p2 - vec2(prim.params[0], prim.params[1]));
        }
        case 4u: {
            let A = vec2(prim.params[2], prim.params[3]);
            let B = vec2(prim.params[4], prim.params[5]);
            let C = vec2(prim.params[6], prim.params[7]);
            d = sdBezier(q, A - vec2(prim.params[0], prim.params[1]),
                            B - vec2(prim.params[0], prim.params[1]),
                            C - vec2(prim.params[0], prim.params[1]));
        }
        case 5u: {
            let sc = vec2(prim.params[2], prim.params[3]);
            d = sdArc(q, sc, prim.params[4], prim.params[5]);
        }
        case 6u: {
            d = sdEllipse(q, vec2(prim.params[2], prim.params[3]));
        }
        case 7u: {
            let P0 = vec2(prim.params[2], prim.params[3]);
            let P1 = vec2(prim.params[4], prim.params[5]);
            let P2 = vec2(prim.params[6], prim.params[7]);
            let P3 = vec2(prim.params[8], prim.params[9]);
            d = sdCubicBezier(q, P0 - vec2(prim.params[0], prim.params[1]),
                                P1 - vec2(prim.params[0], prim.params[1]),
                                P2 - vec2(prim.params[0], prim.params[1]),
                                P3 - vec2(prim.params[0], prim.params[1]));
        }
        case 8u: {
            d = sdEllipseArc(q, prim.params[2], prim.params[3],
                             prim.params[4], prim.params[5], prim.params[6]);
        }
        default: { d = 1e10; }
    }

    return d;
}

// =============================================================================
// Evaluate 3D Primitive
// =============================================================================

fn evalPrimitive3D(p: vec3<f32>, prim: Primitive) -> f32 {
    let pos = vec3(prim.params[0], prim.params[1], prim.params[2]);
    var q = p - pos;
    var d = 1e10;

    switch (prim.ptype) {
        case 100u: { d = sdSphere(q, prim.params[3]); }
        case 101u: { d = sdBox3D(q, vec3(prim.params[3], prim.params[4], prim.params[5])); }
        case 102u: { d = sdBoxFrame(q, vec3(prim.params[3], prim.params[4], prim.params[5]), prim.params[6]); }
        case 103u: { d = sdTorus(q, vec2(prim.params[3], prim.params[4])); }
        case 104u: { d = sdCappedTorus(q, vec2(prim.params[3], prim.params[4]), prim.params[5], prim.params[6]); }
        case 105u: { d = sdCylinder(q, prim.params[3], prim.params[4]); }
        case 106u: { d = sdCappedCylinder(q, prim.params[3], prim.params[4]); }
        case 107u: { d = sdRoundedCylinder(q, prim.params[3], prim.params[4], prim.params[5]); }
        case 108u: { d = sdVerticalCapsule(q, prim.params[3], prim.params[4]); }
        case 109u: { d = sdCone(q, vec2(prim.params[3], prim.params[4]), prim.params[5]); }
        case 110u: { d = sdCappedCone(q, prim.params[3], prim.params[4], prim.params[5]); }
        case 111u: { d = sdRoundCone(q, prim.params[3], prim.params[4], prim.params[5]); }
        case 112u: { d = sdPlane(q, normalize(vec3(prim.params[3], prim.params[4], prim.params[5])), prim.params[6]); }
        case 113u: { d = sdHexPrism(q, vec2(prim.params[3], prim.params[4])); }
        case 114u: { d = sdTriPrism(q, vec2(prim.params[3], prim.params[4])); }
        case 115u: { d = sdOctahedron(q, prim.params[3]); }
        case 116u: { d = sdPyramid(q, prim.params[3]); }
        case 117u: { d = sdEllipsoid(q, vec3(prim.params[3], prim.params[4], prim.params[5])); }
        case 118u: { d = sdRhombus(q, prim.params[3], prim.params[4], prim.params[5], prim.params[6]); }
        case 119u: { d = sdLink(q, prim.params[3], prim.params[4], prim.params[5]); }
        default: { d = 1e10; }
    }

    return d;
}

// =============================================================================
// Scene SDF for raymarching
// =============================================================================

fn sceneSDF(p: vec3<f32>) -> f32 {
    var d = 1e10;
    let numPrims = i32(u.num_primitives);
    for (var i = 0; i < numPrims; i = i + 1) {
        let prim = primitives[i];
        if (prim.ptype >= 100u) {
            d = min(d, evalPrimitive3D(p, prim));
        }
    }
    return d;
}

fn sceneColor(p: vec3<f32>) -> vec4<f32> {
    var minD = 1e10;
    var col = vec4<f32>(0.8, 0.8, 0.8, 1.0);
    let numPrims = i32(u.num_primitives);
    for (var i = 0; i < numPrims; i = i + 1) {
        let prim = primitives[i];
        if (prim.ptype >= 100u) {
            let d = evalPrimitive3D(p, prim);
            if (d < minD) {
                minD = d;
                col = prim.style.fill;
            }
        }
    }
    return col;
}

fn calcNormal(p: vec3<f32>) -> vec3<f32> {
    let e = vec2(0.001, 0.0);
    return normalize(vec3(
        sceneSDF(p + e.xyy) - sceneSDF(p - e.xyy),
        sceneSDF(p + e.yxy) - sceneSDF(p - e.yxy),
        sceneSDF(p + e.yyx) - sceneSDF(p - e.yyx)));
}

// =============================================================================
// Fragment Shader
// =============================================================================

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let fragCoord = uv * u.resolution;
    var color = vec4<f32>(0.95, 0.95, 0.95, 1.0);

    let numPrims = i32(u.num_primitives);

    // Check if we have 3D primitives
    var has3D = false;
    for (var i = 0; i < numPrims; i = i + 1) {
        if (primitives[i].ptype >= 100u) { has3D = true; break; }
    }

    if (has3D) {
        // Raymarching for 3D
        let aspect = u.resolution.x / u.resolution.y;
        let uv2 = (uv - 0.5) * vec2(aspect, 1.0);

        let camPos = vec3(0.0, 0.0, 3.0);
        let camDir = normalize(vec3(uv2.x, -uv2.y, -1.5));

        var t = 0.0;
        var hit = false;
        var p = camPos;

        for (var i = 0; i < 64; i = i + 1) {
            p = camPos + camDir * t;
            let d = sceneSDF(p);
            if (d < 0.001) { hit = true; break; }
            if (t > 100.0) { break; }
            t = t + d;
        }

        if (hit) {
            let n = calcNormal(p);
            let lightDir = normalize(vec3(1.0, 1.0, 1.0));
            let diff = max(dot(n, lightDir), 0.0);
            let amb = 0.2;
            let matColor = sceneColor(p);
            color = vec4(matColor.rgb * (amb + diff * 0.8), matColor.a);
        } else {
            color = vec4(0.9, 0.9, 0.95, 1.0);
        }
    }

    // 2D primitives (rendered on top or standalone)
    for (var i = 0; i < numPrims; i = i + 1) {
        let prim = primitives[i];
        if (prim.ptype < 100u) {
            let d = evalPrimitive2D(fragCoord, prim);

            if (prim.style.fill.a > 0.0) {
                let fillAlpha = 1.0 - smoothstep(-1.0, 1.0, d);
                color = mix(color, prim.style.fill, fillAlpha * prim.style.fill.a);
            }

            if (prim.style.stroke.a > 0.0 && prim.style.stroke_width > 0.0) {
                let strokeDist = abs(d) - prim.style.stroke_width * 0.5;
                let strokeAlpha = 1.0 - smoothstep(-1.0, 1.0, strokeDist);
                color = mix(color, prim.style.stroke, strokeAlpha * prim.style.stroke.a);
            }
        }
    }

    return color;
}
)";

//-----------------------------------------------------------------------------
// YDrawRenderer
//-----------------------------------------------------------------------------

YDrawRenderer::YDrawRenderer() = default;

YDrawRenderer::~YDrawRenderer() {
    (void)dispose();
}

Result<void> YDrawRenderer::init(const std::string& content) {
    if (!content.empty()) {
        return parse(content);
    }
    return Ok();
}

Result<void> YDrawRenderer::dispose() {
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_primitive_buffer) { wgpuBufferRelease(_primitive_buffer); _primitive_buffer = nullptr; }
    _gpu_initialized = false;
    _current_format = WGPUTextureFormat_Undefined;
    return Ok();
}

void YDrawRenderer::clear() {
    _primitives.clear();
    _primitives_dirty = true;
}

void YDrawRenderer::addPrimitive(const YDrawPrimitiveGPU& prim) {
    if (_primitives.size() < MAX_PRIMITIVES) {
        _primitives.push_back(prim);
        _primitives_dirty = true;
    }
}

Result<void> YDrawRenderer::parse(const std::string& content) {
    if (content.empty()) return Ok();

    // Check if SVG
    size_t i = 0;
    while (i < content.size() && std::isspace(content[i])) i++;
    if (i < content.size()) {
        if (content.substr(i, 5) == "<?xml" || content.substr(i, 4) == "<svg" ||
            content.find("<svg") != std::string::npos) {
            return parseSVG(content);
        }
    }

    return parseYAML(content);
}

//-----------------------------------------------------------------------------
// Color/coordinate parsing
//-----------------------------------------------------------------------------

bool YDrawRenderer::parseColor(const std::string& color, float out[4]) {
    if (color.empty()) return false;

    if (color[0] == '#' && (color.size() == 7 || color.size() == 9)) {
        unsigned int r, g, b, a = 255;
        if (color.size() == 7) {
            sscanf(color.c_str(), "#%02x%02x%02x", &r, &g, &b);
        } else {
            sscanf(color.c_str(), "#%02x%02x%02x%02x", &r, &g, &b, &a);
        }
        out[0] = r / 255.0f;
        out[1] = g / 255.0f;
        out[2] = b / 255.0f;
        out[3] = a / 255.0f;
        return true;
    }
    return false;
}

bool YDrawRenderer::parseVec2(const std::string& str, float out[2]) {
    return sscanf(str.c_str(), "[%f,%f]", &out[0], &out[1]) == 2 ||
           sscanf(str.c_str(), "[%f, %f]", &out[0], &out[1]) == 2;
}

bool YDrawRenderer::parseVec3(const std::string& str, float out[3]) {
    return sscanf(str.c_str(), "[%f,%f,%f]", &out[0], &out[1], &out[2]) == 3 ||
           sscanf(str.c_str(), "[%f, %f, %f]", &out[0], &out[1], &out[2]) == 3;
}

//-----------------------------------------------------------------------------
// YAML parsing
//-----------------------------------------------------------------------------

void YDrawRenderer::registerStruct(const YDrawStruct& s) {
    _structs[s.name] = s;
}

void YDrawRenderer::registerWidget(const YDrawWidget& w) {
    _widgets[w.name] = w;
}

static std::string substituteVars(const std::string& input,
                                   const std::unordered_map<std::string, std::string>& vars) {
    std::string result = input;
    for (const auto& [name, value] : vars) {
        std::string pattern = "$" + name;
        size_t pos = 0;
        while ((pos = result.find(pattern, pos)) != std::string::npos) {
            result.replace(pos, pattern.length(), value);
            pos += value.length();
        }
    }
    return result;
}

YDrawStyle YDrawRenderer::expandStruct(const std::string& name,
                                        const std::vector<std::string>& positional_args) {
    YDrawStyle style;
    auto it = _structs.find(name);
    if (it == _structs.end()) return style;

    const auto& s = it->second;
    std::unordered_map<std::string, std::string> vars;
    for (size_t i = 0; i < s.args.size() && i < positional_args.size(); ++i) {
        vars[s.args[i]] = positional_args[i];
    }

    try {
        std::string substituted = substituteVars(s.body_yaml, vars);
        YAML::Node body = YAML::Load(substituted);

        if (body["fill"]) parseColor(body["fill"].as<std::string>(), style.fill);
        if (body["stroke"]) parseColor(body["stroke"].as<std::string>(), style.stroke);
        if (body["stroke-width"]) style.stroke_width = body["stroke-width"].as<float>();
        if (body["round"]) style.round = body["round"].as<float>();
        if (body["rotate"]) style.rotate = body["rotate"].as<float>();
    } catch (const std::exception& e) {
        spdlog::error("Failed to expand struct '{}': {}", name, e.what());
    }

    return style;
}

static void parseStyleFromNode(const YAML::Node& node, YDrawStyle& style) {
    if (node["fill"]) YDrawRenderer::parseColor(node["fill"].as<std::string>(), style.fill);
    if (node["stroke"]) YDrawRenderer::parseColor(node["stroke"].as<std::string>(), style.stroke);
    if (node["stroke-width"]) style.stroke_width = node["stroke-width"].as<float>();
    if (node["round"]) style.round = node["round"].as<float>();
    if (node["rotate"]) style.rotate = node["rotate"].as<float>();
    if (node["opacity"]) {
        float opacity = node["opacity"].as<float>();
        style.fill[3] *= opacity;
        style.stroke[3] *= opacity;
    }
}

static YDrawPrimitiveGPU parsePrimitive(const std::string& type, const YAML::Node& node) {
    YDrawPrimitiveGPU prim = {};
    memset(&prim, 0, sizeof(prim));

    prim.style.fill[0] = 1.0f;
    prim.style.fill[1] = 1.0f;
    prim.style.fill[2] = 1.0f;
    prim.style.fill[3] = 1.0f;

    parseStyleFromNode(node, prim.style);

    // 2D primitives
    if (type == "circle" || type == "sdCircle") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Circle2D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["radius"]) prim.params[2] = node["radius"].as<float>();
    }
    else if (type == "box" || type == "sdBox") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Box2D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["size"] && node["size"].IsSequence()) {
            prim.params[2] = node["size"][0].as<float>();
            prim.params[3] = node["size"][1].as<float>();
        }
    }
    else if (type == "segment" || type == "sdSegment") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Segment2D);
        if (node["from"] && node["from"].IsSequence()) {
            prim.params[2] = node["from"][0].as<float>();
            prim.params[3] = node["from"][1].as<float>();
        }
        if (node["to"] && node["to"].IsSequence()) {
            prim.params[4] = node["to"][0].as<float>();
            prim.params[5] = node["to"][1].as<float>();
        }
        if (!node["stroke"]) {
            prim.style.stroke[0] = prim.style.fill[0];
            prim.style.stroke[1] = prim.style.fill[1];
            prim.style.stroke[2] = prim.style.fill[2];
            prim.style.stroke[3] = prim.style.fill[3];
            prim.style.fill[3] = 0;
            if (prim.style.stroke_width == 0) prim.style.stroke_width = 2.0f;
        }
    }
    else if (type == "triangle" || type == "sdTriangle") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Triangle2D);
        if (node["p0"] && node["p0"].IsSequence()) {
            prim.params[2] = node["p0"][0].as<float>();
            prim.params[3] = node["p0"][1].as<float>();
        }
        if (node["p1"] && node["p1"].IsSequence()) {
            prim.params[4] = node["p1"][0].as<float>();
            prim.params[5] = node["p1"][1].as<float>();
        }
        if (node["p2"] && node["p2"].IsSequence()) {
            prim.params[6] = node["p2"][0].as<float>();
            prim.params[7] = node["p2"][1].as<float>();
        }
    }
    else if (type == "ellipse" || type == "sdEllipse") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Ellipse2D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["radii"] && node["radii"].IsSequence()) {
            prim.params[2] = node["radii"][0].as<float>();
            prim.params[3] = node["radii"][1].as<float>();
        }
    }
    // 3D primitives
    else if (type == "sphere" || type == "sdSphere") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Sphere3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["radius"]) prim.params[3] = node["radius"].as<float>();
    }
    else if (type == "box3d" || type == "sdBox3D") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Box3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["size"] && node["size"].IsSequence()) {
            prim.params[3] = node["size"][0].as<float>();
            prim.params[4] = node["size"][1].as<float>();
            prim.params[5] = node["size"].size() > 2 ? node["size"][2].as<float>() : node["size"][0].as<float>();
        }
    }
    else if (type == "torus" || type == "sdTorus") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Torus3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["major-radius"]) prim.params[3] = node["major-radius"].as<float>();
        if (node["minor-radius"]) prim.params[4] = node["minor-radius"].as<float>();
    }
    else if (type == "cylinder" || type == "sdCylinder") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Cylinder3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["radius"]) prim.params[3] = node["radius"].as<float>();
        if (node["height"]) prim.params[4] = node["height"].as<float>();
    }
    else if (type == "capsule" || type == "sdCapsule") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Capsule3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["height"]) prim.params[3] = node["height"].as<float>();
        if (node["radius"]) prim.params[4] = node["radius"].as<float>();
    }
    else if (type == "cone" || type == "sdCone") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::CappedCone3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["height"]) prim.params[3] = node["height"].as<float>();
        if (node["radius1"]) prim.params[4] = node["radius1"].as<float>();
        if (node["radius2"]) prim.params[5] = node["radius2"].as<float>();
    }
    else if (type == "octahedron" || type == "sdOctahedron") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Octahedron3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["size"]) prim.params[3] = node["size"].as<float>();
    }
    else if (type == "pyramid" || type == "sdPyramid") {
        prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Pyramid3D);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
            prim.params[2] = node["position"].size() > 2 ? node["position"][2].as<float>() : 0.0f;
        }
        if (node["height"]) prim.params[3] = node["height"].as<float>();
    }

    return prim;
}

Result<void> YDrawRenderer::parseYAML(const std::string& yaml) {
    try {
        YAML::Node doc = YAML::Load(yaml);

        if (!doc.IsSequence()) {
            return Err<void>("YAML must be a sequence of entries");
        }

        std::vector<YAML::Node> bodyNodes;

        for (const auto& entry : doc) {
            if (entry["struct"]) {
                auto structNode = entry["struct"];
                YDrawStruct s;
                s.name = structNode["name"].as<std::string>();
                if (structNode["args"] && structNode["args"].IsSequence()) {
                    for (const auto& arg : structNode["args"]) {
                        s.args.push_back(arg.as<std::string>());
                    }
                }
                std::stringstream ss;
                ss << structNode["body"];
                s.body_yaml = ss.str();
                registerStruct(s);
            }
            else if (entry["widget"]) {
                auto widgetNode = entry["widget"];
                YDrawWidget w;
                w.name = widgetNode["name"].as<std::string>();
                if (widgetNode["args"] && widgetNode["args"].IsSequence()) {
                    for (const auto& arg : widgetNode["args"]) {
                        w.args.push_back(arg.as<std::string>());
                    }
                }
                std::stringstream ss;
                ss << widgetNode["body"];
                w.body_yaml = ss.str();
                registerWidget(w);
            }
            else if (entry["body"]) {
                bodyNodes.push_back(entry["body"]);
            }
        }

        for (const auto& body : bodyNodes) {
            if (!body.IsSequence()) continue;

            for (const auto& item : body) {
                if (!item.IsMap()) continue;

                for (const auto& kv : item) {
                    std::string key = kv.first.as<std::string>();
                    const auto& value = kv.second;

                    auto prim = parsePrimitive(key, value);
                    if (prim.type != 0 || key == "circle" || key == "sdCircle") {
                        _primitives.push_back(prim);
                    }
                    else if (_widgets.count(key)) {
                        const auto& widget = _widgets[key];
                        std::unordered_map<std::string, std::string> vars;

                        if (value.IsSequence()) {
                            for (size_t i = 0; i < widget.args.size() && i < value.size(); ++i) {
                                std::stringstream ss;
                                ss << value[i];
                                vars[widget.args[i]] = ss.str();
                            }
                        } else if (value.IsMap()) {
                            for (const auto& arg : value) {
                                std::stringstream ss;
                                ss << arg.second;
                                vars[arg.first.as<std::string>()] = ss.str();
                            }
                        }

                        std::string substituted = substituteVars(widget.body_yaml, vars);
                        YAML::Node widgetBody = YAML::Load(substituted);

                        if (widgetBody.IsSequence()) {
                            for (const auto& witem : widgetBody) {
                                if (!witem.IsMap()) continue;
                                for (const auto& wkv : witem) {
                                    std::string wkey = wkv.first.as<std::string>();
                                    auto wprim = parsePrimitive(wkey, wkv.second);
                                    _primitives.push_back(wprim);
                                }
                            }
                        }
                    }
                }
            }
        }

        _primitives_dirty = true;
    } catch (const YAML::Exception& e) {
        return Err<void>(std::string("YAML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Err<void>(std::string("Error: ") + e.what());
    }

    return Ok();
}

//-----------------------------------------------------------------------------
// SVG parsing (simplified)
//-----------------------------------------------------------------------------

static std::string getAttr(const std::string& tag, const std::string& attr) {
    std::string search = attr + "=\"";
    size_t pos = tag.find(search);
    if (pos == std::string::npos) {
        search = attr + "='";
        pos = tag.find(search);
    }
    if (pos == std::string::npos) return "";
    pos += search.length();
    char quote = tag[pos - 1];
    size_t end = tag.find(quote, pos);
    if (end == std::string::npos) return "";
    return tag.substr(pos, end - pos);
}

static float parseNumber(const std::string& s) {
    if (s.empty()) return 0.0f;
    try { return std::stof(s); } catch (...) { return 0.0f; }
}

Result<void> YDrawRenderer::parseSVG(const std::string& svg) {
    try {
        size_t pos = 0;

        while ((pos = svg.find("<", pos)) != std::string::npos) {
            if (svg.substr(pos, 4) == "<!--" || svg.substr(pos, 2) == "<?") {
                pos++;
                continue;
            }

            size_t tagEnd = svg.find(">", pos);
            if (tagEnd == std::string::npos) break;

            std::string tag = svg.substr(pos, tagEnd - pos + 1);
            pos = tagEnd + 1;

            if (tag.size() > 1 && tag[1] == '/') continue;

            size_t nameStart = 1;
            size_t nameEnd = nameStart;
            while (nameEnd < tag.size() && !std::isspace(tag[nameEnd]) && tag[nameEnd] != '>' && tag[nameEnd] != '/') {
                nameEnd++;
            }
            std::string elemName = tag.substr(nameStart, nameEnd - nameStart);

            YDrawStyle style = {};
            std::string fillStr = getAttr(tag, "fill");
            std::string strokeStr = getAttr(tag, "stroke");
            std::string strokeWidthStr = getAttr(tag, "stroke-width");

            if (!fillStr.empty() && fillStr != "none") {
                parseColor(fillStr, style.fill);
            }
            if (!strokeStr.empty() && strokeStr != "none") {
                parseColor(strokeStr, style.stroke);
                style.stroke_width = strokeWidthStr.empty() ? 1.0f : parseNumber(strokeWidthStr);
            }

            if (elemName == "circle") {
                float cx = parseNumber(getAttr(tag, "cx"));
                float cy = parseNumber(getAttr(tag, "cy"));
                float r = parseNumber(getAttr(tag, "r"));

                YDrawPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Circle2D);
                prim.params[0] = cx; prim.params[1] = cy;
                prim.params[2] = r;
                prim.style = style;
                _primitives.push_back(prim);
            }
            else if (elemName == "ellipse") {
                float cx = parseNumber(getAttr(tag, "cx"));
                float cy = parseNumber(getAttr(tag, "cy"));
                float rx = parseNumber(getAttr(tag, "rx"));
                float ry = parseNumber(getAttr(tag, "ry"));

                YDrawPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Ellipse2D);
                prim.params[0] = cx; prim.params[1] = cy;
                prim.params[2] = rx; prim.params[3] = ry;
                prim.style = style;
                _primitives.push_back(prim);
            }
            else if (elemName == "rect") {
                float x = parseNumber(getAttr(tag, "x"));
                float y = parseNumber(getAttr(tag, "y"));
                float w = parseNumber(getAttr(tag, "width"));
                float h = parseNumber(getAttr(tag, "height"));
                float rx = parseNumber(getAttr(tag, "rx"));

                YDrawPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Box2D);
                prim.params[0] = x + w / 2; prim.params[1] = y + h / 2;
                prim.params[2] = w / 2; prim.params[3] = h / 2;
                prim.style = style;
                prim.style.round = rx;
                _primitives.push_back(prim);
            }
            else if (elemName == "line") {
                float x1 = parseNumber(getAttr(tag, "x1"));
                float y1 = parseNumber(getAttr(tag, "y1"));
                float x2 = parseNumber(getAttr(tag, "x2"));
                float y2 = parseNumber(getAttr(tag, "y2"));

                YDrawPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(YDrawPrimitiveType::Segment2D);
                prim.params[2] = x1; prim.params[3] = y1;
                prim.params[4] = x2; prim.params[5] = y2;
                prim.style = style;
                _primitives.push_back(prim);
            }
        }

        _primitives_dirty = true;
        spdlog::info("YDraw SVG parsed: {} primitives", _primitives.size());
    } catch (const std::exception& e) {
        return Err<void>(std::string("SVG parse error: ") + e.what());
    }

    return Ok();
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

Result<void> YDrawRenderer::render(WebGPUContext& ctx,
                                    WGPURenderPassEncoder pass,
                                    float x, float y, float width, float height,
                                    float screenWidth, float screenHeight,
                                    WGPUTextureFormat targetFormat) {
    if (_failed) return Err<void>("YDrawRenderer already failed");
    if (_primitives.empty()) return Ok();

    if (!_gpu_initialized || _current_format != targetFormat) {
        if (_gpu_initialized) {
            (void)dispose();
        }
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("YDrawRenderer: Failed to create pipeline", result);
        }
        _gpu_initialized = true;
        _current_format = targetFormat;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("YDrawRenderer: pipeline not initialized");
    }

    // Calculate NDC coordinates
    float ndcX = (x / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / screenHeight) * 2.0f;
    float ndcW = (width / screenWidth) * 2.0f;
    float ndcH = (height / screenHeight) * 2.0f;

    struct Uniforms {
        float rect[4];
        float resolution[2];
        float time;
        float num_primitives;
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.resolution[0] = width;
    uniforms.resolution[1] = height;
    uniforms.time = _time;
    uniforms.num_primitives = static_cast<float>(_primitives.size());

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    if (_primitives_dirty && !_primitives.empty()) {
        wgpuQueueWriteBuffer(ctx.getQueue(), _primitive_buffer, 0,
                             _primitives.data(), _primitives.size() * sizeof(YDrawPrimitiveGPU));
        _primitives_dirty = false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    return Ok();
}

Result<void> YDrawRenderer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 32;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

    // Primitive storage buffer
    WGPUBufferDescriptor primBufDesc = {};
    primBufDesc.size = MAX_PRIMITIVES * sizeof(YDrawPrimitiveGPU);
    primBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    _primitive_buffer = wgpuDeviceCreateBuffer(device, &primBufDesc);
    if (!_primitive_buffer) return Err<void>("Failed to create primitive buffer");

    // Shader
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(YDRAW_SHADER);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bgl) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Bind group
    WGPUBindGroupEntry bgE[2] = {};
    bgE[0].binding = 0;
    bgE[0].buffer = _uniform_buffer;
    bgE[0].size = 32;
    bgE[1].binding = 1;
    bgE[1].buffer = _primitive_buffer;
    bgE[1].size = MAX_PRIMITIVES * sizeof(YDrawPrimitiveGPU);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = WGPU_STR("fs_main");

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) return Err<void>("Failed to create render pipeline");

    spdlog::info("YDrawRenderer: pipeline created");
    return Ok();
}

//-----------------------------------------------------------------------------
// Helper functions for building primitives
//-----------------------------------------------------------------------------

namespace ydraw {

YDrawPrimitiveGPU circle(float x, float y, float radius, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Circle2D);
    p.params[0] = x; p.params[1] = y; p.params[2] = radius;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU box(float x, float y, float halfW, float halfH, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Box2D);
    p.params[0] = x; p.params[1] = y; p.params[2] = halfW; p.params[3] = halfH;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU segment(float x1, float y1, float x2, float y2, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Segment2D);
    p.params[2] = x1; p.params[3] = y1; p.params[4] = x2; p.params[5] = y2;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU triangle(float x0, float y0, float x1, float y1, float x2, float y2, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Triangle2D);
    p.params[2] = x0; p.params[3] = y0;
    p.params[4] = x1; p.params[5] = y1;
    p.params[6] = x2; p.params[7] = y2;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU ellipse(float x, float y, float rx, float ry, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Ellipse2D);
    p.params[0] = x; p.params[1] = y; p.params[2] = rx; p.params[3] = ry;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU sphere(float x, float y, float z, float radius, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Sphere3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z; p.params[3] = radius;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU box3d(float x, float y, float z, float hx, float hy, float hz, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Box3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z;
    p.params[3] = hx; p.params[4] = hy; p.params[5] = hz;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU torus(float x, float y, float z, float majorR, float minorR, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Torus3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z;
    p.params[3] = majorR; p.params[4] = minorR;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU cylinder(float x, float y, float z, float radius, float height, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Cylinder3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z;
    p.params[3] = radius; p.params[4] = height;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU capsule(float x, float y, float z, float radius, float height, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Capsule3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z;
    p.params[3] = height; p.params[4] = radius;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU cone(float x, float y, float z, float radius, float height, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::CappedCone3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z;
    p.params[3] = height; p.params[4] = radius; p.params[5] = 0.0f;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU octahedron(float x, float y, float z, float size, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Octahedron3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z; p.params[3] = size;
    p.style = style;
    return p;
}

YDrawPrimitiveGPU pyramid(float x, float y, float z, float size, float height, const YDrawStyle& style) {
    YDrawPrimitiveGPU p = {};
    p.type = static_cast<uint32_t>(YDrawPrimitiveType::Pyramid3D);
    p.params[0] = x; p.params[1] = y; p.params[2] = z; p.params[3] = height;
    p.style = style;
    return p;
}

YDrawStyle solid(float r, float g, float b, float a) {
    YDrawStyle s = {};
    s.fill[0] = r; s.fill[1] = g; s.fill[2] = b; s.fill[3] = a;
    return s;
}

YDrawStyle stroke(float r, float g, float b, float width, float a) {
    YDrawStyle s = {};
    s.fill[3] = 0;
    s.stroke[0] = r; s.stroke[1] = g; s.stroke[2] = b; s.stroke[3] = a;
    s.stroke_width = width;
    return s;
}

} // namespace ydraw

} // namespace yetty
