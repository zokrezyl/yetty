// yvideo Complex Primitive Shader
// Renders the decoded frame texture into the prim's AABB. Same
// fullscreen-triangle + zoom/viewport math as yimage; only the
// sampling target differs.
//
// Bindings supplied by the binder (auto-bound from the resource set):
//   uniforms.yvideo_bounds_x/y/w/h, video_w/h
//   uniforms.yvideo_fps, color_matrix, flags          (unused by shader v1)
//   uniforms.yvideo_visual_zoom_*, cell_zoom_*, viewport_*
//   uniforms.yvideo_frame_region: vec4<f32>           (atlas UV rect)
//   atlas_rgba8_texture / atlas_rgba8_sampler

fn yvideo_render(local_pos: vec2<f32>) -> vec4<f32> {
    let bounds_w = yvideo_get_bounds_w();
    let bounds_h = yvideo_get_bounds_h();

    let local_uv = local_pos / vec2<f32>(bounds_w, bounds_h);
    let region = uniforms.yvideo_frame_region;
    let atlas_uv = mix(region.xy, region.zw, local_uv);

    return textureSample(atlas_rgba8_texture, atlas_rgba8_sampler, atlas_uv);
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    // Fullscreen triangle covers entire framebuffer.
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
    // Same pane-px → source-px transform as yimage so zoom behaves
    // consistently across all complex prims.
    let vz_scale = uniforms.yvideo_visual_zoom_scale;
    let vz_off   = vec2<f32>(uniforms.yvideo_visual_zoom_off_x,
                             uniforms.yvideo_visual_zoom_off_y);
    let cz_scale = uniforms.yvideo_cell_zoom_scale;
    let cz_off   = vec2<f32>(uniforms.yvideo_cell_zoom_off_x,
                             uniforms.yvideo_cell_zoom_off_y);
    let vp       = vec2<f32>(uniforms.yvideo_viewport_w,
                             uniforms.yvideo_viewport_h);
    let vp_c     = vp * 0.5;

    let pane_px = in.position.xy;
    let after_visual = (pane_px - vp_c) / max(vz_scale, 0.0001) + vp_c + vz_off;
    let source_px    = after_visual / max(cz_scale, 0.0001) + cz_off;

    let bounds_x = yvideo_get_bounds_x();
    let bounds_y = yvideo_get_bounds_y();
    let bounds_w = yvideo_get_bounds_w();
    let bounds_h = yvideo_get_bounds_h();

    let local_pos = source_px - vec2<f32>(bounds_x, bounds_y);
    if (local_pos.x < 0.0 || local_pos.y < 0.0 ||
        local_pos.x >= bounds_w || local_pos.y >= bounds_h) {
        discard;
    }

    return yvideo_render(local_pos);
}
