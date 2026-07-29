// grid-text.wgsl — the yvterm text-grid shader: one full-screen quad that
// paints the grid.c cell model (16 bytes/cell) with MSDF/raster font atlases,
// cursor/selection inversion, underline/strikethrough, the visual zoom and
// the OSC-driven post/coord effects.
//
// Staged to <shaders_dir> as a yshaders module shader. vterm.c loads it in
// vterm_gpu_init and prepends effects-lib.wgsl (or a no-op stub) before
// compiling — fx_post_apply / fx_coord_apply resolve from that prelude, so
// this file never compiles alone. The asset is mandatory: when it is missing
// the vterm pipeline fails hard and the terminal renders no text, so every
// packaging path must ship it (yshaders/CMakeLists.txt MODULE_SHADERS,
// platform/shared.cmake yetty_embed_assets, webasm-stage-assets.cmake).

struct Uniforms {
    grid_size: vec2<f32>,
    cell_size: vec2<f32>,
    scale: f32,
    baseline_y: f32,
    glyph_left: f32,
    pixel_range: f32,
    root_row: u32,
    cursor_col: u32,
    cursor_row: u32,
    cursor_visible: u32,
    sel_active: u32,
    sel_start_row: u32,
    sel_start_col: u32,
    sel_end_row: u32,
    sel_end_col: u32,
    ring_rows: u32,
    visual_zoom_scale: f32,
    visual_zoom_offset_x: f32,
    visual_zoom_offset_y: f32,
    time: f32, mouse_x: f32, mouse_y: f32,
    post_fx_index: u32,
    post_fx_p0: f32, post_fx_p1: f32, post_fx_p2: f32,
    post_fx_p3: f32, post_fx_p4: f32, post_fx_p5: f32,
    coord_fx_index: u32,
    coord_fx_p0: f32, coord_fx_p1: f32, coord_fx_p2: f32,
    coord_fx_p3: f32, coord_fx_p4: f32, coord_fx_p5: f32,
    pad_a: u32, pad_b: u32,
    face_methods: u32, face_pad0: u32, face_pad1: u32, face_pad2: u32,
    face_params: array<vec4<f32>, 4>,
};
@group(0) @binding(0) var<storage, read> cells: array<u32>;
@group(0) @binding(1) var<storage, read> glyph_meta: array<u32>;
@group(0) @binding(2) var atlas_tex: texture_2d<f32>;
@group(0) @binding(3) var atlas_smp: sampler;
@group(0) @binding(4) var<uniform> uni: Uniforms;
// Extra font faces (config range faces). Unused slots are bound to the
// face-0 resources, and face_methods routes decoding, so the shader is
// compiled once regardless of how many faces the config declares.
@group(0) @binding(5) var<storage, read> face1_meta: array<u32>;
@group(0) @binding(6) var face1_tex: texture_2d<f32>;
@group(0) @binding(7) var<storage, read> face2_meta: array<u32>;
@group(0) @binding(8) var face2_tex: texture_2d<f32>;
@group(0) @binding(9) var<storage, read> face3_meta: array<u32>;
@group(0) @binding(10) var face3_tex: texture_2d<f32>;
struct VSOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,
};
@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VSOut {
    var corners = array<vec2<f32>, 6>(
        vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,-1.0), vec2<f32>(1.0,1.0),
        vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,1.0), vec2<f32>(-1.0,1.0));
    let ndc = corners[vid];
    var out: VSOut;
    out.pos = vec4<f32>(ndc, 0.0, 1.0);
    let grid_w = uni.grid_size.x * uni.cell_size.x;
    let grid_h = uni.grid_size.y * uni.cell_size.y;
    out.grid_pixel = vec2<f32>((ndc.x*0.5+0.5)*grid_w, (0.5-ndc.y*0.5)*grid_h);
    return out;
}
fn median3(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r,g), min(max(r,g), b));
}
fn face_meta(face: u32, index: u32) -> u32 {
    switch face {
        case 1u: { return face1_meta[index]; }
        case 2u: { return face2_meta[index]; }
        case 3u: { return face3_meta[index]; }
        default: { return glyph_meta[index]; }
    }
}
fn face_texel(face: u32, uv: vec2<f32>) -> vec4<f32> {
    switch face {
        case 1u: { return textureSampleLevel(face1_tex, atlas_smp, uv, 0.0); }
        case 2u: { return textureSampleLevel(face2_tex, atlas_smp, uv, 0.0); }
        case 3u: { return textureSampleLevel(face3_tex, atlas_smp, uv, 0.0); }
        default: { return textureSampleLevel(atlas_tex, atlas_smp, uv, 0.0); }
    }
}
fn face_atlas_size(face: u32) -> vec2<f32> {
    switch face {
        case 1u: { return vec2<f32>(textureDimensions(face1_tex)); }
        case 2u: { return vec2<f32>(textureDimensions(face2_tex)); }
        case 3u: { return vec2<f32>(textureDimensions(face3_tex)); }
        default: { return vec2<f32>(textureDimensions(atlas_tex)); }
    }
}
// Raster faces: 4-word meta (uv origin + slot width in cells), glyphs
// pre-rasterized at cell size. Returns the raw texel: R8 coverage
// lands in .r, color (RGBA8 emoji) texels come through whole.
fn sample_raster_texel(face: u32, glyph: u32, local_px: vec2<f32>) -> vec4<f32> {
    let base = glyph * 4u;
    let uv0 = vec2<f32>(bitcast<f32>(face_meta(face, base+0u)), bitcast<f32>(face_meta(face, base+1u)));
    if (uv0.x < 0.0) { return vec4<f32>(0.0); }
    let width_cells = max(bitcast<f32>(face_meta(face, base+2u)), 1.0);
    let slot_px = vec2<f32>(uni.cell_size.x * width_cells, uni.cell_size.y);
    if (local_px.x < 0.0 || local_px.y < 0.0 || local_px.x >= slot_px.x || local_px.y >= slot_px.y) { return vec4<f32>(0.0); }
    let uv = uv0 + local_px / face_atlas_size(face);
    return face_texel(face, uv);
}
// One MSDF coverage tap: median of the three channels, mapped to an
// alpha ramp screen_px_range screen-pixels steep around the 0.5
// iso-line.
fn msdf_coverage(face: u32, uv: vec2<f32>, screen_px_range: f32) -> f32 {
    let texel = face_texel(face, uv);
    let sd = median3(texel.r, texel.g, texel.b);
    return clamp((sd - 0.5) * screen_px_range + 0.5, 0.0, 1.0);
}
// MSDF faces: 10-word glyph meta (uv_min, uv_max, size, bearing,
// advance, pad), face_params = (pixel_range, scale, baseline_y,
// glyph_left).
fn sample_face_glyph(face: u32, glyph: u32, local_px: vec2<f32>) -> f32 {
    let method = (uni.face_methods >> (face * 4u)) & 0xFu;
    if (method == 1u) {
        return sample_raster_texel(face, glyph, local_px).r;
    }
    let params = uni.face_params[face];
    let base = glyph * 10u;
    let uv_min = vec2<f32>(bitcast<f32>(face_meta(face, base+0u)), bitcast<f32>(face_meta(face, base+1u)));
    let uv_max = vec2<f32>(bitcast<f32>(face_meta(face, base+2u)), bitcast<f32>(face_meta(face, base+3u)));
    let gsize = vec2<f32>(bitcast<f32>(face_meta(face, base+4u)), bitcast<f32>(face_meta(face, base+5u)));
    let bear = vec2<f32>(bitcast<f32>(face_meta(face, base+6u)), bitcast<f32>(face_meta(face, base+7u)));
    if (gsize.x <= 0.0 || gsize.y <= 0.0) { return 0.0; }
    let scaled_size = gsize * params.y;
    let scaled_bear = bear * params.y;
    let gtop = params.z - scaled_bear.y;
    let gleft = params.w + scaled_bear.x;
    let gmin = vec2<f32>(gleft, gtop);
    let gmax = vec2<f32>(gleft + scaled_size.x, gtop + scaled_size.y);
    if (local_px.x < gmin.x || local_px.x >= gmax.x || local_px.y < gmin.y || local_px.y >= gmax.y) { return 0.0; }
    let gl = (local_px - gmin) / scaled_size;
    let uv = mix(uv_min, uv_max, gl);
    // AA width in SCREEN pixels: field range in atlas texels (params.x)
    // x grid px per texel (params.y) x screen px per grid px (the
    // visual zoom). Without the zoom factor the ramp is 1 grid px, so a
    // zoomed-in glyph edge smears across `zoom` screen pixels. Clamped
    // so deep minification never drops the ramp below one screen px.
    let zoom = max(uni.visual_zoom_scale, 0.0001);
    let screen_px_range = max(params.x * params.y * zoom, 1.0);
    let texels_per_screen_px = 1.0 / (params.y * zoom);
    if (texels_per_screen_px < 1.25) {
        return msdf_coverage(face, uv, screen_px_range);
    }
    // Minified: one screen pixel spans >1.25 atlas texels, and a single
    // bilinear tap under-resolves the field (stroke-weight wobble,
    // nicked corners). Box-filter instead: 2x2 taps at +-0.25 screen px,
    // each with a half-pixel ramp, averaged.
    let tap_uv = (uv_max - uv_min) / scaled_size * (0.25 / zoom);
    let tap_range = screen_px_range * 2.0;
    var coverage = msdf_coverage(face, uv + vec2<f32>(-tap_uv.x, -tap_uv.y), tap_range);
    coverage += msdf_coverage(face, uv + vec2<f32>(tap_uv.x, -tap_uv.y), tap_range);
    coverage += msdf_coverage(face, uv + vec2<f32>(-tap_uv.x, tap_uv.y), tap_range);
    coverage += msdf_coverage(face, uv + vec2<f32>(tap_uv.x, tap_uv.y), tap_range);
    return coverage * 0.25;
}
@fragment
fn fs_main(in: VSOut) -> @location(0) vec4<f32> {
    let grid_w = uni.grid_size.x * uni.cell_size.x;
    let grid_h = uni.grid_size.y * uni.cell_size.y;
    // Invert the visual-zoom transform to find the grid pixel under this
    // fragment. This is the canonical mouse-anchored zoom shared with the
    // figure shaders and the zoom controller:
    //     source = (screen - center)/scale + center + offset
    // where offset is a SOURCE-space pan (so a drag makes content follow the
    // cursor) and center is the pane centre. Identity at scale 1 / offset 0.
    // Text and figures use the same formula, so they zoom, pan and stay
    // anchored to the same row together.
    let vz = max(uni.visual_zoom_scale, 0.0001);
    let center = vec2<f32>(grid_w, grid_h) * 0.5;
    let voff = vec2<f32>(uni.visual_zoom_offset_x, uni.visual_zoom_offset_y);
    var px = (in.grid_pixel - center) / vz + center + voff;
    // Pointer positions in pane pixels: mouse (falls back to cursor) and
    // the terminal cursor cell centre.
    let fx_cursor = vec2<f32>((f32(uni.cursor_col) + 0.5) * uni.cell_size.x,
                              (f32(uni.cursor_row) + 0.5) * uni.cell_size.y);
    let fx_mouse = vec2<f32>(uni.mouse_x, uni.mouse_y);
    // Apply coordinate distortion after the zoom inversion.
    px = fx_coord_apply(uni.coord_fx_index, px, vec2<f32>(grid_w, grid_h),
                        uni.time, fx_mouse, fx_cursor,
                        uni.coord_fx_p0, uni.coord_fx_p1, uni.coord_fx_p2,
                        uni.coord_fx_p3, uni.coord_fx_p4, uni.coord_fx_p5);
    if (px.x < 0.0 || px.y < 0.0 || px.x >= grid_w || px.y >= grid_h) {
        return vec4<f32>(0.0,0.0,0.0,1.0);
    }
    let colf = floor(px.x / uni.cell_size.x);
    let rowf = floor(px.y / uni.cell_size.y);
    let col = u32(colf);
    let row = u32(rowf);
    let gcols = u32(uni.grid_size.x);
    let slot = (row + uni.root_row) % uni.ring_rows;
    let cell_index = slot * gcols + col;
    var local = vec2<f32>(px.x - colf*uni.cell_size.x, px.y - rowf*uni.cell_size.y);
    var glyph = cells[cell_index*4u + 0u];
    let w1 = cells[cell_index*4u + 1u];
    let w2 = cells[cell_index*4u + 2u];
    let attrs = (w2 >> 16u) & 0xFFFFu;
    let fg = vec3<f32>(f32(w1 & 0xFFu)/255.0, f32((w1>>8u)&0xFFu)/255.0, f32((w1>>16u)&0xFFu)/255.0);
    let bg = vec3<f32>(f32((w1>>24u)&0xFFu)/255.0, f32(w2 & 0xFFu)/255.0, f32((w2>>8u)&0xFFu)/255.0);
    // A wide glyph occupies two cells: the head (width 2) holds the glyph,
    // the spill cell (width 0) to its right is blank. Continue sampling the
    // head glyph into the spill cell, shifted one cell to the right, so the
    // right half of CJK/double-width glyphs is drawn.
    var face = (cells[cell_index*4u + 3u] >> 8u) & 0xFFu;
    if ((cells[cell_index*4u + 3u] & 0xFFu) == 0u && col > 0u) {
        let head = slot * gcols + (col - 1u);
        if ((cells[head*4u + 3u] & 0xFFu) == 2u) {
            glyph = cells[head*4u + 0u];
            face = (cells[head*4u + 3u] >> 8u) & 0xFFu;
            local.x = local.x + uni.cell_size.x;
        }
    }
    var alpha = 0.0;
    var glyph_rgb = vec3<f32>(0.0);
    var glyph_is_color = false;
    // 0xFFFFFFFF = notdef sentinel: a codepoint no face could supply. Draw a
    // hollow box inset from the cell edges (fg-tinted) so a missing glyph is
    // a visible tofu, never a blank cell.
    if (glyph == 0xFFFFFFFFu) {
        let inx = uni.cell_size.x * 0.16;
        let iny = uni.cell_size.y * 0.12;
        let th = max(1.0, uni.cell_size.x * 0.07);
        let x0 = inx; let x1 = uni.cell_size.x - inx;
        let y0 = iny; let y1 = uni.cell_size.y - iny;
        let on_box = local.x >= x0 && local.x <= x1 && local.y >= y0 && local.y <= y1;
        let in_hole = local.x >= x0 + th && local.x <= x1 - th &&
                      local.y >= y0 + th && local.y <= y1 - th;
        if (on_box && !in_hole) { alpha = 1.0; }
    } else if (glyph != 0u) {
        let cell_method = (uni.face_methods >> (face * 4u)) & 0xFu;
        if (cell_method == 2u) {
            let texel = sample_raster_texel(face, glyph, local);
            glyph_rgb = texel.rgb;
            alpha = texel.a;
            glyph_is_color = true;
        } else {
            alpha = sample_face_glyph(face, glyph, local);
        }
    }
    // Underline (single 0x2 or double 0x4) sits just below the baseline;
    // strikethrough (0x40) crosses the cell middle. Both paint at full
    // coverage so they show on blank cells too.
    let line_h = max(1.0, uni.cell_size.y * 0.07);
    let ul_y = uni.baseline_y + max(1.0, uni.cell_size.y * 0.10);
    if ((attrs & 0x6u) != 0u && local.y >= ul_y && local.y < ul_y + line_h) { alpha = 1.0; }
    let st_y = uni.cell_size.y * 0.5;
    if ((attrs & 0x40u) != 0u && local.y >= st_y && local.y < st_y + line_h) { alpha = 1.0; }
    let is_cursor = uni.cursor_visible != 0u && col == uni.cursor_col && row == uni.cursor_row;
    // Selection highlight (reading-order stream, start <= end). Inverted
    // like the cursor — the xterm default look.
    var selected = false;
    if (uni.sel_active != 0u) {
        if (row > uni.sel_start_row && row < uni.sel_end_row) { selected = true; }
        else if (row == uni.sel_start_row && row == uni.sel_end_row) {
            selected = col >= uni.sel_start_col && col <= uni.sel_end_col;
        } else if (row == uni.sel_start_row) { selected = col >= uni.sel_start_col; }
        else if (row == uni.sel_end_row) { selected = col <= uni.sel_end_col; }
    }
    var composed = mix(bg, fg, alpha);
    if (glyph_is_color) {
        // Pre-colored emoji texel — no fg tint.
        composed = mix(bg, glyph_rgb, alpha);
    }
    if (is_cursor || selected) {
        // Inverted: fg fill, glyph punched in bg.
        composed = mix(fg, bg, alpha);
    }
    // Post-color effect over the opaque terminal surface. Shared clock in
    // uni.time keeps animation in phase with every other shader.
    if (uni.post_fx_index != 0u) {
        let screen = uni.grid_size * uni.cell_size;
        composed = fx_post_apply(uni.post_fx_index, composed, in.grid_pixel, screen,
            uni.time, fx_mouse, fx_cursor, uni.cell_size,
            uni.post_fx_p0, uni.post_fx_p1, uni.post_fx_p2,
            uni.post_fx_p3, uni.post_fx_p4, uni.post_fx_p5);
    }
    return vec4<f32>(composed, 1.0);
}
