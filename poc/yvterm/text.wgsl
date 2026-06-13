struct Uniforms {
    grid_size: vec2<f32>,
    cell_size: vec2<f32>,
    scale: f32,
    baseline_y: f32,
    glyph_left: f32,
    pixel_range: f32,
    root_row: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
};

@group(0) @binding(0) var<storage, read> cells: array<u32>;
@group(0) @binding(1) var<storage, read> glyph_meta: array<u32>;
@group(0) @binding(2) var atlas_tex: texture_2d<f32>;
@group(0) @binding(3) var atlas_smp: sampler;
@group(0) @binding(4) var<uniform> uni: Uniforms;

struct VSOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VSOut {
    var corners = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0),
        vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, 1.0), vec2<f32>(-1.0, 1.0)
    );
    let ndc = corners[vid];
    var out: VSOut;
    out.pos = vec4<f32>(ndc, 0.0, 1.0);
    let grid_w = uni.grid_size.x * uni.cell_size.x;
    let grid_h = uni.grid_size.y * uni.cell_size.y;
    out.grid_pixel = vec2<f32>((ndc.x * 0.5 + 0.5) * grid_w, (0.5 - ndc.y * 0.5) * grid_h);
    return out;
}

fn median3(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

fn sample_glyph(glyph: u32, local_px: vec2<f32>) -> f32 {
    let base = glyph * 10u;
    let uv_min = vec2<f32>(bitcast<f32>(glyph_meta[base + 0u]), bitcast<f32>(glyph_meta[base + 1u]));
    let uv_max = vec2<f32>(bitcast<f32>(glyph_meta[base + 2u]), bitcast<f32>(glyph_meta[base + 3u]));
    let gsize = vec2<f32>(bitcast<f32>(glyph_meta[base + 4u]), bitcast<f32>(glyph_meta[base + 5u]));
    let bear = vec2<f32>(bitcast<f32>(glyph_meta[base + 6u]), bitcast<f32>(glyph_meta[base + 7u]));
    if (gsize.x <= 0.0 || gsize.y <= 0.0) { return 0.0; }
    let scaled_size = gsize * uni.scale;
    let scaled_bear = bear * uni.scale;
    let gtop = uni.baseline_y - scaled_bear.y;
    let gleft = uni.glyph_left + scaled_bear.x;
    let gmin = vec2<f32>(gleft, gtop);
    let gmax = vec2<f32>(gleft + scaled_size.x, gtop + scaled_size.y);
    if (local_px.x < gmin.x || local_px.x >= gmax.x || local_px.y < gmin.y || local_px.y >= gmax.y) { return 0.0; }
    let gl = (local_px - gmin) / scaled_size;
    let uv = mix(uv_min, uv_max, gl);
    let texel = textureSampleLevel(atlas_tex, atlas_smp, uv, 0.0);
    let sd = median3(texel.r, texel.g, texel.b);
    let screen_px_range = uni.pixel_range * uni.scale;
    return clamp((sd - 0.5) * screen_px_range + 0.5, 0.0, 1.0);
}

@fragment
fn fs_main(in: VSOut) -> @location(0) vec4<f32> {
    let grid_w = uni.grid_size.x * uni.cell_size.x;
    let grid_h = uni.grid_size.y * uni.cell_size.y;
    let px = in.grid_pixel;
    if (px.x < 0.0 || px.y < 0.0 || px.x >= grid_w || px.y >= grid_h) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let col = floor(px.x / uni.cell_size.x);
    let row = floor(px.y / uni.cell_size.y);
    let slot = (u32(row) + uni.root_row) % u32(uni.grid_size.y);
    let cell_index = slot * u32(uni.grid_size.x) + u32(col);
    let local = vec2<f32>(px.x - col * uni.cell_size.x, px.y - row * uni.cell_size.y);
    let glyph = cells[cell_index * 4u + 0u];
    let w1 = cells[cell_index * 4u + 1u];
    let w2 = cells[cell_index * 4u + 2u];
    let fg = vec3<f32>(f32(w1 & 0xFFu) / 255.0, f32((w1 >> 8u) & 0xFFu) / 255.0, f32((w1 >> 16u) & 0xFFu) / 255.0);
    let bg = vec3<f32>(f32((w1 >> 24u) & 0xFFu) / 255.0, f32(w2 & 0xFFu) / 255.0, f32((w2 >> 8u) & 0xFFu) / 255.0);
    var alpha = 0.0;
    if (glyph != 0u) { alpha = sample_glyph(glyph, local); }
    let composed = mix(bg, fg, alpha);
    return vec4<f32>(composed, 1.0);
}
