// YPlot Complex Primitive Shader
// Renders plot from uniform values and bytecode in storage buffer
//
// Architecture:
//   Uniforms (binding 0): bounds, ranges, flags, function_count, colors
//   Storage buffer (binding 1): bytecode for yfsvm interpreter
//
// Generated accessors in yplot-gen.wgsl provide type-safe uniform access

const YPLOT_FLAG_GRID: u32 = 1u;
const YPLOT_FLAG_AXES: u32 = 2u;
const YPLOT_FLAG_LABELS: u32 = 4u;

const YPLOT_BG_COLOR: vec3<f32> = vec3<f32>(0.102, 0.102, 0.180);
const YPLOT_GRID_COLOR: vec3<f32> = vec3<f32>(0.267, 0.267, 0.267);
const YPLOT_AXIS_COLOR: vec3<f32> = vec3<f32>(0.667, 0.667, 0.667);

fn yplot_unpack_color(packed: u32) -> vec3<f32> {
    return vec3<f32>(
        f32((packed >> 16u) & 0xFFu) / 255.0,
        f32((packed >> 8u) & 0xFFu) / 255.0,
        f32(packed & 0xFFu) / 255.0
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

fn yplot_draw_axes(bg: vec3<f32>, plotUV: vec2<f32>, xMin: f32, xMax: f32, yMin: f32, yMax: f32) -> vec3<f32> {
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

// Main yplot render function - called by ypaint dispatcher
// Uses generated uniform accessors: yplot_get_*()
fn yplot_render(local_pos: vec2<f32>) -> vec4<f32> {
    // `local_pos` is ALREADY relative to the plot's top-left — fs_main has
    // subtracted bounds_x/y and discarded fragments outside [0..bounds_w] ×
    // [0..bounds_h]. Do NOT re-subtract bounds_xy here. Doing so was the
    // historic "second plot invisible" bug: when bounds_x = 320 (right
    // plot in a side-by-side pair), the old `local_pos.x < bounds_x` check
    // was true for every local_pos in [0..300] and the whole plot returned 0.
    let bounds_w = yplot_get_bounds_w();
    let bounds_h = yplot_get_bounds_h();

    // Normalize to 0-1 within plot area (local_pos already in [0..bounds_*]).
    let plotUV = local_pos / vec2<f32>(bounds_w, bounds_h);

    // Read plot parameters from uniforms
    let flags = yplot_get_flags();
    let func_count = yplot_get_function_count();
    let xMin = yplot_get_x_min();
    let xMax = yplot_get_x_max();
    let yMin = yplot_get_y_min();
    let yMax = yplot_get_y_max();

    // Background
    var color = YPLOT_BG_COLOR;

    // Grid
    if ((flags & YPLOT_FLAG_GRID) != 0u) {
        color = yplot_draw_grid(color, plotUV);
    }

    // Axes
    if ((flags & YPLOT_FLAG_AXES) != 0u) {
        color = yplot_draw_axes(color, plotUV, xMin, xMax, yMin, yMax);
    }

    // Map plotUV to data coordinates
    let dataX = mix(xMin, xMax, plotUV.x);
    let dataY = mix(yMin, yMax, plotUV.y);

    // Line width in normalized coords
    let lineWidth = 3.0 / bounds_h;

    // Evaluate and render each function using bytecode from storage buffer
    if (func_count > 0u) {
        var samplers: array<f32, 8>;
        samplers[0] = dataX;

        for (var fi = 0u; fi < min(func_count, 8u); fi++) {
            let func_color_packed = yplot_get_colors(fi);
            let func_color = yplot_unpack_color(func_color_packed);

            // Evaluate function using yfsvm (bytecode is in storage_buffer starting at offset 0)
            let y = yfsvm_execute(0u, fi, dataX, 0.0, samplers);

            // Map y to plot UV
            let yNorm = (y - yMin) / (yMax - yMin);
            let dist = abs(plotUV.y - yNorm);

            // Anti-aliased line
            if (dist < lineWidth) {
                let alpha = 1.0 - dist / lineWidth;
                color = mix(color, func_color, alpha);
            }
        }
    }

    return vec4<f32>(color, 1.0);
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
    // Viewport is the full pane. Transform this fragment's pane pixel into
    // the plot's SOURCE pixel by composing TWO independent zoom transforms:
    //     visual_zoom_* : non-intrusive (Ctrl+Scroll, mouse-anchored)
    //     cell_zoom_*   : intrusive (Ctrl+Shift+Scroll, cell-size scale)
    // Each has the same shape — source = (pane - c)/scale + c + off — so
    // applying them in sequence gives a combined zoom whose scale is the
    // product. SDF math runs at the final `source`, edges stay sharp at any
    // zoom level. No bitmap stretch anywhere. That is the point of yetty.
    let vz_scale = uniforms.yplot_visual_zoom_scale;
    let vz_off   = vec2<f32>(uniforms.yplot_visual_zoom_off_x,
                             uniforms.yplot_visual_zoom_off_y);
    let cz_scale = uniforms.yplot_cell_zoom_scale;
    let cz_off   = vec2<f32>(uniforms.yplot_cell_zoom_off_x,
                             uniforms.yplot_cell_zoom_off_y);
    let vp       = vec2<f32>(uniforms.yplot_viewport_w,
                             uniforms.yplot_viewport_h);
    let vp_c     = vp * 0.5;

    // visual_zoom is mouse-anchored around pane center (like Ctrl+Scroll).
    // cell_zoom is structural — zooms around the pane ORIGIN (0,0) so plot
    // positions grow the same direction as text cells growing from top-left.
    let pane_px = in.position.xy;
    let after_visual = (pane_px - vp_c) / max(vz_scale, 0.0001) + vp_c + vz_off;
    let source_px    = after_visual / max(cz_scale, 0.0001) + cz_off;

    let bounds_x = yplot_get_bounds_x();
    let bounds_y = yplot_get_bounds_y();
    let bounds_w = yplot_get_bounds_w();
    let bounds_h = yplot_get_bounds_h();

    // Cull fragments outside this plot's rect (both when zoom is identity
    // and when zoom moves the rect partly off-screen).
    let local_pos = source_px - vec2<f32>(bounds_x, bounds_y);
    if (local_pos.x < 0.0 || local_pos.y < 0.0 ||
        local_pos.x >= bounds_w || local_pos.y >= bounds_h) {
        discard;
    }

    return yplot_render(local_pos);
}
