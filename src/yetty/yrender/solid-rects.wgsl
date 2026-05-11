/* solid-rects.wgsl — SDF-shaded axis-aligned rounded rectangles for UI
 * chrome (Chrome-style tab strip, focus outlines, badges, dividers).
 *
 * Why SDF: pixel-aligned rounded corners with sub-pixel antialiasing at any
 * size, no separate texture or font atlas needed. Each instance carries its
 * own four corner radii — set them all to 0 for a sharp axis-aligned rect.
 *
 * Layout (must match struct yetty_yrender_solid_rect in C, 48 B total):
 *   pos    : vec2 px  (instance top-left in target pixel space)
 *   size   : vec2 px
 *   color  : vec4 rgba in [0..1]
 *   radii  : vec4 px  (top-left, top-right, bottom-right, bottom-left)
 *
 * y grows downward in pixel space; the vertex shader flips for NDC.
 */

struct InstanceIn {
    @location(0) pos: vec2<f32>,
    @location(1) size: vec2<f32>,
    @location(2) color: vec4<f32>,
    @location(3) radii: vec4<f32>,
};

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    /* Position relative to the rect center, in pixel space. The SDF in the
     * fragment shader does its math in this local frame so rect rotation /
     * pan never has to be undone. */
    @location(1) local_px: vec2<f32>,
    @location(2) half_size: vec2<f32>,
    @location(3) radii: vec4<f32>,
};

struct Uniforms {
    target_size: vec2<f32>,
    _pad: vec2<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vid: u32, inst: InstanceIn) -> VertexOut {
    var corners = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 1.0),
    );
    let c = corners[vid];
    let px = inst.pos + c * inst.size;
    let ts = max(uniforms.target_size, vec2<f32>(1.0, 1.0));
    let ndc = vec2<f32>(
        px.x / ts.x * 2.0 - 1.0,
        1.0 - px.y / ts.y * 2.0,
    );

    let half_size = inst.size * 0.5;
    /* local frame: (-half_size .. half_size), origin at the rect center. */
    let local = c * inst.size - half_size;

    var out: VertexOut;
    out.position = vec4<f32>(ndc, 0.0, 1.0);
    out.color = inst.color;
    out.local_px = local;
    out.half_size = half_size;
    out.radii = inst.radii;
    return out;
}

/* Signed distance to an axis-aligned rounded box with per-corner radii.
 * p is in the rect's local frame (center at origin). b is the half-extent.
 * r packs the four radii in (top-left, top-right, bottom-right, bottom-left)
 * order — same as CSS `border-radius`. Negative inside, positive outside,
 * zero on the boundary; the fragment shader uses fwidth() to antialias. */
fn sd_round_box(p: vec2<f32>, b: vec2<f32>, r: vec4<f32>) -> f32 {
    /* Pick the radius for the quadrant p lives in. */
    var rr: vec2<f32>;
    if (p.x > 0.0) {
        rr = select(vec2<f32>(r.z, r.z), vec2<f32>(r.y, r.y), p.y < 0.0);
    } else {
        rr = select(vec2<f32>(r.w, r.w), vec2<f32>(r.x, r.x), p.y < 0.0);
    }
    let radius = rr.x;
    let q = abs(p) - b + vec2<f32>(radius, radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0, 0.0))) - radius;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    /* radii=0 → sharp rect, the SDF still evaluates to the box SDF and the
     * smoothstep yields alpha=1 inside / 0 outside. One uniform code path.
     * For rounded cells: fwidth gives the screen-space derivative of d, so
     * AA stays a consistent ~1px band even at zoom. */
    let d = sd_round_box(in.local_px, in.half_size, in.radii);
    let aa = max(fwidth(d), 0.5);
    let alpha = 1.0 - smoothstep(-aa, aa, d);
    return vec4<f32>(in.color.rgb, in.color.a * alpha);
}
