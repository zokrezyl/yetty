// ymesh 3D pass — render an indexed triangle mesh into the per-instance
// offscreen color+depth target. Lambert lighting, one fixed directional
// light. Vertex layout: positions @location(0), normals @location(1).

struct Uniforms {
    mvp: mat4x4<f32>,           // projection * view * model
    model: mat4x4<f32>,         // model only (for world-space normals)
    normal_matrix: mat4x4<f32>, // transpose(inverse(model))
    light_dir: vec4<f32>,       // world-space, .xyz; .w unused
    base_color: vec4<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VsIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
};

struct VsOut {
    @builtin(position) clip_pos: vec4<f32>,
    @location(0) world_normal: vec3<f32>,
};

@vertex
fn vs_main(in: VsIn) -> VsOut {
    var out: VsOut;
    out.clip_pos = uniforms.mvp * vec4<f32>(in.position, 1.0);
    let n = (uniforms.normal_matrix * vec4<f32>(in.normal, 0.0)).xyz;
    out.world_normal = n;
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let n = normalize(in.world_normal);
    let l = normalize(uniforms.light_dir.xyz);
    let ndotl = max(dot(n, l), 0.0);
    // Ambient + Lambert diffuse.
    let lit = uniforms.base_color.rgb * (0.2 + 0.8 * ndotl);
    return vec4<f32>(lit, uniforms.base_color.a);
}
