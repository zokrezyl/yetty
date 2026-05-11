/* tabbar-chrome.wgsl — SDF-shaded rounded (optionally rotated) rectangles
 * for the yui tabbar chrome: strip background, tab cells, "+" new-tab
 * button, plus the upcoming minimize / maximize / close glyphs.
 *
 * Why SDF: pixel-aligned rounded corners with sub-pixel antialiasing at any
 * size, no separate texture or font atlas needed. Each instance carries
 * its own four corner radii (CSS order: tl, tr, br, bl) — set them all
 * to 0 for a sharp rect. A per-instance rotation lets diagonal glyphs
 * like the X close button share the same pipeline as the axis-aligned
 * tab cells, instead of needing a second shader.
 *
 * Layout (must match struct yetty_yui_tabbar_chrome_rect in C, 52 B):
 *   pos      : vec2 px  (un-rotated rect's top-left in target pixel space)
 *   size     : vec2 px  (un-rotated dimensions)
 *   color    : vec4 rgba in [0..1]
 *   radii    : vec4 px  (top-left, top-right, bottom-right, bottom-left)
 *   rotation : f32  radians, around the rect's center
 *
 * y grows downward in pixel space; the vertex shader flips for NDC.
 */

struct InstanceIn {
    @location(0) pos: vec2<f32>,
    @location(1) size: vec2<f32>,
    @location(2) color: vec4<f32>,
    @location(3) radii: vec4<f32>,
    @location(4) rotation: f32,
};

struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    /* Position relative to the rect's *rotated* bounding-box center, in
     * pixel space. The fragment shader rotates this back into the rect's
     * own frame before evaluating the SDF. */
    @location(1) local_px: vec2<f32>,
    @location(2) half_size: vec2<f32>,
    @location(3) radii: vec4<f32>,
    @location(4) rotation: f32,
};

struct Uniforms {
    target_size: vec2<f32>,
    _pad: vec2<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vid: u32, inst: InstanceIn) -> VertexOut {
    /* For rotated rects the emitted quad has to be the axis-aligned
     * bounding box of the rotated shape, otherwise the corners would be
     * clipped to the un-rotated quad and we'd see a parallelogram-shaped
     * cut-off. cos+sin of |rotation| give the bbox dimensions. */
    let cr = abs(cos(inst.rotation));
    let sr = abs(sin(inst.rotation));
    let bbox = vec2<f32>(inst.size.x * cr + inst.size.y * sr,
                         inst.size.x * sr + inst.size.y * cr);

    /* The rotated rect's center sits at the un-rotated rect's center
     * (rotation pivots around that point), so the bbox is centered there
     * too. */
    let center = inst.pos + inst.size * 0.5;
    let bbox_topleft = center - bbox * 0.5;

    var corners = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 1.0),
    );
    let c = corners[vid];
    let px = bbox_topleft + c * bbox;
    let ts = max(uniforms.target_size, vec2<f32>(1.0, 1.0));
    let ndc = vec2<f32>(
        px.x / ts.x * 2.0 - 1.0,
        1.0 - px.y / ts.y * 2.0,
    );

    var out: VertexOut;
    out.position = vec4<f32>(ndc, 0.0, 1.0);
    out.color = inst.color;
    out.local_px = c * bbox - bbox * 0.5;
    out.half_size = inst.size * 0.5;
    out.radii = inst.radii;
    out.rotation = inst.rotation;
    return out;
}

/* Signed distance to an axis-aligned rounded box with per-corner radii.
 * p is in the rect's local frame (center at origin). b is the half-extent.
 * r packs the four radii in (top-left, top-right, bottom-right, bottom-left)
 * order — same as CSS `border-radius`. Negative inside, positive outside,
 * zero on the boundary; the fragment shader uses fwidth() to antialias. */
fn sd_round_box(p: vec2<f32>, b: vec2<f32>, r: vec4<f32>) -> f32 {
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
    /* Undo the rotation so the SDF can evaluate the rect in its own
     * (axis-aligned) frame. For rotation=0 the cos/sin reduce to (1,0)
     * and the multiply is a no-op — same code path serves rotated and
     * un-rotated rects. */
    let c = cos(-in.rotation);
    let s = sin(-in.rotation);
    let p = vec2<f32>(in.local_px.x * c - in.local_px.y * s,
                      in.local_px.x * s + in.local_px.y * c);

    let d = sd_round_box(p, in.half_size, in.radii);
    let aa = max(fwidth(d), 0.5);
    let alpha = 1.0 - smoothstep(-aa, aa, d);
    return vec4<f32>(in.color.rgb, in.color.a * alpha);
}
