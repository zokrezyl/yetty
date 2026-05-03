// yimage Complex Primitive Shader
// Renders a rectangular RGBA8 image, atlas-sampled with hardware bilinear.
//
// Bindings supplied by the binder (auto-generated):
//   uniforms.yimage_bounds_x/y/w/h, image_w/h
//   atlas_rgba8_texture / atlas_rgba8_sampler
//   uniforms.yimage_image_region: vec4<f32>  (atlas UV rect: u0,v0,u1,v1
//       — written by the binder at finalize after atlas pack; defaults
//       to the identity (0,0,1,1) before resolution)
//   uniforms.yimage_visual_zoom_*, yimage_cell_zoom_*, yimage_viewport_*
//
// Generated accessors in yimage-gen.wgsl provide type-safe uniform access.

fn yimage_render(local_pos: vec2<f32>) -> vec4<f32> {
    let bounds_w = yimage_get_bounds_w();
    let bounds_h = yimage_get_bounds_h();

    // Normalise to 0..1 inside the image rect, then remap to the
    // texture's slice of the atlas.
    let local_uv = local_pos / vec2<f32>(bounds_w, bounds_h);
    let region = uniforms.yimage_image_region;
    let atlas_uv = mix(region.xy, region.zw, local_uv);

    return textureSample(atlas_rgba8_texture, atlas_rgba8_sampler, atlas_uv);
}

// Vertex/Fragment entry points for standalone pipeline
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    // Fullscreen triangle - 3 vertices cover entire screen
    var pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    var uv: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 1.0),
        vec2<f32>(2.0, 1.0),
        vec2<f32>(0.0, -1.0)
    );

    var out: VertexOutput;
    out.position = vec4<f32>(pos[vertex_index], 0.0, 1.0);
    out.uv = uv[vertex_index];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Compose visual + cell zoom transforms from pane pixel to source
    // pixel — same shape as yplot/yshaders so primitives behave
    // consistently under Ctrl+Scroll / Ctrl+Shift+Scroll.
    let vz_scale = uniforms.yimage_visual_zoom_scale;
    let vz_off   = vec2<f32>(uniforms.yimage_visual_zoom_off_x,
                             uniforms.yimage_visual_zoom_off_y);
    let cz_scale = uniforms.yimage_cell_zoom_scale;
    let cz_off   = vec2<f32>(uniforms.yimage_cell_zoom_off_x,
                             uniforms.yimage_cell_zoom_off_y);
    let vp       = vec2<f32>(uniforms.yimage_viewport_w,
                             uniforms.yimage_viewport_h);
    let vp_c     = vp * 0.5;

    let pane_px = in.position.xy;
    let after_visual = (pane_px - vp_c) / max(vz_scale, 0.0001) + vp_c + vz_off;
    let source_px    = after_visual / max(cz_scale, 0.0001) + cz_off;

    let bounds_x = yimage_get_bounds_x();
    let bounds_y = yimage_get_bounds_y();
    let bounds_w = yimage_get_bounds_w();
    let bounds_h = yimage_get_bounds_h();

    // Cull fragments outside the image's rect.
    let local_pos = source_px - vec2<f32>(bounds_x, bounds_y);
    if (local_pos.x < 0.0 || local_pos.y < 0.0 ||
        local_pos.x >= bounds_w || local_pos.y >= bounds_h) {
        discard;
    }

    return yimage_render(local_pos);
}
