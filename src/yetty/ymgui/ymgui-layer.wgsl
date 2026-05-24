// =============================================================================
// ymgui Layer Shader — native ImGui pipeline
// =============================================================================
// Vertex layout matches Dear ImGui's ImDrawVert exactly (20 bytes):
//   @location(0) pos: vec2<f32>   pixel coords (frame-local, Y-down)
//   @location(1) uv:  vec2<f32>   atlas UV
//   @location(2) col: vec4<f32>   from Unorm8x4 → 0..1 (RGBA)
//
// Bindings (group 0):
//   binding 0 — uniforms (display_size, frame_top)
//   binding 1 — atlas texture (R8Unorm; .r used as alpha multiplier)
//   binding 2 — atlas sampler
//
// Vertex shader:
//   1. shifts pos by frame_top so the frame can scroll with the terminal
//      without the layer re-uploading geometry — same model as ydraw's
//      rolling-row trick, just applied as a single uniform translation
//   2. converts to NDC: WebGPU clip space is Y-up, ImGui Y-down → flip Y
//
// Fragment shader: vert_col * vec4(1, 1, 1, atlas.r). Standard ImGui shader
// for an Alpha8 atlas — backgrounds sample the atlas's "white pixel" so
// .r=1 and the multiply reduces to vert_col, glyphs sample anti-aliased
// alpha coverage.
//
// This shader is loaded from disk by ymgui-layer.c (paths/shaders/ymgui-layer.wgsl)
// and used to compile a single render pipeline that is cached for the
// life of the layer — no per-frame recompilation.
// =============================================================================

struct Uniforms {
    display_size: vec2<f32>,
    frame_top:    vec2<f32>,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var atlas:   texture_2d<f32>;
@group(0) @binding(2) var atlas_s: sampler;

struct VsIn {
    @location(0) pos: vec2<f32>,
    @location(1) uv:  vec2<f32>,
    @location(2) col: vec4<f32>,
};

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv:  vec2<f32>,
    @location(1) col: vec4<f32>,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    let p = in.pos + u.frame_top;
    let nx = p.x / max(u.display_size.x, 1.0) * 2.0 - 1.0;
    let ny = 1.0 - p.y / max(u.display_size.y, 1.0) * 2.0;
    var o: VsOut;
    o.pos = vec4<f32>(nx, ny, 0.0, 1.0);
    o.uv  = in.uv;
    o.col = in.col;
    return o;
}

@fragment
fn fs_main(i: VsOut) -> @location(0) vec4<f32> {
    let s = textureSample(atlas, atlas_s, i.uv).r;
    return vec4<f32>(i.col.rgb, i.col.a * s);
}
