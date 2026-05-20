// yvideo Complex Primitive Shader — v2 in-shader YUV→RGB.
//
// Samples three R8 plane textures (Y at full res, U+V at chroma 4:2:0
// half res) from the shared R8 atlas, then runs the BT.601 / BT.709
// matrix multiply driven by the color_matrix uniform. The pre-v2
// CPU YUV→BGRA path is gone — each plane is uploaded verbatim from
// openh264's output.
//
// Bindings supplied by the binder (auto-bound from the resource set):
//   uniforms.yvideo_bounds_x/y/w/h, video_w/h, chroma_w/h
//   uniforms.yvideo_fps, color_matrix, flags (unused in shader)
//   uniforms.yvideo_audio_* (unused — audio path is host-only)
//   uniforms.yvideo_visual_zoom_*, cell_zoom_*, viewport_*
//   uniforms.yvideo_y_plane_region: vec4<f32> (atlas UV rect for Y)
//   uniforms.yvideo_u_plane_region: vec4<f32> (atlas UV rect for U)
//   uniforms.yvideo_v_plane_region: vec4<f32> (atlas UV rect for V)
//   atlas_r8_texture / atlas_r8_sampler  (shared R8 atlas)

fn yvideo_yuv_to_rgb(y: f32, u: f32, v: f32) -> vec3<f32> {
    // Limited-range (TV) 8-bit YUV decode. Y in [16/255, 235/255],
    // U/V in [16/255, 240/255] centered at 128/255. We normalise into
    // full-range linear-ish [0,1] floats then apply the matrix.
    //
    //   Yn = (Y - 16/255) * (255 / 219)
    //   Un = (U - 128/255) * (255 / 224)
    //   Vn = (V - 128/255) * (255 / 224)
    //
    // Coefficients (Kb, Kr):
    //   BT.601:  0.114, 0.299
    //   BT.709:  0.0722, 0.2126
    //   BT.2020: 0.0593, 0.2627
    let yn = (y - 16.0 / 255.0) * (255.0 / 219.0);
    let un = (u - 128.0 / 255.0) * (255.0 / 224.0);
    let vn = (v - 128.0 / 255.0) * (255.0 / 224.0);

    let cm = yvideo_get_color_matrix();
    var kb: f32 = 0.0722;     // BT.709 default
    var kr: f32 = 0.2126;
    if (cm == 0u) {           // BT.601
        kb = 0.114;
        kr = 0.299;
    } else if (cm == 2u) {    // BT.2020
        kb = 0.0593;
        kr = 0.2627;
    }
    let kg = 1.0 - kb - kr;

    // Derived from the (Y, Cb, Cr) → (R, G, B) inverse with the chosen Kb/Kr.
    let r = yn + 2.0 * (1.0 - kr) * vn;
    let g = yn - (2.0 * kb * (1.0 - kb) / kg) * un
               - (2.0 * kr * (1.0 - kr) / kg) * vn;
    let b = yn + 2.0 * (1.0 - kb) * un;
    return clamp(vec3<f32>(r, g, b), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn yvideo_render(local_pos: vec2<f32>) -> vec4<f32> {
    let bounds_w = yvideo_get_bounds_w();
    let bounds_h = yvideo_get_bounds_h();

    let local_uv = local_pos / vec2<f32>(bounds_w, bounds_h);

    let y_region = uniforms.yvideo_y_plane_region;
    let u_region = uniforms.yvideo_u_plane_region;
    let v_region = uniforms.yvideo_v_plane_region;
    let y_uv = mix(y_region.xy, y_region.zw, local_uv);
    let u_uv = mix(u_region.xy, u_region.zw, local_uv);
    let v_uv = mix(v_region.xy, v_region.zw, local_uv);

    // Three samples (one per plane). Linear filter on U/V gives the
    // implicit chroma upsample from half-res to full-res for free.
    let y = textureSample(atlas_r8_texture, atlas_r8_sampler, y_uv).r;
    let u = textureSample(atlas_r8_texture, atlas_r8_sampler, u_uv).r;
    let v = textureSample(atlas_r8_texture, atlas_r8_sampler, v_uv).r;

    let rgb = yvideo_yuv_to_rgb(y, u, v);
    return vec4<f32>(rgb, 1.0);
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
