// ymesh blit pass — fullscreen-quad-style blit of the offscreen color
// texture into the layer's render target, clipped to the bounds rect.

struct BlitUniforms {
    bounds: vec4<f32>,    // x, y, w, h in pane pixels (post-scroll origin)
    viewport: vec2<f32>,  // pane viewport size in pixels
    _pad: vec2<f32>,
};

@group(0) @binding(0) var<uniform> u: BlitUniforms;
@group(0) @binding(1) var src_tex: texture_2d<f32>;
@group(0) @binding(2) var src_smp: sampler;

struct VsOut {
    @builtin(position) position: vec4<f32>,
    @location(0) clip_xy: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VsOut {
    var p = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );
    var out: VsOut;
    out.position = vec4<f32>(p[vi], 0.0, 1.0);
    out.clip_xy = p[vi];
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let pane_px = in.position.xy;
    let local = pane_px - u.bounds.xy;
    if (local.x < 0.0 || local.y < 0.0 ||
        local.x >= u.bounds.z || local.y >= u.bounds.w) {
        discard;
    }
    let uv = local / u.bounds.zw;
    return textureSample(src_tex, src_smp, uv);
}
