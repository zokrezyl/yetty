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
const YPLOT_FLAG_FIELD:  u32 = 16u;  // f(x,y) heatmap instead of a line curve

// Viridis colormap (perceptually-uniform) — Matt Zucker's 6th-order
// polynomial fit. Input clamped to [0,1]. Used for field/heatmap mode.
fn yplot_colormap(t_in: f32) -> vec3<f32> {
    let t = clamp(t_in, 0.0, 1.0);
    let c0 = vec3<f32>(0.2777273272234177,   0.005407344544966578, 0.3340998053353061);
    let c1 = vec3<f32>(0.1050930431085774,   1.404613529898575,    1.384590162594685);
    let c2 = vec3<f32>(-0.3308618287255563,  0.214847559468213,    0.09509516302823659);
    let c3 = vec3<f32>(-4.634230498983486,  -5.799100973351585,  -19.33244095627987);
    let c4 = vec3<f32>(6.228269936347081,   14.17993336680509,    56.69055260068105);
    let c5 = vec3<f32>(4.776384997670288,  -13.74514537774601,   -65.35303263337234);
    let c6 = vec3<f32>(-5.435455855934631,   4.645852612178535,   26.3124352495832);
    let rgb = c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
    return clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0));
}

// Plot chrome rides the brand palette: near-black canvas, teal-tinted
// separators, mint axes. Curve colours are supplied per-function and are
// left untouched.
const YPLOT_BG_COLOR:   vec3<f32> = vec3<f32>(0.043, 0.063, 0.078); // BRAND_BG       #0B1014
const YPLOT_GRID_COLOR: vec3<f32> = vec3<f32>(0.212, 0.290, 0.278); // BRAND_BORDER   #364A47
const YPLOT_AXIS_COLOR: vec3<f32> = vec3<f32>(0.420, 0.659, 0.573); // BRAND_ACCENT   #6BA892 (mint)

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
                   xMin: f32, xMax: f32, yMin: f32, yMax: f32,
                   bounds_w: f32, bounds_h: f32) -> vec3<f32> {
    var color = bg;
    // Thickness in PIXELS, not plot-UV — a UV width makes the vertical axis
    // fat on wide plots and the horizontal axis thin on short ones. Half-width
    // 1.0 → a crisp ~2px line at any plot size.
    let halfPx = 1.0;

    // Y-axis at x=0
    let xZero = (0.0 - xMin) / (xMax - xMin);
    if (xZero >= 0.0 && xZero <= 1.0 && abs(plotUV.x - xZero) * bounds_w < halfPx) {
        color = YPLOT_AXIS_COLOR;
    }

    // X-axis at y=0. plotUV.y = 0 is the top (= yMax), so the row for data
    // y=0 is measured from yMax down, matching the curve mapping.
    let yZeroRow = (yMax - 0.0) / (yMax - yMin);
    if (yZeroRow >= 0.0 && yZeroRow <= 1.0 && abs(plotUV.y - yZeroRow) * bounds_h < halfPx) {
        color = YPLOT_AXIS_COLOR;
    }

    // Border
    if (plotUV.x * bounds_w < halfPx || (1.0 - plotUV.x) * bounds_w < halfPx ||
        plotUV.y * bounds_h < halfPx || (1.0 - plotUV.y) * bounds_h < halfPx) {
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

    let dataX = mix(xMin, xMax, plotUV.x);
    // plotUV.y grows downward, so the top row (y=0) maps to yMax.
    let dataY = mix(yMax, yMin, plotUV.y);
    let yRange = yMax - yMin;
    let lineWidth = 3.0 / bounds_h;
    let isField = (flags & YPLOT_FLAG_FIELD) != 0u;

    // -------- pre-sample data buffers into the sampler array ---------------
    // Expressions that reference `f(x)` (where f was declared as `f=buffer`)
    // compile to LOAD_S against the buffer's slot. Walk the buffers ONCE,
    // fill samplers[i] with the linear-interpolated sample at plotUV.x.
    let data_count = yplot_data_count();
    var samplers: array<f32, 8>;
    samplers[0] = dataX; // legacy default for @buffer1 in expressions w/o data
    {
        var cursor: u32 = yplot_data_count_offset() + 1u;
        for (var bi = 0u; bi < YPLOT_MAX_CURVES; bi++) {
            if (bi >= data_count) { break; }
            let len = storage_buffer[cursor];
            let samples_off = cursor + 1u;
            if (len >= 2u && bi < 8u) {
                let idx_f   = plotUV.x * f32(len - 1u);
                let idx     = u32(floor(idx_f));
                let nxt     = min(idx + 1u, len - 1u);
                let t_lerp  = fract(idx_f);
                let v1 = bitcast<f32>(storage_buffer[samples_off + idx]);
                let v2 = bitcast<f32>(storage_buffer[samples_off + nxt]);
                let sv = mix(v1, v2, t_lerp);
                switch (bi) {
                    case 0u: { samplers[0] = sv; }
                    case 1u: { samplers[1] = sv; }
                    case 2u: { samplers[2] = sv; }
                    case 3u: { samplers[3] = sv; }
                    case 4u: { samplers[4] = sv; }
                    case 5u: { samplers[5] = sv; }
                    case 6u: { samplers[6] = sv; }
                    default: { samplers[7] = sv; }
                }
            }
            cursor = cursor + 1u + len;
        }
    }

    // -------- base layer: heatmap field OR background + grid/axes ----------
    var color = YPLOT_BG_COLOR;
    if (isField && func_count > 0u) {
        // 2D field f(x,y): evaluate function 0 per pixel and colormap it. The
        // value is assumed to sit in [-1, 1]; scale your expression (or wrap
        // it in tanh) to fit other ranges.
        let bc_off = yplot_bytecode_offset();
        let value = yfsvm_execute(bc_off, 0u, dataX, dataY, yplot_get_time(), samplers);
        color = yplot_colormap(value * 0.5 + 0.5);
    } else if ((flags & YPLOT_FLAG_GRID) != 0u) {
        color = yplot_draw_grid(color, plotUV);
    }
    if ((flags & YPLOT_FLAG_AXES) != 0u) {
        color = yplot_draw_axes(color, plotUV, xMin, xMax, yMin, yMax, bounds_w, bounds_h);
    }

    // -------- expression curves (yfsvm bytecode) -----------------------------
    if (!isField && func_count > 0u) {
        // bytecode begins at storage word 1 (word 0 is bytecode_len).
        let bc_off = yplot_bytecode_offset();

        for (var fi = 0u; fi < min(func_count, 8u); fi++) {
            let curve_color = yplot_unpack_color(yplot_get_colors(fi));
            let y = yfsvm_execute(bc_off, fi, dataX, dataY, yplot_get_time(), samplers);
            // plotUV.y grows downward (0 = top = yMax), so a larger value maps
            // to a row nearer the top — measure the value's row from yMax down.
            let yRow = (yMax - y) / yRange;
            color = yplot_line_blend(color, plotUV.y, yRow, lineWidth, curve_color);
        }
    }

    // -------- data-buffer curves --------------------------------------------
    // Sequentially walk [data_count][len_0][samples_0...][len_1]...
    // The shader can't binary-search since each entry's length is data-driven,
    // but a linear walk over N≤16 entries is fine. Skipped in field mode.
    if (!isField) {
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

                // Larger value → row nearer the top (plotUV.y = 0 = yMax).
                let yRow = (yMax - y) / yRange;
                color = yplot_line_blend(color, plotUV.y, yRow, lineWidth, curve_color);
            }

            cursor = cursor + 1u + len;
        }
    }

    return vec4<f32>(color, 1.0);
}

// Vertex/Fragment entry points for standalone pipeline.
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    // Pane-local pixel coordinate (origin at the pane's top-left). The
    // raw @builtin(position) in the fragment is the FRAMEBUFFER pixel
    // — once the layer's render target has a non-zero viewport offset
    // (yui titlebar pushes the terminal pane down by ~32 px), framebuffer
    // coords no longer line up with the pane origin, and the bounds-x/y
    // check below would compare canvas-local bounds against framebuffer
    // pixels — yplot ends up painting over the titlebar / tabbar header
    // strip instead of clipping to its widget box. Map NDC → pane pixels
    // here so the FS reasons in the pane's own coordinate system,
    // independent of where the pane sits in the big surface. Same trick
    // text-layer.wgsl / ydraw-layer.wgsl already use.
    @location(0) @interpolate(linear) pane_pixel: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    // Fullscreen triangle - 3 vertices cover entire screen.
    var pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );

    let vp_w = uniforms.yplot_viewport_w;
    let vp_h = uniforms.yplot_viewport_h;

    var out: VertexOutput;
    out.position = vec4<f32>(pos[vertex_index], 0.0, 1.0);
    // NDC.x: -1 → 0, 1 → vp_w
    // NDC.y:  1 → 0, -1 → vp_h (framebuffer y is top-down)
    out.pane_pixel = vec2<f32>(
        (pos[vertex_index].x * 0.5 + 0.5) * vp_w,
        (0.5 - pos[vertex_index].y * 0.5) * vp_h
    );
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

    let pane_px      = in.pane_pixel;
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
