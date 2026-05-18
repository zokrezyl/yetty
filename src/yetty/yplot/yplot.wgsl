// YPlot Complex Primitive Shader
// Renders plot from uniform values and a self-describing storage buffer.
//
// Architecture:
//   Uniforms (binding 0):       bounds, ranges, flags, function_count, colors
//   Storage  (binding 1):       bytecode + variable-count data buffers
//
// The shader walks the storage layout at runtime — no per-instance constants
// are baked into the WGSL, so adding/removing/resizing data buffers never
// triggers a pipeline recompile.

const YPLOT_FLAG_GRID:   u32 = 1u;
const YPLOT_FLAG_AXES:   u32 = 2u;
const YPLOT_FLAG_LABELS: u32 = 4u;

const YPLOT_BG_COLOR:   vec3<f32> = vec3<f32>(0.102, 0.102, 0.180);
const YPLOT_GRID_COLOR: vec3<f32> = vec3<f32>(0.267, 0.267, 0.267);
const YPLOT_AXIS_COLOR: vec3<f32> = vec3<f32>(0.667, 0.667, 0.667);

// Max curves the shader will iterate. Generous static bound for WGSL —
// the actual count is read from data; this just stops the loop from being
// unbounded. 16 ≫ any realistic mix of expressions + buffers.
const YPLOT_MAX_CURVES: u32 = 16u;

fn yplot_unpack_color(packed: u32) -> vec3<f32> {
    return vec3<f32>(
        f32((packed >> 16u) & 0xFFu) / 255.0,
        f32((packed >>  8u) & 0xFFu) / 255.0,
        f32( packed         & 0xFFu) / 255.0
    );
}

fn yplot_draw_grid(bg: vec3<f32>, plotUV: vec2<f32>) -> vec3<f32> {
    var color = bg;
    let gridX = fract(plotUV.x * 10.0);
    let gridY = fract(plotUV.y * 10.0);
    let lineWidth = 0.015;
    if (gridX < lineWidth || gridX > 1.0 - lineWidth ||
        gridY < lineWidth || gridY > 1.0 - lineWidth) {
        color = mix(color, YPLOT_GRID_COLOR, 0.5);
    }
    return color;
}

fn yplot_draw_axes(bg: vec3<f32>, plotUV: vec2<f32>,
                   xMin: f32, xMax: f32, yMin: f32, yMax: f32) -> vec3<f32> {
    var color = bg;
    let lineWidth = 0.008;

    // Y-axis at x=0
    let xZero = (0.0 - xMin) / (xMax - xMin);
    if (xZero >= 0.0 && xZero <= 1.0 && abs(plotUV.x - xZero) < lineWidth) {
        color = YPLOT_AXIS_COLOR;
    }

    // X-axis at y=0
    let yZero = (0.0 - yMin) / (yMax - yMin);
    if (yZero >= 0.0 && yZero <= 1.0 && abs(plotUV.y - yZero) < lineWidth) {
        color = YPLOT_AXIS_COLOR;
    }

    // Border
    if (plotUV.x < lineWidth || plotUV.x > 1.0 - lineWidth ||
        plotUV.y < lineWidth || plotUV.y > 1.0 - lineWidth) {
        color = YPLOT_AXIS_COLOR * 0.7;
    }

    return color;
}

// Anti-aliased horizontal line at y == yNorm (in plot UV). Returns the
// blended foreground over `color`. Shared by expression and buffer paths.
fn yplot_line_blend(color: vec3<f32>, plotUV_y: f32, yNorm: f32,
                    lineWidth: f32, curveColor: vec3<f32>) -> vec3<f32> {
    let dist = abs(plotUV_y - yNorm);
    if (dist < lineWidth) {
        let alpha = 1.0 - dist / lineWidth;
        return mix(color, curveColor, alpha);
    }
    return color;
}

// Main yplot render function - called by ydraw dispatcher.
fn yplot_render(local_pos: vec2<f32>) -> vec4<f32> {
    // `local_pos` is ALREADY relative to the plot's top-left — fs_main has
    // subtracted bounds_x/y and discarded fragments outside [0..bounds_w] ×
    // [0..bounds_h]. Do NOT re-subtract bounds_xy here. Doing so was the
    // historic "second plot invisible" bug.
    let bounds_w = yplot_get_bounds_w();
    let bounds_h = yplot_get_bounds_h();

    let plotUV = local_pos / vec2<f32>(bounds_w, bounds_h);

    let flags      = yplot_get_flags();
    let func_count = yplot_get_function_count();
    let xMin       = yplot_get_x_min();
    let xMax       = yplot_get_x_max();
    let yMin       = yplot_get_y_min();
    let yMax       = yplot_get_y_max();

    var color = YPLOT_BG_COLOR;

    if ((flags & YPLOT_FLAG_GRID) != 0u) {
        color = yplot_draw_grid(color, plotUV);
    }
    if ((flags & YPLOT_FLAG_AXES) != 0u) {
        color = yplot_draw_axes(color, plotUV, xMin, xMax, yMin, yMax);
    }

    let dataX = mix(xMin, xMax, plotUV.x);
    let yRange = yMax - yMin;
    let lineWidth = 3.0 / bounds_h;

    // -------- expression curves (yfsvm bytecode) -----------------------------
    if (func_count > 0u) {
        var samplers: array<f32, 8>;
        samplers[0] = dataX;

        // bytecode begins at storage word 1 (word 0 is bytecode_len).
        let bc_off = yplot_bytecode_offset();

        for (var fi = 0u; fi < min(func_count, 8u); fi++) {
            let curve_color = yplot_unpack_color(yplot_get_colors(fi));
            let y = yfsvm_execute(bc_off, fi, dataX, 0.0, samplers);
            let yNorm = (y - yMin) / yRange;
            color = yplot_line_blend(color, plotUV.y, yNorm, lineWidth, curve_color);
        }
    }

    // -------- data-buffer curves --------------------------------------------
    // Sequentially walk [data_count][len_0][samples_0...][len_1]...
    // The shader can't binary-search since each entry's length is data-driven,
    // but a linear walk over N≤16 entries is fine.
    let data_count = yplot_data_count();
    var cursor: u32 = yplot_data_count_offset() + 1u;  // first len_i

    for (var bi = 0u; bi < YPLOT_MAX_CURVES; bi++) {
        if (bi >= data_count) { break; }

        let len = storage_buffer[cursor];
        let samples_off = cursor + 1u;

        if (len >= 2u) {
            // Color slot: expressions first, then buffers, modulo 8.
            let color_slot = (func_count + bi) % 8u;
            let curve_color = yplot_unpack_color(yplot_get_colors(color_slot));

            // Map plotUV.x → fractional sample index, lerp neighbours.
            let idx_f   = plotUV.x * f32(len - 1u);
            let idx     = u32(floor(idx_f));
            let nxt     = min(idx + 1u, len - 1u);
            let t_lerp  = fract(idx_f);

            let v1 = bitcast<f32>(storage_buffer[samples_off + idx]);
            let v2 = bitcast<f32>(storage_buffer[samples_off + nxt]);
            let y  = mix(v1, v2, t_lerp);

            let yNorm = (y - yMin) / yRange;
            color = yplot_line_blend(color, plotUV.y, yNorm, lineWidth, curve_color);
        }

        cursor = cursor + 1u + len;
    }

    return vec4<f32>(color, 1.0);
}

// Vertex/Fragment entry points for standalone pipeline.
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    // Fullscreen triangle - 3 vertices cover entire screen.
    var pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );
    var uv: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(0.0,  1.0),
        vec2<f32>(2.0,  1.0),
        vec2<f32>(0.0, -1.0)
    );

    var out: VertexOutput;
    out.position = vec4<f32>(pos[vertex_index], 0.0, 1.0);
    out.uv = uv[vertex_index];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Viewport is the full pane. Transform this fragment's pane pixel into
    // the plot's SOURCE pixel by composing TWO independent zoom transforms:
    //     visual_zoom_* : non-intrusive (Ctrl+Scroll, mouse-anchored)
    //     cell_zoom_*   : intrusive (Ctrl+Shift+Scroll, cell-size scale)
    let vz_scale = uniforms.yplot_visual_zoom_scale;
    let vz_off   = vec2<f32>(uniforms.yplot_visual_zoom_off_x,
                             uniforms.yplot_visual_zoom_off_y);
    let cz_scale = uniforms.yplot_cell_zoom_scale;
    let cz_off   = vec2<f32>(uniforms.yplot_cell_zoom_off_x,
                             uniforms.yplot_cell_zoom_off_y);
    let vp       = vec2<f32>(uniforms.yplot_viewport_w,
                             uniforms.yplot_viewport_h);
    let vp_c     = vp * 0.5;

    let pane_px      = in.position.xy;
    let after_visual = (pane_px - vp_c) / max(vz_scale, 0.0001) + vp_c + vz_off;
    let source_px    = after_visual / max(cz_scale, 0.0001) + cz_off;

    let bounds_x = yplot_get_bounds_x();
    let bounds_y = yplot_get_bounds_y();
    let bounds_w = yplot_get_bounds_w();
    let bounds_h = yplot_get_bounds_h();

    let local_pos = source_px - vec2<f32>(bounds_x, bounds_y);
    if (local_pos.x < 0.0 || local_pos.y < 0.0 ||
        local_pos.x >= bounds_w || local_pos.y >= bounds_h) {
        discard;
    }

    return yplot_render(local_pos);
}
