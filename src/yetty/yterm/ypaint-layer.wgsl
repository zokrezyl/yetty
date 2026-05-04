// =============================================================================
// YPaint Layer Shader - SDF Primitive Rendering
// =============================================================================
// Complete standalone shader that renders ypaint primitives to a texture.
// This layer is rendered separately, then composited with text-layer.
//
// Generated constants (prepended by binder):
//   uniforms.ypaint_ypaint_grid_size
//   uniforms.ypaint_ypaint_cell_size
//   uniforms.ypaint_ypaint_rolling_row_0
//   uniforms.ypaint_ypaint_prim_count
//   ypaint_grid_offset
//   ypaint_prims_offset

// RENDER_LAYER_BINDINGS_PLACEHOLDER

// SDF functions + evaluate_sdf_2d() dispatcher come from the GENERATED
// src/yetty/ysdf/ysdf.gen.wgsl — prepended to this file by ypaint-layer.c
// at shader-load time. Don't hand-add SDF cases here; update the .yaml and
// regenerate.

// GLYPH is ypaint's own primitive (not SDF), kept here next to its reader fns.
const YPAINT_SDF_GLYPH: u32 = 200u;

// =============================================================================
// Vertex Shader
// =============================================================================
struct VertexInput {
    @location(0) position: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.position, 0.0, 1.0);
    return output;
}

// =============================================================================
// Primitive Buffer Layout (serialized with rolling_row prepended):
//   [0] rolling_row   - absolute row number of primitive's line
//   [1] type          - primitive type for dispatch
//   [2] z_order       - rendering order
//   [3] fill_color    - packed RGBA
//   [4] stroke_color  - packed RGBA
//   [5] stroke_width  - f32
//   [6+] geometry     - primitive-specific args (Y coords relative to line)
// =============================================================================

fn ypaint_read_rolling_row(prim_offset: u32) -> u32 {
    return storage_buffer[prim_offset + 0u];
}

fn ypaint_read_prim_type(prim_offset: u32) -> u32 {
    return storage_buffer[prim_offset + 1u];
}

fn ypaint_read_fill_color(prim_offset: u32) -> u32 {
    return storage_buffer[prim_offset + 3u];
}

fn ypaint_read_stroke_color(prim_offset: u32) -> u32 {
    return storage_buffer[prim_offset + 4u];
}

fn ypaint_read_stroke_width(prim_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[prim_offset + 5u]);
}

fn ypaint_read_geom_f32(prim_offset: u32, idx: u32) -> f32 {
    return bitcast<f32>(storage_buffer[prim_offset + 6u + idx]);
}

// =============================================================================
// Glyph Primitive Layout (different from SDF):
//   [0] rolling_row
//   [1] type (200 = GLYPH)
//   [2] z_order
//   [3] x           - glyph position (with bearing applied)
//   [4] y           - glyph position (with bearing applied)
//   [5] font_size   - target render size (scale = font_size / base_size)
//   [6] packed      - glyph_index (low 16) | font_id (high 16)
//   [7] color       - packed RGBA
// =============================================================================

fn glyph_read_x(prim_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[prim_offset + 3u]);
}

fn glyph_read_y(prim_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[prim_offset + 4u]);
}

fn glyph_read_font_size(prim_offset: u32) -> f32 {
    return bitcast<f32>(storage_buffer[prim_offset + 5u]);
}

fn glyph_read_packed(prim_offset: u32) -> u32 {
    return storage_buffer[prim_offset + 6u];
}

fn glyph_read_color(prim_offset: u32) -> u32 {
    return storage_buffer[prim_offset + 7u];
}

// The active fonts' shaders (msdf-font.wgsl × N) are merged by the binder.
// Each instance contributes its own helpers — `<ns>_base_size`, `<ns>_glyph_size`,
// `<ns>_glyph_sample` — namespaced by the binder's __NS__ substitution.
//
// ypaint-layer.c emits a per-canvas dispatcher block ABOVE this static
// source that exposes:
//   font_base_size(slot)            -> f32
//   font_glyph_size(slot, i)        -> vec2<f32>
//   font_glyph_sample(slot, i, uv, ps) -> f32
// The dispatcher switches on `slot` and forwards to the right `<ns>_…`
// helper. Slot 0 is the canvas's default font; 1..N are PDF-embedded
// fonts in canvas->all_fonts order.

// SDF evaluation: call the generated evaluate_sdf_2d() (from ysdf.gen.wgsl).
// The ypaint prim stores rolling_row at word +0 and the raw ysdf record
// from +1 onward, so pass prim_offset + 1u.

// Unpack RGBA color from u32
fn ypaint_unpack_color(packed: u32) -> vec4<f32> {
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
    let prim_count = uniforms.ypaint_ypaint_prim_count;
    let grid_size = uniforms.ypaint_ypaint_grid_size;
    let cell_size = uniforms.ypaint_ypaint_cell_size;

    let grid_width = u32(grid_size.x);
    let grid_height = u32(grid_size.y);

    // Early exit if no primitives
    if (prim_count == 0u || grid_width == 0u || grid_height == 0u) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);  // Fully transparent
    }

    let grid_offset = ypaint_grid_offset;
    let prims_offset = ypaint_prims_offset;

    // Grid pixel bounds
    let grid_pixel_w = grid_size.x * cell_size.x;
    let grid_pixel_h = grid_size.y * cell_size.y;

    // Two independent zoom transforms:
    //   visual_zoom — Ctrl+Scroll, mouse-anchored, around pane center
    //   cell_zoom   — Ctrl+Shift+Scroll, structural, around origin (0,0)
    //                 so content grows top-left→right-down exactly like text
    //                 cells growing in the text layer (they multiply from 0).
    // Both run BEFORE cell lookup / SDF eval, so edges stay crisp at any
    // zoom level — the shader re-samples SDF math per fragment.
    let vz_scale = uniforms.ypaint_ypaint_visual_zoom_scale;
    let vz_off   = uniforms.ypaint_ypaint_visual_zoom_off;
    let cz_scale = uniforms.ypaint_ypaint_cell_zoom_scale;
    let cz_off   = uniforms.ypaint_ypaint_cell_zoom_off;
    let vz_center = vec2<f32>(grid_pixel_w * 0.5, grid_pixel_h * 0.5);
    let after_visual = (input.position.xy - vz_center) / max(vz_scale, 0.0001)
                     + vz_center + vz_off;
    let pixel_pos = after_visual / max(cz_scale, 0.0001) + cz_off;

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
    let loop_count = min(cell_count, 64u);  // Safety cap

    var result_color = vec3<f32>(0.0);
    var result_alpha = 0.0;

    for (var i = 0u; i < loop_count; i++) {
        let raw_idx = storage_buffer[grid_offset + cell_start + 1u + i];

        // raw_idx is a primitive index (0, 1, 2...), not a data offset
        // Prim staging layout: [offset_table...][prim_data...]
        // Read data offset from offset table, then compute actual position
        let data_offset = storage_buffer[prims_offset + raw_idx];
        let prim_offset = prims_offset + prim_count + data_offset;

        // Compute primitive's screen Y offset from its rolling_row
        // Use signed arithmetic to handle scrolling past the primitive
        let rolling_row = ypaint_read_rolling_row(prim_offset);
        let rolling_row_0 = uniforms.ypaint_ypaint_rolling_row_0;
        let y_offset = f32(i32(rolling_row) - i32(rolling_row_0)) * cell_size.y;

        let prim_type = ypaint_read_prim_type(prim_offset);

        // Transform pixel position to primitive-local coords
        let local_pos = vec2<f32>(pixel_pos.x, pixel_pos.y - y_offset);


        // Glyph primitives — delegate atlas sampling to the active font
        // backend via font_glyph_sample() / font_glyph_size(). Bearing has
        // already been applied on the CPU, so (glyph_x, glyph_y) is the
        // top-left corner of the rendered glyph rectangle.
        if (prim_type == YPAINT_SDF_GLYPH) {
            let glyph_x = glyph_read_x(prim_offset);
            let glyph_y = glyph_read_y(prim_offset);
            let font_size = glyph_read_font_size(prim_offset);
            let packed = glyph_read_packed(prim_offset);
            let glyph_index = packed & 0xFFFFu;
            // High 16 bits hold (canvas_slot + 1). 0 means "default font".
            let slot_plus_one = (packed >> 16u) & 0xFFFFu;
            let font_slot = select(0u, slot_plus_one - 1u, slot_plus_one > 0u);
            let color_packed = glyph_read_color(prim_offset);

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

            let glyph_uv = (local_pos - glyph_min) / (glyph_size * pixel_scale);
            let glyph_alpha = font_glyph_sample(font_slot, glyph_index, glyph_uv, pixel_scale);

            if (glyph_alpha > 0.0) {
                let glyph_rgba = ypaint_unpack_color(color_packed);
                let alpha = glyph_alpha * glyph_rgba.a;
                result_color = mix(result_color, glyph_rgba.rgb, alpha);
                result_alpha = max(result_alpha, alpha);
            }
            continue;
        }

        // Evaluate SDF for non-glyph primitives
        let d = evaluate_sdf_2d(prim_offset + 1u, local_pos);

        // Render fill
        let fill_color = ypaint_read_fill_color(prim_offset);
        if (d < 0.0 && fill_color != 0u) {
            let fill_rgba = ypaint_unpack_color(fill_color);
            let edge_alpha = clamp(-d * 2.0, 0.0, 1.0);
            let alpha = edge_alpha * fill_rgba.a;
            result_color = mix(result_color, fill_rgba.rgb, alpha);
            result_alpha = max(result_alpha, alpha);
        }

        // Render stroke
        let stroke_color = ypaint_read_stroke_color(prim_offset);
        let stroke_width = ypaint_read_stroke_width(prim_offset);
        if (stroke_width > 0.0 && stroke_color != 0u) {
            let stroke_dist = abs(d) - stroke_width * 0.5;
            if (stroke_dist < 0.0) {
                let stroke_rgba = ypaint_unpack_color(stroke_color);
                let edge_alpha = clamp(-stroke_dist * 2.0, 0.0, 1.0);
                let alpha = edge_alpha * stroke_rgba.a;
                result_color = mix(result_color, stroke_rgba.rgb, alpha);
                result_alpha = max(result_alpha, alpha);
            }
        }
    }

    // Output with premultiplied alpha for proper blending
    return vec4<f32>(result_color * result_alpha, result_alpha);
}
