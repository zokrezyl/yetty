// =============================================================================
// YDraw Layer Shader - SDF Primitive Rendering
// =============================================================================
// Complete standalone shader that renders ydraw primitives to a texture.
// This layer is rendered separately, then composited with text-layer.
//
// Generated constants (prepended by binder):
//   uniforms.ydraw_ydraw_grid_size
//   uniforms.ydraw_ydraw_cell_size
//   uniforms.ydraw_ydraw_rolling_row_0
//   uniforms.ydraw_ydraw_drawable_count
//   uniforms.ydraw_grid_offset
//   uniforms.ydraw_prims_offset

// RENDER_LAYER_BINDINGS_PLACEHOLDER

// SDF functions + evaluate_sdf_2d() dispatcher come from the GENERATED
// src/yetty/ysdf/ysdf.gen.wgsl — prepended to this file by ydraw-layer.c
// at shader-load time. Don't hand-add SDF cases here; update the .yaml and
// regenerate.

// GLYPH is ydraw's own primitive (not SDF), kept here next to its reader fns.
const YDRAW_SDF_GLYPH: u32 = 200u;

// =============================================================================
// Vertex Shader
// =============================================================================
struct VertexInput {
    @location(0) position: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    // See text-layer.wgsl for the rationale: @builtin(position) in fragment
    // is the framebuffer pixel, which doesn't match the grid origin once
    // the pane viewport sits at offset (vp.x, vp.y) != (0,0). Map the NDC
    // quad onto the grid's pixel area in the vertex shader so cell lookup
    // is independent of where the pane sits in the big surface.
    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.position, 0.0, 1.0);

    // Map the rect's NDC quad onto the on-screen view (rect) size in px.
    // The fragment inverse-applies the visual zoom and buckets/bounds
    // against grid_size*cell_size (the content extent). When content ==
    // rect, view == content and this is identical to mapping the whole
    // content onto the rect (the non-scrolling case).
    let view = uniforms.ydraw_ydraw_view_size;
    output.grid_pixel = vec2<f32>(
        (input.position.x * 0.5 + 0.5) * view.x,
        (0.5 - input.position.y * 0.5) * view.y
    );
    return output;
}

// =============================================================================
// Primitive Buffer Layout (serialized with a 3-word header prepended):
//   [0] rolling_row   - absolute row number of primitive's line
//   [1] offset_x      - f32 projection offset (accumulated group offsets)
//   [2] offset_y      - f32 projection offset
//   [3] type          - primitive type for dispatch
//   [4] z_order       - rendering order
//   [5] fill_color    - packed RGBA
//   [6] stroke_color  - packed RGBA
//   [7] stroke_width  - f32
//   [8+] geometry     - primitive-specific args (Y coords relative to line)
// =============================================================================

fn ydraw_read_rolling_row(drawable_offset: u32) -> u32 {
    return storage_buffer[drawable_offset + 0u];
}

fn ydraw_read_offset(drawable_offset: u32) -> vec2<f32> {
    return vec2<f32>(bitcast<f32>(storage_buffer[drawable_offset + 1u]),
                     bitcast<f32>(storage_buffer[drawable_offset + 2u]));
}

// Accumulated ancestor clip rect (block-content px, offsets applied);
// w <= 0 disables clipping.
fn ydraw_read_clip(drawable_offset: u32) -> vec4<f32> {
    return vec4<f32>(bitcast<f32>(storage_buffer[drawable_offset + 3u]),
                     bitcast<f32>(storage_buffer[drawable_offset + 4u]),
                     bitcast<f32>(storage_buffer[drawable_offset + 5u]),
                     bitcast<f32>(storage_buffer[drawable_offset + 6u]));
}

fn ydraw_read_drawable_type(drawable_offset: u32) -> u32 {
    return storage_buffer[drawable_offset + 7u];
}

fn ydraw_read_fill_color(drawable_offset: u32) -> u32 {
    return storage_buffer[drawable_offset + 9u];
}

fn ydraw_read_stroke_color(drawable_offset: u32) -> u32 {
    return storage_buffer[drawable_offset + 10u];
}

fn ydraw_read_stroke_width(drawable_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[drawable_offset + 11u]);
}

fn ydraw_read_geom_f32(drawable_offset: u32, idx: u32) -> f32 {
    return bitcast<f32>(storage_buffer[drawable_offset + 12u + idx]);
}

// =============================================================================
// Glyph Primitive Layout (different from SDF; same 3-word header):
//   [0] rolling_row
//   [1] offset_x    - f32 projection offset
//   [2] offset_y    - f32 projection offset
//   [3] type (200 = GLYPH)
//   [4] z_order
//   [5] x           - glyph position (with bearing applied)
//   [6] y           - glyph position (with bearing applied)
//   [7] font_size   - target render size (scale = font_size / base_size)
//   [8] packed      - glyph_index (low 16) | font_id (high 16)
//   [9] color       - packed RGBA
// =============================================================================

fn glyph_read_x(drawable_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[drawable_offset + 9u]);
}

fn glyph_read_y(drawable_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[drawable_offset + 10u]);
}

fn glyph_read_font_size(drawable_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[drawable_offset + 11u]);
}

fn glyph_read_packed(drawable_offset: u32) -> u32 {
    return storage_buffer[drawable_offset + 12u];
}

fn glyph_read_color(drawable_offset: u32) -> u32 {
    return storage_buffer[drawable_offset + 13u];
}

// The active fonts' shaders (msdf-font.wgsl × N) are merged by the binder.
// Each instance contributes its own helpers — `<ns>_base_size`, `<ns>_glyph_size`,
// `<ns>_glyph_sample` — namespaced by the binder's __NS__ substitution.
//
// ydraw-layer.c emits a per-canvas dispatcher block ABOVE this static
// source that exposes:
//   font_base_size(slot)            -> f32
//   font_glyph_size(slot, i)        -> vec2<f32>
//   font_glyph_sample(slot, i, uv, ps) -> f32
// The dispatcher switches on `slot` and forwards to the right `<ns>_…`
// helper. Slot 0 is the canvas's default font; 1..N are PDF-embedded
// fonts in canvas->all_fonts order.

// SDF evaluation: call the generated evaluate_sdf_2d() (from ysdf.gen.wgsl).
// The ydraw prim stores rolling_row at word +0 and the raw ysdf record
// from the word after the per-prim header (3 words: rolling_row, offset_x,
// offset_y + clip rect), so pass drawable_offset + 7u.

// Unpack RGBA color from u32
fn ydraw_unpack_color(packed: u32) -> vec4<f32> {
    return vec4<f32>(
        f32(packed & 0xFFu) / 255.0,
        f32((packed >> 8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0,
        f32((packed >> 24u) & 0xFFu) / 255.0
    );
}

// =============================================================================
// Fragment Shader
// =============================================================================
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let drawable_count = uniforms.ydraw_ydraw_drawable_count;
    let grid_size = uniforms.ydraw_ydraw_grid_size;
    let cell_size = uniforms.ydraw_ydraw_cell_size;

    let grid_width = u32(grid_size.x);
    let grid_height = u32(grid_size.y);

    // Early exit if no primitives
    if (drawable_count == 0u || grid_width == 0u || grid_height == 0u) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);  // Fully transparent
    }

    let grid_offset = uniforms.ydraw_grid_offset;
    let prims_offset = uniforms.ydraw_prims_offset;

    // Grid pixel bounds
    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;

    // Visual zoom (Ctrl+Scroll, mouse-anchored, around pane center) is the
    // only GLOBAL transform: it applies identically to terminal glyphs and
    // rich content, matching the text/figure shaders. Structural cell zoom
    // (Ctrl+Shift+Scroll) is NOT global — row anchors already move with
    // the CURRENT cell size, so cell zoom belongs exclusively to the
    // rich-primitive LOCAL scale below (dividing the whole pane coordinate
    // by it scaled the row anchors a second time and sampled prims far
    // from their bucketed cells at any nonzero row).
    let vz_scale = uniforms.ydraw_ydraw_visual_zoom_scale;
    let vz_off   = uniforms.ydraw_ydraw_visual_zoom_off;
    let cz_scale = uniforms.ydraw_ydraw_cell_zoom_scale;
    let vz_center = vec2<f32>(grid_pixel_w * 0.5, grid_pixel_h * 0.5);
    let pixel_pos = (input.grid_pixel - vz_center) / max(vz_scale, 0.0001)
                  + vz_center + vz_off;

    // Outside grid = transparent
    if (pixel_pos.x < 0.0 || pixel_pos.y < 0.0 ||
        pixel_pos.x >= grid_pixel_w || pixel_pos.y >= grid_pixel_h) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    // Scene position = pixel position (1:1 mapping)
    let scene_pos = pixel_pos;

    // Grid lookup: find which cell we're in
    let cell_x = u32(clamp(pixel_pos.x / cell_size.x, 0.0, f32(grid_width - 1u)));
    let cell_y = u32(clamp(pixel_pos.y / cell_size.y, 0.0, f32(grid_height - 1u)));
    let cell_index = cell_y * grid_width + cell_x;

    // Read cell's primitive list from grid
    let cell_start = storage_buffer[grid_offset + cell_index];
    let cell_count = storage_buffer[grid_offset + cell_start];
    // Safety cap. 64 proved far too low for map-like content: a measured
    // dense-city 32 px cell holds ~800 prims (fill triangles + cased
    // roads + dash segments + label glyphs), and prims past the cap —
    // labels first, since they are appended last — silently vanish.
    // grid.c logs "max prims/cell" at staging rebuild; keep this above it.
    let loop_count = min(cell_count, 1024u);

    // Premultiplied-alpha accumulators: starting from vec3(0),
    // `mix(result_color, rgb, alpha)` keeps result_color premultiplied
    // (each contribution enters as rgb * alpha), and result_alpha composes
    // src-over. The final return must NOT multiply by alpha again — that
    // squares the alpha of every translucent pixel and crushes AA fringes
    // and thin strokes to near-black.
    var result_color = vec3<f32>(0.0);
    var result_alpha = 0.0;

    // Active plan run: only prims with index in [run_first, run_first +
    // run_count) composite in this draw. Prims are staged in paint-plan
    // order, so a run is a contiguous index interval and interleaving
    // ranged draws with complex draws yields the total paint order.
    let run_first = uniforms.ydraw_ydraw_run_first;
    let run_count = uniforms.ydraw_ydraw_run_count;

    for (var i = 0u; i < loop_count; i++) {
        let raw_idx = storage_buffer[grid_offset + cell_start + 1u + i];
        if (raw_idx < run_first || raw_idx >= run_first + run_count) {
            continue;
        }

        // raw_idx is a primitive index (0, 1, 2...), not a data offset
        // Prim staging layout: [offset_table...][drawable_data...]
        // Read data offset from offset table, then compute actual position
        let data_offset = storage_buffer[prims_offset + raw_idx];
        let drawable_offset = prims_offset + drawable_count + data_offset;

        // Compute primitive's screen Y offset from its rolling_row
        // Use signed arithmetic to handle scrolling past the primitive
        let rolling_row = ydraw_read_rolling_row(drawable_offset);
        let rolling_row_0 = uniforms.ydraw_ydraw_rolling_row_0;
        let y_offset = f32(i32(rolling_row) - i32(rolling_row_0)) * cell_size.y;

        let drawable_type = ydraw_read_drawable_type(drawable_offset);

        // Transform pixel position to primitive-local coords. The row
        // anchor (y_offset, CURRENT cell pixels) stays in framebuffer
        // space; only the primitive-LOCAL space scales. RICH prims are
        // producer-logical x density x structural cell zoom; the base
        // range below ydraw_rich_first is shaped TERMINAL text staged at
        // current framebuffer cell metrics — it follows the text grid
        // exactly once and receives NO conversion (scale 1).
        let density = max(uniforms.ydraw_ydraw_density_scale, 0.0001);
        let rich_prim = raw_idx >= uniforms.ydraw_ydraw_rich_first;
        let local_scale = select(1.0, max(density * cz_scale, 0.0001), rich_prim);
        let content_pos = vec2<f32>(pixel_pos.x, pixel_pos.y - y_offset) / local_scale;
        let prim_offset = ydraw_read_offset(drawable_offset);
        let local_pos = content_pos - prim_offset;

        // Ancestor clip (block-content space = pixel with the block anchor
        // removed but BEFORE the group offset): outside → this prim
        // contributes nothing at this pixel. clip.z <= 0 = unclipped.
        let prim_clip = ydraw_read_clip(drawable_offset);
        if (prim_clip.z > 0.0) {
            if (content_pos.x < prim_clip.x || content_pos.y < prim_clip.y ||
                content_pos.x >= prim_clip.x + prim_clip.z ||
                content_pos.y >= prim_clip.y + prim_clip.w) {
                continue;
            }
        }


        // Glyph primitives — delegate atlas sampling to the active font
        // backend via font_glyph_sample() / font_glyph_size(). Bearing has
        // already been applied on the CPU, so (glyph_x, glyph_y) is the
        // top-left corner of the rendered glyph rectangle.
        if (drawable_type == YDRAW_SDF_GLYPH) {
            let glyph_x = glyph_read_x(drawable_offset);
            let glyph_y = glyph_read_y(drawable_offset);
            let font_size = glyph_read_font_size(drawable_offset);
            let packed = glyph_read_packed(drawable_offset);
            let glyph_index = packed & 0xFFFFu;
            // High 16 bits hold (canvas_slot + 1). 0 means "default font".
            let slot_plus_one = (packed >> 16u) & 0xFFFFu;
            let font_slot = select(0u, slot_plus_one - 1u, slot_plus_one > 0u);
            let color_packed = glyph_read_color(drawable_offset);

            let base_size = font_base_size(font_slot);
            let pixel_scale = select(1.0, font_size / base_size, base_size > 0.0);

            let glyph_size = font_glyph_size(font_slot, glyph_index);
            if (glyph_size.x <= 0.0 || glyph_size.y <= 0.0) {
                continue;
            }

            let glyph_min = vec2<f32>(glyph_x, glyph_y);
            let glyph_max = glyph_min + glyph_size * pixel_scale;
            if (local_pos.x < glyph_min.x || local_pos.x >= glyph_max.x ||
                local_pos.y < glyph_min.y || local_pos.y >= glyph_max.y) {
                continue;
            }

            // The sampler's AA sharpness follows the on-screen scale:
            // local pixel_scale times the prim's local-space multiplier.
            let glyph_uv = (local_pos - glyph_min) / (glyph_size * pixel_scale);
            let glyph_alpha =
                font_glyph_sample(font_slot, glyph_index, glyph_uv, pixel_scale * local_scale);

            if (glyph_alpha > 0.0) {
                let glyph_rgba = ydraw_unpack_color(color_packed);
                let alpha = glyph_alpha * glyph_rgba.a;
                result_color = mix(result_color, glyph_rgba.rgb, alpha);
                result_alpha = alpha + result_alpha * (1.0 - alpha);
            }
            continue;
        }

        // Evaluate SDF for non-glyph primitives. The evaluation runs in
        // the prim's local space; multiplying the distance by local_scale
        // puts it back in framebuffer pixels, so the one-pixel AA ramp
        // and the stroke bands below stay screen-accurate at any density
        // and any cell zoom.
        let d = evaluate_sdf_2d(drawable_offset + 7u, local_pos) * local_scale;

        // Resolve the fill color. Gradient primitives compute their color
        // from per-pixel position; everything else reads the single
        // packed fill_color word.
        let drawable_type_for_color = ydraw_read_drawable_type(drawable_offset);
        var fill_rgba: vec4<f32>;
        var has_fill: bool;
        if (yetty_ysdf_is_gradient_2d(drawable_type_for_color)) {
            fill_rgba = yetty_ysdf_eval_gradient_color_2d(drawable_offset + 7u, local_pos);
            has_fill = fill_rgba.a > 0.0;
        } else {
            let fill_color = ydraw_read_fill_color(drawable_offset);
            fill_rgba = ydraw_unpack_color(fill_color);
            has_fill = fill_color != 0u;
        }

        // Analytic AA: scene == pixel space here, so a fixed one-pixel
        // coverage ramp (half a pixel to each side of the boundary) equals
        // screen-space fwidth AA — which WGSL forbids inside this
        // non-uniform loop.
        // Coverage is gamma-corrected (^(1/2.2)) because compositing happens
        // on sRGB-encoded values: without it a 50%-covered fringe displays at
        // ~21% perceived brightness and thin light-on-dark lines look pinched
        // and ropey. Exact over a black background, close enough elsewhere.
        if (d < 0.5 && has_fill) {
            let coverage = clamp(0.5 - d, 0.0, 1.0);
            let alpha = pow(coverage, 1.0 / 2.2) * fill_rgba.a;
            result_color = mix(result_color, fill_rgba.rgb, alpha);
            result_alpha = alpha + result_alpha * (1.0 - alpha);
        }

        // Render stroke. Strokes thinner than 1px draw as a 1px band dimmed
        // by the requested width, so hairlines stay visible (and uniform)
        // at any subpixel position. Rich stroke widths are producer-local —
        // scale to framebuffer pixels alongside the distance.
        let stroke_color = ydraw_read_stroke_color(drawable_offset);
        let stroke_width = ydraw_read_stroke_width(drawable_offset) * local_scale;
        if (stroke_width > 0.0 && stroke_color != 0u) {
            let effective_stroke_width = max(stroke_width, 1.0);
            let stroke_dist = abs(d) - effective_stroke_width * 0.5;
            if (stroke_dist < 0.5) {
                let stroke_rgba = ydraw_unpack_color(stroke_color);
                let coverage = clamp(0.5 - stroke_dist, 0.0, 1.0) * min(stroke_width, 1.0);
                let alpha = pow(coverage, 1.0 / 2.2) * stroke_rgba.a;
                result_color = mix(result_color, stroke_rgba.rgb, alpha);
                result_alpha = alpha + result_alpha * (1.0 - alpha);
            }
        }
    }

    // result_color is already premultiplied (see accumulator comment above);
    // the pipeline blends with (One, OneMinusSrcAlpha).
    return vec4<f32>(result_color, result_alpha);
}
