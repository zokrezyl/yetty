#include "sdf-primitives.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <regex>

namespace yetty {

//-----------------------------------------------------------------------------
// WGSL Shader with SDF primitives (based on Inigo Quilez)
//-----------------------------------------------------------------------------

static const char* SDF_SHADER = R"(
struct Uniforms {
    rect: vec4<f32>,
    resolution: vec2<f32>,
    time: f32,
    num_primitives: f32,
}

struct SDFStyle {
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
    style: SDFStyle,
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

// Helper functions
fn dot2(v: vec2<f32>) -> f32 { return dot(v, v); }

fn rotate2d(p: vec2<f32>, angle: f32) -> vec2<f32> {
    let c = cos(angle);
    let s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// SDF Primitives (Inigo Quilez - iquilezles.org)

fn sdCircle(p: vec2<f32>, r: f32) -> f32 {
    return length(p) - r;
}

fn sdBox(p: vec2<f32>, b: vec2<f32>) -> f32 {
    let d = abs(p) - b;
    return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

fn sdRoundedBox(p: vec2<f32>, b: vec2<f32>, r: f32) -> f32 {
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

// Ellipse SDF (Inigo Quilez - iquilezles.org)
fn sdEllipse(p: vec2<f32>, ab: vec2<f32>) -> f32 {
    var q = abs(p);
    var r = ab;
    if (q.x > q.y) {
        q = q.yx;
        r = r.yx;
    }
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

// Cubic Bezier evaluation: p(t) = (1-t)³·p0 + 3·(1-t)²·t·p1 + 3·(1-t)·t²·p2 + t³·p3
fn evalCubicBezier(p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>, p3: vec2<f32>, t: f32) -> vec2<f32> {
    let c = -p0 + p1;
    let b = p0 - 2.0 * p1 + p2;
    let a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
    return p0 + t * (3.0 * c + t * (3.0 * b + t * a));
}

// Cubic Bezier SDF via numerical sampling
fn sdCubicBezier(pos: vec2<f32>, p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>, p3: vec2<f32>) -> f32 {
    var minDist = 1e10;
    let steps = 16;
    for (var i = 0; i <= steps; i = i + 1) {
        let t = f32(i) / f32(steps);
        let pt = evalCubicBezier(p0, p1, p2, p3, t);
        let d = length(pos - pt);
        minDist = min(minDist, d);
    }
    // Refine with binary search around minimum
    var tBest = 0.0;
    var bestDist = 1e10;
    for (var i = 0; i <= steps; i = i + 1) {
        let t = f32(i) / f32(steps);
        let pt = evalCubicBezier(p0, p1, p2, p3, t);
        let d = length(pos - pt);
        if (d < bestDist) {
            bestDist = d;
            tBest = t;
        }
    }
    // Refine
    let dt = 1.0 / f32(steps);
    var tLo = max(0.0, tBest - dt);
    var tHi = min(1.0, tBest + dt);
    for (var j = 0; j < 8; j = j + 1) {
        let tMid = (tLo + tHi) * 0.5;
        let tA = (tLo + tMid) * 0.5;
        let tB = (tMid + tHi) * 0.5;
        let dA = length(pos - evalCubicBezier(p0, p1, p2, p3, tA));
        let dB = length(pos - evalCubicBezier(p0, p1, p2, p3, tB));
        if (dA < dB) {
            tHi = tMid;
        } else {
            tLo = tMid;
        }
    }
    let tFinal = (tLo + tHi) * 0.5;
    return length(pos - evalCubicBezier(p0, p1, p2, p3, tFinal));
}

// Elliptical arc point evaluation
fn evalEllipseArc(rx: f32, ry: f32, rot: f32, theta: f32) -> vec2<f32> {
    // Point on ellipse at angle theta, then rotated by rot
    let c = cos(rot);
    let s = sin(rot);
    let px = rx * cos(theta);
    let py = ry * sin(theta);
    return vec2(c * px - s * py, s * px + c * py);
}

// Elliptical arc SDF via numerical sampling
// center is at origin (caller offsets), radii rx/ry, rotation, angle range [theta1, theta2]
fn sdEllipseArc(pos: vec2<f32>, rx: f32, ry: f32, rot: f32, theta1: f32, theta2: f32) -> f32 {
    var minDist = 1e10;
    let steps = 24;  // More steps for arcs since they can be long

    // Handle angle wrapping
    var t1 = theta1;
    var t2 = theta2;
    if (t2 < t1) {
        t2 = t2 + 6.28318530718;  // 2*PI
    }

    // Sample along the arc
    for (var i = 0; i <= steps; i = i + 1) {
        let t = f32(i) / f32(steps);
        let theta = t1 + t * (t2 - t1);
        let pt = evalEllipseArc(rx, ry, rot, theta);
        let d = length(pos - pt);
        minDist = min(minDist, d);
    }

    // Find best t for refinement
    var tBest = 0.0;
    var bestDist = 1e10;
    for (var i = 0; i <= steps; i = i + 1) {
        let t = f32(i) / f32(steps);
        let theta = t1 + t * (t2 - t1);
        let pt = evalEllipseArc(rx, ry, rot, theta);
        let d = length(pos - pt);
        if (d < bestDist) {
            bestDist = d;
            tBest = t;
        }
    }

    // Binary search refinement
    let dt = 1.0 / f32(steps);
    var tLo = max(0.0, tBest - dt);
    var tHi = min(1.0, tBest + dt);
    for (var j = 0; j < 8; j = j + 1) {
        let tMid = (tLo + tHi) * 0.5;
        let tA = (tLo + tMid) * 0.5;
        let tB = (tMid + tHi) * 0.5;
        let thetaA = t1 + tA * (t2 - t1);
        let thetaB = t1 + tB * (t2 - t1);
        let dA = length(pos - evalEllipseArc(rx, ry, rot, thetaA));
        let dB = length(pos - evalEllipseArc(rx, ry, rot, thetaB));
        if (dA < dB) {
            tHi = tMid;
        } else {
            tLo = tMid;
        }
    }
    let tFinal = (tLo + tHi) * 0.5;
    let thetaFinal = t1 + tFinal * (t2 - t1);
    return length(pos - evalEllipseArc(rx, ry, rot, thetaFinal));
}

// Evaluate a single primitive
fn evalPrimitive(p: vec2<f32>, prim: Primitive) -> f32 {
    var q = p - vec2(prim.params[0], prim.params[1]);  // position offset

    // Apply rotation
    if (prim.style.rotate != 0.0) {
        q = rotate2d(q, prim.style.rotate * 3.14159265 / 180.0);
    }

    var d = 1e10;

    switch (prim.ptype) {
        case 0u: {  // Circle
            d = sdCircle(q, prim.params[2]);
        }
        case 1u: {  // Box
            let size = vec2(prim.params[2], prim.params[3]);
            if (prim.style.round > 0.0) {
                d = sdRoundedBox(q, size, prim.style.round);
            } else {
                d = sdBox(q, size);
            }
        }
        case 2u: {  // Segment
            let a = vec2(prim.params[2], prim.params[3]);
            let b = vec2(prim.params[4], prim.params[5]);
            d = sdSegment(q, a - vec2(prim.params[0], prim.params[1]),
                             b - vec2(prim.params[0], prim.params[1]));
        }
        case 3u: {  // Triangle
            let p0 = vec2(prim.params[2], prim.params[3]);
            let p1 = vec2(prim.params[4], prim.params[5]);
            let p2 = vec2(prim.params[6], prim.params[7]);
            d = sdTriangle(q, p0 - vec2(prim.params[0], prim.params[1]),
                              p1 - vec2(prim.params[0], prim.params[1]),
                              p2 - vec2(prim.params[0], prim.params[1]));
        }
        case 4u: {  // Bezier
            let A = vec2(prim.params[2], prim.params[3]);
            let B = vec2(prim.params[4], prim.params[5]);
            let C = vec2(prim.params[6], prim.params[7]);
            d = sdBezier(q, A - vec2(prim.params[0], prim.params[1]),
                            B - vec2(prim.params[0], prim.params[1]),
                            C - vec2(prim.params[0], prim.params[1]));
        }
        case 5u: {  // Arc
            let sc = vec2(prim.params[2], prim.params[3]);  // sin/cos of angle
            let ra = prim.params[4];  // radius
            let rb = prim.params[5];  // thickness
            d = sdArc(q, sc, ra, rb);
        }
        case 6u: {  // Ellipse
            let ab = vec2(prim.params[2], prim.params[3]);  // radii
            d = sdEllipse(q, ab);
        }
        case 7u: {  // CubicBezier
            let P0 = vec2(prim.params[2], prim.params[3]);
            let P1 = vec2(prim.params[4], prim.params[5]);
            let P2 = vec2(prim.params[6], prim.params[7]);
            let P3 = vec2(prim.params[8], prim.params[9]);
            d = sdCubicBezier(q, P0 - vec2(prim.params[0], prim.params[1]),
                                 P1 - vec2(prim.params[0], prim.params[1]),
                                 P2 - vec2(prim.params[0], prim.params[1]),
                                 P3 - vec2(prim.params[0], prim.params[1]));
        }
        case 8u: {  // EllipseArc
            let rx = prim.params[2];      // x radius
            let ry = prim.params[3];      // y radius
            let rot = prim.params[4];     // rotation (radians)
            let theta1 = prim.params[5];  // start angle (radians)
            let theta2 = prim.params[6];  // end angle (radians)
            d = sdEllipseArc(q, rx, ry, rot, theta1, theta2);
        }
        default: {
            d = 1e10;
        }
    }

    return d;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let fragCoord = uv * u.resolution;

    // Background
    var color = vec4<f32>(0.95, 0.95, 0.95, 1.0);

    let numPrims = i32(u.num_primitives);

    // Render primitives back to front
    for (var i = 0; i < numPrims; i = i + 1) {
        let prim = primitives[i];
        let d = evalPrimitive(fragCoord, prim);

        // Anti-aliased fill
        if (prim.style.fill.a > 0.0) {
            let fillAlpha = 1.0 - smoothstep(-1.0, 1.0, d);
            color = mix(color, prim.style.fill, fillAlpha * prim.style.fill.a);
        }

        // Anti-aliased stroke
        if (prim.style.stroke.a > 0.0 && prim.style.stroke_width > 0.0) {
            let strokeDist = abs(d) - prim.style.stroke_width * 0.5;
            let strokeAlpha = 1.0 - smoothstep(-1.0, 1.0, strokeDist);
            color = mix(color, prim.style.stroke, strokeAlpha * prim.style.stroke.a);
        }
    }

    return color;
}
)";

//-----------------------------------------------------------------------------
// SDFPrimitivesPlugin
//-----------------------------------------------------------------------------

SDFPrimitivesPlugin::~SDFPrimitivesPlugin() {
    (void)dispose();
}

Result<PluginPtr> SDFPrimitivesPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new SDFPrimitivesPlugin(std::move(engine)));
    if (auto res = static_cast<SDFPrimitivesPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init SDFPrimitivesPlugin", res);
    }
    return Ok(p);
}

Result<void> SDFPrimitivesPlugin::init() noexcept {
    _initialized = true;
    return Ok();
}

Result<void> SDFPrimitivesPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose SDFPrimitivesPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> SDFPrimitivesPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<SDFPrimitivesLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize SDFPrimitives layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> SDFPrimitivesPlugin::renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset, uint32_t termRows,
                                     bool isAltScreen) {
    if (!engine_) return Err<void>("SDFPrimitivesPlugin::renderAll: no engine");

    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<SDFPrimitivesLayer>(layerBase);

        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;
            }
        }

        if (auto res = layer->render(*engine_->context(), targetView, targetFormat,
                                      screenWidth, screenHeight,
                                      pixelX, pixelY, pixelW, pixelH); !res) {
            return Err<void>("Failed to render SDFPrimitives layer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// SDFPrimitivesLayer
//-----------------------------------------------------------------------------

SDFPrimitivesLayer::SDFPrimitivesLayer() = default;

SDFPrimitivesLayer::~SDFPrimitivesLayer() {
    (void)dispose();
}

// Check if payload is SVG
static bool isSVG(const std::string& payload) {
    // Skip whitespace
    size_t i = 0;
    while (i < payload.size() && std::isspace(payload[i])) i++;
    if (i >= payload.size()) return false;

    // Check for <?xml or <svg
    if (payload.substr(i, 5) == "<?xml" || payload.substr(i, 4) == "<svg") {
        return true;
    }
    // Also check for SVG with namespace
    if (payload.find("<svg") != std::string::npos) {
        return true;
    }
    return false;
}

Result<void> SDFPrimitivesLayer::init(const std::string& payload) {
    _payload = payload;

    if (!payload.empty()) {
        Result<void> result;
        if (isSVG(payload)) {
            result = parseSVG(payload);
            if (!result) {
                return Err<void>("Failed to parse SVG", result);
            }
        } else {
            result = parseYAML(payload);
            if (!result) {
                return Err<void>("Failed to parse YAML", result);
            }
        }
    }

    spdlog::info("SDFPrimitivesLayer: initialized with {} primitives", _primitives.size());
    return Ok();
}

Result<void> SDFPrimitivesLayer::dispose() {
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_primitive_buffer) { wgpuBufferRelease(_primitive_buffer); _primitive_buffer = nullptr; }
    _gpu_initialized = false;
    return Ok();
}

Result<void> SDFPrimitivesLayer::update(double deltaTime) {
    _time += static_cast<float>(deltaTime);
    return Ok();
}

bool SDFPrimitivesLayer::onMouseMove(float localX, float localY) {
    (void)localX; (void)localY;
    return true;
}

bool SDFPrimitivesLayer::onMouseButton(int button, bool pressed) {
    (void)button; (void)pressed;
    return true;
}

bool SDFPrimitivesLayer::onKey(int key, int scancode, int action, int mods) {
    (void)key; (void)scancode; (void)action; (void)mods;
    return true;
}

bool SDFPrimitivesLayer::onChar(unsigned int codepoint) {
    (void)codepoint;
    return true;
}

//-----------------------------------------------------------------------------
// YAML Parsing
//-----------------------------------------------------------------------------

bool SDFPrimitivesLayer::parseColor(const std::string& color, float out[4]) {
    if (color.empty()) return false;

    // Handle #RRGGBB or #RRGGBBAA
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

bool SDFPrimitivesLayer::parseVec2(const std::string& str, float out[2]) {
    return sscanf(str.c_str(), "[%f,%f]", &out[0], &out[1]) == 2 ||
           sscanf(str.c_str(), "[%f, %f]", &out[0], &out[1]) == 2;
}

void SDFPrimitivesLayer::registerStruct(const SDFStruct& s) {
    _structs[s.name] = s;
}

void SDFPrimitivesLayer::registerWidget(const SDFWidget& w) {
    _widgets[w.name] = w;
}

// Substitute $variables in a string
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

SDFStyle SDFPrimitivesLayer::expandStruct(const std::string& name,
                                           const std::vector<std::string>& positional_args) {
    SDFStyle style;

    auto it = _structs.find(name);
    if (it == _structs.end()) {
        spdlog::warn("Struct '{}' not found", name);
        return style;
    }

    const auto& s = it->second;

    // Build variable map from positional args
    std::unordered_map<std::string, std::string> vars;
    for (size_t i = 0; i < s.args.size() && i < positional_args.size(); ++i) {
        vars[s.args[i]] = positional_args[i];
    }

    // Parse the substituted body
    try {
        std::string substituted = substituteVars(s.body_yaml, vars);
        YAML::Node body = YAML::Load(substituted);

        if (body["fill"]) {
            parseColor(body["fill"].as<std::string>(), style.fill);
        }
        if (body["stroke"]) {
            parseColor(body["stroke"].as<std::string>(), style.stroke);
        }
        if (body["stroke-width"]) {
            style.stroke_width = body["stroke-width"].as<float>();
        }
        if (body["round"]) {
            style.round = body["round"].as<float>();
        }
        if (body["rotate"]) {
            style.rotate = body["rotate"].as<float>();
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to expand struct '{}': {}", name, e.what());
    }

    return style;
}

static void parseStyleFromNode(const YAML::Node& node, SDFStyle& style) {
    if (node["fill"]) {
        SDFPrimitivesLayer::parseColor(node["fill"].as<std::string>(), style.fill);
    }
    if (node["stroke"]) {
        SDFPrimitivesLayer::parseColor(node["stroke"].as<std::string>(), style.stroke);
    }
    if (node["stroke-width"]) {
        style.stroke_width = node["stroke-width"].as<float>();
    }
    if (node["round"]) {
        style.round = node["round"].as<float>();
    }
    if (node["rotate"]) {
        style.rotate = node["rotate"].as<float>();
    }
    if (node["opacity"]) {
        float opacity = node["opacity"].as<float>();
        style.fill[3] *= opacity;
        style.stroke[3] *= opacity;
    }
}

static SDFPrimitiveGPU parsePrimitive(const std::string& type, const YAML::Node& node) {
    SDFPrimitiveGPU prim = {};
    memset(&prim, 0, sizeof(prim));

    // Default style
    prim.style.fill[0] = 1.0f;
    prim.style.fill[1] = 1.0f;
    prim.style.fill[2] = 1.0f;
    prim.style.fill[3] = 1.0f;

    parseStyleFromNode(node, prim.style);

    if (type == "sdCircle") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Circle);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["radius"]) {
            prim.params[2] = node["radius"].as<float>();
        }
    }
    else if (type == "sdBox") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Box);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["size"] && node["size"].IsSequence()) {
            prim.params[2] = node["size"][0].as<float>();
            prim.params[3] = node["size"][1].as<float>();
        }
    }
    else if (type == "sdSegment") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
        prim.params[0] = 0; prim.params[1] = 0;  // No position offset for segment
        if (node["from"] && node["from"].IsSequence()) {
            prim.params[2] = node["from"][0].as<float>();
            prim.params[3] = node["from"][1].as<float>();
        }
        if (node["to"] && node["to"].IsSequence()) {
            prim.params[4] = node["to"][0].as<float>();
            prim.params[5] = node["to"][1].as<float>();
        }
        // Segments are strokes by default
        if (!node["stroke"]) {
            prim.style.stroke[0] = prim.style.fill[0];
            prim.style.stroke[1] = prim.style.fill[1];
            prim.style.stroke[2] = prim.style.fill[2];
            prim.style.stroke[3] = prim.style.fill[3];
            prim.style.fill[3] = 0;  // No fill
            if (prim.style.stroke_width == 0) prim.style.stroke_width = 2.0f;
        }
    }
    else if (type == "sdTriangle") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Triangle);
        prim.params[0] = 0; prim.params[1] = 0;
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
    else if (type == "sdBezier") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Bezier);
        prim.params[0] = 0; prim.params[1] = 0;
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
        // Beziers are strokes by default
        if (!node["stroke"] && !node["fill"]) {
            prim.style.stroke[0] = 0; prim.style.stroke[1] = 0;
            prim.style.stroke[2] = 0; prim.style.stroke[3] = 1;
            prim.style.fill[3] = 0;
            if (prim.style.stroke_width == 0) prim.style.stroke_width = 2.0f;
        }
    }
    else if (type == "sdArc") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Arc);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["angle"] && node["angle"].IsSequence()) {
            // Convert angle to sin/cos
            float startAngle = node["angle"][0].as<float>() * 3.14159265f / 180.0f;
            float endAngle = node["angle"][1].as<float>() * 3.14159265f / 180.0f;
            float midAngle = (startAngle + endAngle) * 0.5f;
            prim.params[2] = sin(midAngle);
            prim.params[3] = cos(midAngle);
        }
        if (node["radius"]) {
            prim.params[4] = node["radius"].as<float>();
        }
        prim.params[5] = node["thickness"] ? node["thickness"].as<float>() : 2.0f;
    }
    else if (type == "sdEllipse") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Ellipse);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["size"] && node["size"].IsSequence()) {
            // size is [width, height] - convert to half-axes
            prim.params[2] = node["size"][0].as<float>() * 0.5f;
            prim.params[3] = node["size"][1].as<float>() * 0.5f;
        } else if (node["radii"] && node["radii"].IsSequence()) {
            // radii is direct half-axes [rx, ry]
            prim.params[2] = node["radii"][0].as<float>();
            prim.params[3] = node["radii"][1].as<float>();
        }
    }
    else if (type == "sdCubicBezier") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::CubicBezier);
        prim.params[0] = 0; prim.params[1] = 0;  // offset (not used, points are absolute)
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
        if (node["p3"] && node["p3"].IsSequence()) {
            prim.params[8] = node["p3"][0].as<float>();
            prim.params[9] = node["p3"][1].as<float>();
        }
        // Cubic beziers are strokes by default
        if (!node["stroke"] && !node["fill"]) {
            prim.style.stroke[0] = 0; prim.style.stroke[1] = 0;
            prim.style.stroke[2] = 0; prim.style.stroke[3] = 1;
            prim.style.fill[3] = 0;
            if (prim.style.stroke_width == 0) prim.style.stroke_width = 2.0f;
        }
    }
    else if (type == "sdEllipseArc") {
        prim.type = static_cast<uint32_t>(SDFPrimitiveType::EllipseArc);
        if (node["position"] && node["position"].IsSequence()) {
            prim.params[0] = node["position"][0].as<float>();
            prim.params[1] = node["position"][1].as<float>();
        }
        if (node["radii"] && node["radii"].IsSequence()) {
            prim.params[2] = node["radii"][0].as<float>();  // rx
            prim.params[3] = node["radii"][1].as<float>();  // ry
        } else if (node["rx"] && node["ry"]) {
            prim.params[2] = node["rx"].as<float>();
            prim.params[3] = node["ry"].as<float>();
        }
        // Rotation of the ellipse (in degrees, converted to radians)
        prim.params[4] = node["rotation"] ? node["rotation"].as<float>() * 3.14159265f / 180.0f : 0.0f;
        // Start and end angles (in degrees, converted to radians)
        if (node["angle"] && node["angle"].IsSequence()) {
            prim.params[5] = node["angle"][0].as<float>() * 3.14159265f / 180.0f;  // theta1
            prim.params[6] = node["angle"][1].as<float>() * 3.14159265f / 180.0f;  // theta2
        }
        // Ellipse arcs are strokes by default
        if (!node["stroke"] && !node["fill"]) {
            prim.style.stroke[0] = 0; prim.style.stroke[1] = 0;
            prim.style.stroke[2] = 0; prim.style.stroke[3] = 1;
            prim.style.fill[3] = 0;
            if (prim.style.stroke_width == 0) prim.style.stroke_width = 2.0f;
        }
    }

    return prim;
}

Result<void> SDFPrimitivesLayer::parseYAML(const std::string& yaml) {
    try {
        YAML::Node doc = YAML::Load(yaml);

        if (!doc.IsSequence()) {
            return Err<void>("YAML must be a sequence of entries");
        }

        std::vector<YAML::Node> bodyNodes;

        for (const auto& entry : doc) {
            if (entry["struct"]) {
                auto structNode = entry["struct"];
                SDFStruct s;
                s.name = structNode["name"].as<std::string>();
                if (structNode["args"] && structNode["args"].IsSequence()) {
                    for (const auto& arg : structNode["args"]) {
                        s.args.push_back(arg.as<std::string>());
                    }
                }
                // Store body as raw YAML string for lazy evaluation
                std::stringstream ss;
                ss << structNode["body"];
                s.body_yaml = ss.str();
                registerStruct(s);
            }
            else if (entry["widget"]) {
                auto widgetNode = entry["widget"];
                SDFWidget w;
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

        // Process all body sections
        for (const auto& body : bodyNodes) {
            if (!body.IsSequence()) continue;

            for (const auto& item : body) {
                if (!item.IsMap()) continue;

                for (const auto& kv : item) {
                    std::string key = kv.first.as<std::string>();
                    const auto& value = kv.second;

                    // Check if it's a built-in primitive
                    if (key.substr(0, 2) == "sd") {
                        auto prim = parsePrimitive(key, value);
                        _primitives.push_back(prim);
                    }
                    // Check if it's a widget
                    else if (_widgets.count(key)) {
                        const auto& widget = _widgets[key];

                        // Build variable map
                        std::unordered_map<std::string, std::string> vars;

                        // Handle positional args (if value is sequence)
                        if (value.IsSequence()) {
                            for (size_t i = 0; i < widget.args.size() && i < value.size(); ++i) {
                                std::stringstream ss;
                                ss << value[i];
                                vars[widget.args[i]] = ss.str();
                            }
                        }
                        // Handle named args (if value is map)
                        else if (value.IsMap()) {
                            for (const auto& arg : value) {
                                std::stringstream ss;
                                ss << arg.second;
                                vars[arg.first.as<std::string>()] = ss.str();
                            }
                        }

                        // Substitute and parse widget body
                        std::string substituted = substituteVars(widget.body_yaml, vars);
                        YAML::Node widgetBody = YAML::Load(substituted);

                        if (widgetBody.IsSequence()) {
                            for (const auto& witem : widgetBody) {
                                if (!witem.IsMap()) continue;
                                for (const auto& wkv : witem) {
                                    std::string wkey = wkv.first.as<std::string>();
                                    if (wkey.substr(0, 2) == "sd") {
                                        auto prim = parsePrimitive(wkey, wkv.second);
                                        _primitives.push_back(prim);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    } catch (const YAML::Exception& e) {
        return Err<void>(std::string("YAML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Err<void>(std::string("Error: ") + e.what());
    }

    return Ok();
}

//-----------------------------------------------------------------------------
// SVG Parsing
//-----------------------------------------------------------------------------

// Helper: get attribute value from XML tag
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

// Helper: parse CSS/SVG color to RGBA
static bool parseSVGColor(const std::string& color, float out[4]) {
    if (color.empty() || color == "none") {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return true;
    }
    // Use existing parseColor
    return SDFPrimitivesLayer::parseColor(color, out);
}

// Helper: parse number from string, handling units
static float parseNumber(const std::string& s) {
    if (s.empty()) return 0.0f;
    try {
        return std::stof(s);
    } catch (...) {
        return 0.0f;
    }
}

// Helper: parse list of numbers (space or comma separated)
static std::vector<float> parseNumbers(const std::string& s) {
    std::vector<float> result;
    std::string num;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!num.empty()) {
                result.push_back(parseNumber(num));
                num.clear();
            }
        } else if (c == '-' && !num.empty() && num.back() != 'e' && num.back() != 'E') {
            // Negative sign starts new number
            result.push_back(parseNumber(num));
            num = "-";
        } else {
            num += c;
        }
    }
    if (!num.empty()) {
        result.push_back(parseNumber(num));
    }
    return result;
}

// SVG arc endpoint-to-center conversion
// Based on SVG spec: https://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter
struct ArcParams {
    float cx, cy;       // center
    float rx, ry;       // radii
    float theta1;       // start angle
    float dtheta;       // sweep angle
    float phi;          // rotation
};

static ArcParams svgArcEndpointToCenter(
    float x1, float y1,     // start point
    float x2, float y2,     // end point
    float rx, float ry,     // radii
    float phi,              // x-axis rotation (radians)
    bool largeArc,
    bool sweep
) {
    ArcParams result = {0, 0, rx, ry, 0, 0, phi};

    // Ensure radii are positive
    rx = std::abs(rx);
    ry = std::abs(ry);

    if (rx == 0 || ry == 0) {
        // Degenerate to line
        return result;
    }

    float cosPhi = std::cos(phi);
    float sinPhi = std::sin(phi);

    // Step 1: Compute (x1', y1')
    float dx = (x1 - x2) / 2.0f;
    float dy = (y1 - y2) / 2.0f;
    float x1p = cosPhi * dx + sinPhi * dy;
    float y1p = -sinPhi * dx + cosPhi * dy;

    // Step 2: Compute (cx', cy')
    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float rx2 = rx * rx;
    float ry2 = ry * ry;

    // Check if radii are large enough
    float lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float sqrtLambda = std::sqrt(lambda);
        rx *= sqrtLambda;
        ry *= sqrtLambda;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    float sign = (largeArc == sweep) ? -1.0f : 1.0f;
    float sq = (rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2) / (rx2 * y1p2 + ry2 * x1p2);
    sq = std::max(0.0f, sq);
    float coef = sign * std::sqrt(sq);
    float cxp = coef * rx * y1p / ry;
    float cyp = coef * -ry * x1p / rx;

    // Step 3: Compute (cx, cy) from (cx', cy')
    float cx = cosPhi * cxp - sinPhi * cyp + (x1 + x2) / 2.0f;
    float cy = sinPhi * cxp + cosPhi * cyp + (y1 + y2) / 2.0f;

    // Step 4: Compute theta1 and dtheta
    auto angle = [](float ux, float uy, float vx, float vy) -> float {
        float n = std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy);
        if (n == 0) return 0;
        float c = (ux * vx + uy * vy) / n;
        c = std::max(-1.0f, std::min(1.0f, c));
        float s = ux * vy - uy * vx;
        return std::atan2(s, c);
    };

    float theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                         (-x1p - cxp) / rx, (-y1p - cyp) / ry);

    if (!sweep && dtheta > 0) {
        dtheta -= 2.0f * 3.14159265f;
    } else if (sweep && dtheta < 0) {
        dtheta += 2.0f * 3.14159265f;
    }

    result.cx = cx;
    result.cy = cy;
    result.rx = rx;
    result.ry = ry;
    result.theta1 = theta1;
    result.dtheta = dtheta;
    result.phi = phi;

    return result;
}

// Parse SVG path d attribute
static void parseSVGPath(const std::string& d, std::vector<SDFPrimitiveGPU>& primitives,
                         const SDFStyle& style) {
    if (d.empty()) return;

    float curX = 0, curY = 0;      // Current position
    float startX = 0, startY = 0;  // Start of subpath (for Z command)
    float lastCX = 0, lastCY = 0;  // Last control point (for S/T commands)
    char lastCmd = 0;

    size_t i = 0;
    while (i < d.size()) {
        // Skip whitespace
        while (i < d.size() && (d[i] == ' ' || d[i] == '\t' || d[i] == '\n' || d[i] == '\r' || d[i] == ',')) i++;
        if (i >= d.size()) break;

        char cmd = d[i];
        bool relative = false;

        if (std::isalpha(cmd)) {
            relative = std::islower(cmd);
            cmd = std::toupper(cmd);
            i++;
        } else {
            // Implicit command - repeat last command
            cmd = lastCmd;
            relative = std::islower(lastCmd);
            if (cmd == 'M') cmd = 'L';  // After M, implicit is L
        }

        // Skip whitespace after command
        while (i < d.size() && (d[i] == ' ' || d[i] == '\t' || d[i] == '\n' || d[i] == '\r' || d[i] == ',')) i++;

        // Parse numbers for this command
        std::string numStr;
        while (i < d.size() && !std::isalpha(d[i])) {
            numStr += d[i];
            i++;
        }
        std::vector<float> nums = parseNumbers(numStr);

        size_t ni = 0;
        bool done = false;
        while (!done && (ni < nums.size() || cmd == 'Z')) {
            switch (cmd) {
                case 'M': {  // MoveTo
                    if (ni + 1 >= nums.size()) { ni = nums.size(); break; }
                    float x = nums[ni++];
                    float y = nums[ni++];
                    if (relative) { x += curX; y += curY; }
                    curX = startX = x;
                    curY = startY = y;
                    cmd = 'L';  // Subsequent coords are LineTo
                    break;
                }
                case 'L': {  // LineTo
                    if (ni + 1 >= nums.size()) { ni = nums.size(); break; }
                    float x = nums[ni++];
                    float y = nums[ni++];
                    if (relative) { x += curX; y += curY; }

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = x; prim.params[5] = y;
                    prim.style = style;
                    primitives.push_back(prim);

                    curX = x; curY = y;
                    break;
                }
                case 'H': {  // Horizontal LineTo
                    if (ni >= nums.size()) { ni = nums.size(); break; }
                    float x = nums[ni++];
                    if (relative) x += curX;

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = x; prim.params[5] = curY;
                    prim.style = style;
                    primitives.push_back(prim);

                    curX = x;
                    break;
                }
                case 'V': {  // Vertical LineTo
                    if (ni >= nums.size()) { ni = nums.size(); break; }
                    float y = nums[ni++];
                    if (relative) y += curY;

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = curX; prim.params[5] = y;
                    prim.style = style;
                    primitives.push_back(prim);

                    curY = y;
                    break;
                }
                case 'C': {  // Cubic Bezier
                    if (ni + 5 >= nums.size()) { ni = nums.size(); break; }
                    float x1 = nums[ni++], y1 = nums[ni++];
                    float x2 = nums[ni++], y2 = nums[ni++];
                    float x = nums[ni++], y = nums[ni++];
                    if (relative) {
                        x1 += curX; y1 += curY;
                        x2 += curX; y2 += curY;
                        x += curX; y += curY;
                    }

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::CubicBezier);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = x1; prim.params[5] = y1;
                    prim.params[6] = x2; prim.params[7] = y2;
                    prim.params[8] = x; prim.params[9] = y;
                    prim.style = style;
                    primitives.push_back(prim);

                    lastCX = x2; lastCY = y2;
                    curX = x; curY = y;
                    break;
                }
                case 'S': {  // Smooth Cubic Bezier
                    if (ni + 3 >= nums.size()) { ni = nums.size(); break; }
                    float x2 = nums[ni++], y2 = nums[ni++];
                    float x = nums[ni++], y = nums[ni++];
                    if (relative) {
                        x2 += curX; y2 += curY;
                        x += curX; y += curY;
                    }
                    // Reflect last control point
                    float x1 = 2 * curX - lastCX;
                    float y1 = 2 * curY - lastCY;

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::CubicBezier);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = x1; prim.params[5] = y1;
                    prim.params[6] = x2; prim.params[7] = y2;
                    prim.params[8] = x; prim.params[9] = y;
                    prim.style = style;
                    primitives.push_back(prim);

                    lastCX = x2; lastCY = y2;
                    curX = x; curY = y;
                    break;
                }
                case 'Q': {  // Quadratic Bezier
                    if (ni + 3 >= nums.size()) { ni = nums.size(); break; }
                    float x1 = nums[ni++], y1 = nums[ni++];
                    float x = nums[ni++], y = nums[ni++];
                    if (relative) {
                        x1 += curX; y1 += curY;
                        x += curX; y += curY;
                    }

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Bezier);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = x1; prim.params[5] = y1;
                    prim.params[6] = x; prim.params[7] = y;
                    prim.style = style;
                    primitives.push_back(prim);

                    lastCX = x1; lastCY = y1;
                    curX = x; curY = y;
                    break;
                }
                case 'T': {  // Smooth Quadratic Bezier
                    if (ni + 1 >= nums.size()) { ni = nums.size(); break; }
                    float x = nums[ni++], y = nums[ni++];
                    if (relative) { x += curX; y += curY; }
                    // Reflect last control point
                    float x1 = 2 * curX - lastCX;
                    float y1 = 2 * curY - lastCY;

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Bezier);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = curX; prim.params[3] = curY;
                    prim.params[4] = x1; prim.params[5] = y1;
                    prim.params[6] = x; prim.params[7] = y;
                    prim.style = style;
                    primitives.push_back(prim);

                    lastCX = x1; lastCY = y1;
                    curX = x; curY = y;
                    break;
                }
                case 'A': {  // Elliptical Arc
                    if (ni + 6 >= nums.size()) { ni = nums.size(); break; }
                    float rx = nums[ni++], ry = nums[ni++];
                    float xRot = nums[ni++] * 3.14159265f / 180.0f;
                    float largeArc = nums[ni++];
                    float sweep = nums[ni++];
                    float x = nums[ni++], y = nums[ni++];
                    if (relative) { x += curX; y += curY; }

                    // Skip degenerate arcs
                    if (rx == 0 || ry == 0 || (x == curX && y == curY)) {
                        curX = x; curY = y;
                        break;
                    }

                    // Convert to center parameterization
                    ArcParams arc = svgArcEndpointToCenter(
                        curX, curY, x, y, rx, ry, xRot,
                        largeArc > 0.5f, sweep > 0.5f);

                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::EllipseArc);
                    prim.params[0] = arc.cx; prim.params[1] = arc.cy;
                    prim.params[2] = arc.rx; prim.params[3] = arc.ry;
                    prim.params[4] = arc.phi;
                    prim.params[5] = arc.theta1;
                    prim.params[6] = arc.theta1 + arc.dtheta;
                    prim.style = style;
                    primitives.push_back(prim);

                    curX = x; curY = y;
                    break;
                }
                case 'Z': {  // ClosePath
                    if (curX != startX || curY != startY) {
                        SDFPrimitiveGPU prim = {};
                        prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                        prim.params[0] = 0; prim.params[1] = 0;
                        prim.params[2] = curX; prim.params[3] = curY;
                        prim.params[4] = startX; prim.params[5] = startY;
                        prim.style = style;
                        primitives.push_back(prim);
                    }
                    curX = startX; curY = startY;
                    done = true;  // Exit loop
                    break;
                }
                default:
                    ni = nums.size();  // Unknown command, skip
                    break;
            }
            lastCmd = cmd;
        }
    }
}

Result<void> SDFPrimitivesLayer::parseSVG(const std::string& svg) {
    try {
        // Simple XML tag parser
        size_t pos = 0;

        // Find viewBox for coordinate scaling (optional)
        float viewBoxX = 0, viewBoxY = 0, viewBoxW = 0, viewBoxH = 0;
        size_t svgStart = svg.find("<svg");
        if (svgStart != std::string::npos) {
            size_t svgEnd = svg.find(">", svgStart);
            if (svgEnd != std::string::npos) {
                std::string svgTag = svg.substr(svgStart, svgEnd - svgStart + 1);
                std::string viewBox = getAttr(svgTag, "viewBox");
                if (!viewBox.empty()) {
                    auto vb = parseNumbers(viewBox);
                    if (vb.size() >= 4) {
                        viewBoxX = vb[0]; viewBoxY = vb[1];
                        viewBoxW = vb[2]; viewBoxH = vb[3];
                    }
                }
            }
        }

        // Parse elements
        while ((pos = svg.find("<", pos)) != std::string::npos) {
            // Skip comments and processing instructions
            if (svg.substr(pos, 4) == "<!--" || svg.substr(pos, 2) == "<?") {
                pos++;
                continue;
            }

            size_t tagEnd = svg.find(">", pos);
            if (tagEnd == std::string::npos) break;

            std::string tag = svg.substr(pos, tagEnd - pos + 1);
            pos = tagEnd + 1;

            // Skip closing tags
            if (tag.size() > 1 && tag[1] == '/') continue;

            // Extract element name
            size_t nameStart = 1;
            size_t nameEnd = nameStart;
            while (nameEnd < tag.size() && !std::isspace(tag[nameEnd]) && tag[nameEnd] != '>' && tag[nameEnd] != '/') {
                nameEnd++;
            }
            std::string elemName = tag.substr(nameStart, nameEnd - nameStart);

            // Parse style attributes
            SDFStyle style = {};
            style.fill[0] = 0; style.fill[1] = 0; style.fill[2] = 0; style.fill[3] = 0;
            style.stroke[0] = 0; style.stroke[1] = 0; style.stroke[2] = 0; style.stroke[3] = 0;
            style.stroke_width = 0;

            std::string fillStr = getAttr(tag, "fill");
            std::string strokeStr = getAttr(tag, "stroke");
            std::string strokeWidthStr = getAttr(tag, "stroke-width");

            if (!fillStr.empty() && fillStr != "none") {
                parseSVGColor(fillStr, style.fill);
            }
            if (!strokeStr.empty() && strokeStr != "none") {
                parseSVGColor(strokeStr, style.stroke);
                style.stroke_width = strokeWidthStr.empty() ? 1.0f : parseNumber(strokeWidthStr);
            }

            // Handle each element type
            if (elemName == "circle") {
                float cx = parseNumber(getAttr(tag, "cx"));
                float cy = parseNumber(getAttr(tag, "cy"));
                float r = parseNumber(getAttr(tag, "r"));

                SDFPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(SDFPrimitiveType::Circle);
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

                SDFPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(SDFPrimitiveType::Ellipse);
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
                float ry = parseNumber(getAttr(tag, "ry"));

                // Box is centered, so offset by half size
                SDFPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(SDFPrimitiveType::Box);
                prim.params[0] = x + w / 2; prim.params[1] = y + h / 2;
                prim.params[2] = w / 2; prim.params[3] = h / 2;
                prim.style = style;
                prim.style.round = std::max(rx, ry);
                _primitives.push_back(prim);
            }
            else if (elemName == "line") {
                float x1 = parseNumber(getAttr(tag, "x1"));
                float y1 = parseNumber(getAttr(tag, "y1"));
                float x2 = parseNumber(getAttr(tag, "x2"));
                float y2 = parseNumber(getAttr(tag, "y2"));

                SDFPrimitiveGPU prim = {};
                prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                prim.params[0] = 0; prim.params[1] = 0;
                prim.params[2] = x1; prim.params[3] = y1;
                prim.params[4] = x2; prim.params[5] = y2;
                prim.style = style;
                _primitives.push_back(prim);
            }
            else if (elemName == "polyline" || elemName == "polygon") {
                std::string pointsStr = getAttr(tag, "points");
                auto nums = parseNumbers(pointsStr);

                for (size_t i = 0; i + 3 < nums.size(); i += 2) {
                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = nums[i]; prim.params[3] = nums[i + 1];
                    prim.params[4] = nums[i + 2]; prim.params[5] = nums[i + 3];
                    prim.style = style;
                    _primitives.push_back(prim);
                }
                // Close polygon
                if (elemName == "polygon" && nums.size() >= 4) {
                    SDFPrimitiveGPU prim = {};
                    prim.type = static_cast<uint32_t>(SDFPrimitiveType::Segment);
                    prim.params[0] = 0; prim.params[1] = 0;
                    prim.params[2] = nums[nums.size() - 2]; prim.params[3] = nums[nums.size() - 1];
                    prim.params[4] = nums[0]; prim.params[5] = nums[1];
                    prim.style = style;
                    _primitives.push_back(prim);
                }
            }
            else if (elemName == "path") {
                std::string d = getAttr(tag, "d");
                parseSVGPath(d, _primitives, style);
            }
        }

        spdlog::info("SVG parsed: {} primitives", _primitives.size());
    } catch (const std::exception& e) {
        return Err<void>(std::string("SVG parse error: ") + e.what());
    }

    return Ok();
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

Result<void> SDFPrimitivesLayer::render(WebGPUContext& ctx,
                                 WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float pixelX, float pixelY, float pixelW, float pixelH) {
    if (_failed) return Err<void>("SDFPrimitivesLayer already failed");

    if (_primitives.empty()) return Ok();

    if (!_gpu_initialized) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to create pipeline", result);
        }
        _gpu_initialized = true;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("SDFPrimitivesLayer pipeline not initialized");
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

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
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.time = _time;
    uniforms.num_primitives = static_cast<float>(_primitives.size());

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    // Update primitives buffer
    if (_primitives_dirty && !_primitives.empty()) {
        wgpuQueueWriteBuffer(ctx.getQueue(), _primitive_buffer, 0,
                             _primitives.data(), _primitives.size() * sizeof(SDFPrimitiveGPU));
        _primitives_dirty = false;
    }

    // Render
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("Failed to create command encoder");

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (cmdBuffer) {
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
    }
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

Result<void> SDFPrimitivesLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Uniform buffer (32 bytes)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 32;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

    // Primitive storage buffer
    WGPUBufferDescriptor primBufDesc = {};
    primBufDesc.size = MAX_PRIMITIVES * sizeof(SDFPrimitiveGPU);
    primBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    _primitive_buffer = wgpuDeviceCreateBuffer(device, &primBufDesc);
    if (!_primitive_buffer) return Err<void>("Failed to create primitive buffer");

    // Shader
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(SDF_SHADER);
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
    bgE[1].size = MAX_PRIMITIVES * sizeof(SDFPrimitiveGPU);

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

    spdlog::info("SDFPrimitivesLayer: pipeline created");
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "sdf-primitives"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) { return yetty::SDFPrimitivesPlugin::create(std::move(engine)); }
}
